/*
 * world.c - the living realm: regions in Shadow or liberated, towns that
 * grow and fall, sieges on the throne, realm projects, nemesis monsters,
 * monster ecology, day and night, and the seasons.
 *
 * The state is shared by every game thread (world_lock), saved to
 * WORLD_FILE and published for the website as WORLD_JSON_FILE.  It is
 * advanced at most once a minute by whichever player happens to act.
 */

#include "include.h"

#define NUM_REGIONS	8
#define HEARTLANDS	(-1)
#define MAX_NEMESES	5
#define MAX_NEWS	24
#define MAX_DONORS	5
#define NUM_SPECIES	(NUM_MONSTERS + MAX_EXTRA_MONSTERS)

	/* how fast things change */
#define SHADOW_PER_MINUTE	0.12	/* an untended region fills in ~14 hours */
#define SHADOW_PER_KILL		0.4
#define BLIGHTED		75.0
#define LIBERATED		25.0
#define TOWN_FALLS		95.0
#define TOWN_RETAKEN		50.0
#define PROSPERITY_DECAY	0.02	/* per minute */
#define ABUNDANCE_PER_KILL	0.01
#define ABUNDANCE_REGROWTH	0.002	/* per minute */
#define NIGHT_CYCLE		180	/* minutes: day then night */
#define NIGHT_LENGTH		60

static const char *regionNames[NUM_REGIONS] = {
    "the Eastern Marches", "the Dawnwood", "the Northern Reach", "the Frostfells",
    "the Western Wastes", "the Sunken Fens", "the Southern Expanse", "the Ember Coast"
};
static const char *townNames[NUM_REGIONS] = {
    "Eastwatch", "Dawnhold", "Northgate", "Frostholm",
    "Westmere", "Fenwick Hollow", "Southport", "Emberhaven"
};
static const char *seasonNames[4] = { "Bloom", "Harvest", "Hunter's Moon", "Deep Frost" };
static const char *seasonEffects[4] = {
    "wounds mend as you travel",
    "10% more gold from treasure",
    "10% more experience from fights",
    "monsters are 10% stronger, and gear is found 25% more often"
};

struct project_def {
    const char *name, *effect;
    double gold;
    long kills;
};
static const struct project_def projects[] = {
    { "Raise the Watchtowers", "Shadow spreads a quarter more slowly", 250000, 2000 },
    { "Rebuild the Bridge of Ages", "every shop is 5% cheaper", 500000, 4000 },
    { "Found the Mages' Guild", "spells cost 10% less mana", 750000, 6000 },
    { "Light the Great Forge", "gear is found 10% more often", 1000000, 8000 },
    { "Build the Great Library", "5% more experience from every fight", 1500000, 10000 },
};
#define NUM_PROJECTS ((int) (sizeof projects / sizeof projects[0]))

struct region_t { double shadow, prosperity; int fallen; };
struct donor_t { char name[SZ_NAME]; double gold; };
struct nemesis_t {
    int active, type, region, kills;
    double size, bounty;
    time_t born;
    char victim[SZ_NAME];
    char base[SZ_MONSTER_NAME];		/* e.g. "Grishnak the Troll" */
};
struct news_t { time_t when; char text[160]; };

static struct {
    time_t lastTick, nextSiege, overrunUntil, lastRoam;
    long siegeId, lastSiegeId;
    int lastSiegeWon;
    double army, armyMax;
    int project;
    double projectGold;
    long projectKills;
    struct donor_t donors[MAX_DONORS];
    struct region_t region[NUM_REGIONS];
    struct nemesis_t nemesis[MAX_NEMESES];
    double abundance[NUM_SPECIES];
    struct news_t news[MAX_NEWS];
    int numNews;
} W;

static pthread_mutex_t world_lock = PTHREAD_MUTEX_INITIALIZER;
static int worldLoaded = FALSE;
static int forceNemesis = FALSE;	/* test builds */

	/* messages to announce once the lock is released */
static char pending[8][SZ_LINE];
static int numPending = 0;

static void Do_world_news_locked(const char *text, int announce)
{
    int i;

    if (W.numNews == MAX_NEWS) {
	for (i = 1; i < MAX_NEWS; i++) W.news[i - 1] = W.news[i];
	--W.numNews;
    }
    W.news[W.numNews].when = time(NULL);
    snprintf(W.news[W.numNews].text, sizeof W.news[0].text, "%s", text);
    ++W.numNews;

    if (announce && numPending < 8) {
	snprintf(pending[numPending++], SZ_LINE, "*** %s ***\n", text);
    }
}

static void Do_world_flush(struct client_t *c)
{
    char copy[8][SZ_LINE];
    int i, n;

    pthread_mutex_lock(&world_lock);
    n = numPending;
    for (i = 0; i < n; i++) strcpy(copy[i], pending[i]);
    numPending = 0;
    pthread_mutex_unlock(&world_lock);

    for (i = 0; i < n; i++) {
	Do_broadcast(c, copy[i]);
	Do_log(EVENT_LOG, copy[i]);
    }
}

static const char *Cap(const char *s, char *buf)
{
    if (buf != s) memmove(buf, s, strlen(s) + 1);
    buf[0] = toupper((unsigned char) buf[0]);
    return buf;
}

/************************************************************************
/ time, places
*************************************************************************/

/*
 * Trading posts stand along the eight compass roads out from 0,0: on the
 * diagonals (as in the original game) and on the axes, at 100 x n * n
 * along the road (100, 400, 900, 1600, ...), so every region has them.
 * Returns n (the post's rank, 1 = nearest the throne), or 0 if (x, y) is
 * not a post.  *diagonal says which kind of road it is on.
 */
int Do_trading_post_rank(double x, double y, int *diagonal)
{
    double ax = fabs(x), ay = fabs(y), d, n;

    if (diagonal) *diagonal = FALSE;
    if (ax == 0 && ay == 0) return 0;
    if (ax == ay) { d = ax; if (diagonal) *diagonal = TRUE; }
    else if (ax == 0) d = ay;
    else if (ay == 0) d = ax;
    else return 0;
    n = sqrt(d / 100.0);
    return floor(n) == n ? (int) n : 0;
}

	/* the post nearest (x, y) on its region's road */
static void Do_nearest_post(int r, double x, double y, double *px, double *py)
{
    static const int dx[NUM_REGIONS] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static const int dy[NUM_REGIONS] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    double t = MAX(fabs(x), fabs(y));
    double n = MAX(1.0, floor(sqrt(t / 100.0) + 0.5));

    *px = dx[r] * 100.0 * n * n;
    *py = dy[r] * 100.0 * n * n;
}

int Do_world_is_night(void)
{
    return (time(NULL) / 60) % NIGHT_CYCLE >= NIGHT_CYCLE - NIGHT_LENGTH;
}

	/* weekly seasons, starting on Mondays (UTC) */
int Do_world_season(void)
{
    return (int) (((time(NULL) / 86400) + 3) / 7 % 4);
}

static int Do_region_at(double x, double y, double circle)
{
    double a;
    int o;

    if (circle <= 1.0) return HEARTLANDS;
    a = atan2(y, x);			/* east = 0, north = pi/2 */
    o = (int) floor((a + M_PI / 8) / (M_PI / 4));
    return ((o % NUM_REGIONS) + NUM_REGIONS) % NUM_REGIONS;
}

static int Do_player_region(struct client_t *c)
{
    return Do_region_at(c->player.x, c->player.y, c->player.circle);
}

static const char *Do_region_state(int r)
{
    if (r == HEARTLANDS) return time(NULL) < W.overrunUntil ? "Overrun" : "Safe";
    if (W.region[r].shadow >= BLIGHTED) return "Blighted";
    if (W.region[r].shadow < LIBERATED) return "Liberated";
    return "Contested";
}

static const char *Do_town_tier(int r)
{
    double p;

    if (r == HEARTLANDS) return "";
    if (W.region[r].fallen) return "Fallen";
    p = W.region[r].prosperity;
    if (p >= 85) return "Flourishing";
    if (p >= 60) return "Thriving";
    if (p >= 20) return "Steady";
    return "Struggling";
}

static int Do_project_done(int i) { return W.project > i; }

/************************************************************************
/ saving and the website
*************************************************************************/

static void Do_world_defaults_locked(void)
{
    int i;

    memset(&W, 0, sizeof W);
    for (i = 0; i < NUM_REGIONS; i++) {
	W.region[i].shadow = 35 + RND() * 35;
	W.region[i].prosperity = 30 + RND() * 30;
    }
    for (i = 0; i < NUM_SPECIES; i++) W.abundance[i] = 1.0;
}

static void Do_world_load_locked(void)
{
    FILE *f;
    char line[512], word[32];
    int i, n;

    worldLoaded = TRUE;
    Do_world_defaults_locked();
    if ((f = fopen(WORLD_FILE, "r")) == NULL) {
	return;
    }
    while (fgets(line, sizeof line, f)) {
	line[strcspn(line, "\n")] = '\0';
	if (sscanf(line, "%31s%n", word, &n) != 1) continue;
	char *rest = line + n;

	if (!strcmp(word, "state")) {
	    long a, b, c2, d, e;
	    sscanf(rest, "%ld %ld %ld %ld %ld %ld %ld %d %lf %lf %d %lf %ld",
		    &a, &b, &c2, &d, &e, &W.siegeId, &W.lastSiegeId, &W.lastSiegeWon,
		    &W.army, &W.armyMax, &W.project, &W.projectGold, &W.projectKills);
	    W.lastTick = a; W.nextSiege = b; W.overrunUntil = c2; W.lastRoam = d;
	    (void) e;
	}
	else if (!strcmp(word, "region")) {
	    if (sscanf(rest, "%d", &i) == 1 && i >= 0 && i < NUM_REGIONS) {
		sscanf(rest, "%*d %lf %lf %d", &W.region[i].shadow,
			&W.region[i].prosperity, &W.region[i].fallen);
	    }
	}
	else if (!strcmp(word, "donor")) {
	    for (i = 0; i < MAX_DONORS && W.donors[i].name[0]; i++) ;
	    if (i < MAX_DONORS) sscanf(rest, "%32s %lf", W.donors[i].name, &W.donors[i].gold);
	}
	else if (!strcmp(word, "eco")) {
	    double v;
	    if (sscanf(rest, "%d %lf", &i, &v) == 2 && i >= 0 && i < NUM_SPECIES) W.abundance[i] = v;
	}
	else if (!strcmp(word, "nemesis")) {
	    for (i = 0; i < MAX_NEMESES && W.nemesis[i].active; i++) ;
	    if (i < MAX_NEMESES) {
		struct nemesis_t *m = &W.nemesis[i];
		long born;
		int k = 0;
		if (sscanf(rest, "%d %lf %d %d %lf %ld %32s %n", &m->type, &m->size,
			&m->region, &m->kills, &m->bounty, &born, m->victim, &k) >= 7 && k > 0) {
		    m->born = born;
		    snprintf(m->base, sizeof m->base, "%s", rest + k);
		    m->active = TRUE;
		}
	    }
	}
	else if (!strcmp(word, "news")) {
	    long when;
	    int k = 0;
	    if (W.numNews < MAX_NEWS && sscanf(rest, "%ld %n", &when, &k) >= 1 && k > 0) {
		W.news[W.numNews].when = when;
		snprintf(W.news[W.numNews].text, sizeof W.news[0].text, "%s", rest + k);
		++W.numNews;
	    }
	}
    }
    fclose(f);
}

static void Do_json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
	if (*s == '"' || *s == '\\') fputc('\\', f);
	if ((unsigned char) *s >= 32) fputc(*s, f);
    }
    fputc('"', f);
}

static void Do_world_save_locked(void)
{
    FILE *f;
    int i;
    time_t now = time(NULL);

    if ((f = fopen(TEMP_WORLD_FILE, "w")) != NULL) {
	fprintf(f, "state %ld %ld %ld %ld 0 %ld %ld %d %.2f %.2f %d %.0f %ld\n",
		(long) W.lastTick, (long) W.nextSiege, (long) W.overrunUntil,
		(long) W.lastRoam, W.siegeId, W.lastSiegeId, W.lastSiegeWon,
		W.army, W.armyMax, W.project, W.projectGold, W.projectKills);
	for (i = 0; i < NUM_REGIONS; i++) {
	    fprintf(f, "region %d %.3f %.3f %d\n", i, W.region[i].shadow,
		    W.region[i].prosperity, W.region[i].fallen);
	}
	for (i = 0; i < MAX_DONORS; i++) {
	    if (W.donors[i].name[0]) fprintf(f, "donor %s %.0f\n", W.donors[i].name, W.donors[i].gold);
	}
	for (i = 0; i < NUM_SPECIES; i++) {
	    if (W.abundance[i] < 0.999) fprintf(f, "eco %d %.4f\n", i, W.abundance[i]);
	}
	for (i = 0; i < MAX_NEMESES; i++) {
	    struct nemesis_t *m = &W.nemesis[i];
	    if (m->active) {
		fprintf(f, "nemesis %d %.0f %d %d %.0f %ld %s %s\n", m->type, m->size,
			m->region, m->kills, m->bounty, (long) m->born,
			m->victim[0] ? m->victim : "-", m->base);
	    }
	}
	for (i = 0; i < W.numNews; i++) {
	    fprintf(f, "news %ld %s\n", (long) W.news[i].when, W.news[i].text);
	}
	fclose(f);
	rename(TEMP_WORLD_FILE, WORLD_FILE);
    }

	/* the website's view */
    if ((f = fopen(TEMP_WORLD_JSON_FILE, "w")) == NULL) {
	return;
    }
    fprintf(f, "{\"updated\":%ld,\"night\":%s,\"season\":", (long) now,
	    Do_world_is_night() ? "true" : "false");
    Do_json_str(f, seasonNames[Do_world_season()]);
    fprintf(f, ",\"seasonEffect\":");
    Do_json_str(f, seasonEffects[Do_world_season()]);
    fprintf(f, ",\"overrunUntil\":%ld,\"regions\":[", (long) W.overrunUntil);
    for (i = 0; i < NUM_REGIONS; i++) {
	char cap[64];
	fprintf(f, "%s{\"name\":", i ? "," : "");
	Do_json_str(f, Cap(regionNames[i], cap));
	fprintf(f, ",\"town\":");
	Do_json_str(f, townNames[i]);
	fprintf(f, ",\"shadow\":%.0f,\"state\":", W.region[i].shadow);
	Do_json_str(f, Do_region_state(i));
	fprintf(f, ",\"prosperity\":%.0f,\"townState\":", W.region[i].prosperity);
	Do_json_str(f, Do_town_tier(i));
	fprintf(f, "}");
    }
    fprintf(f, "],\"siege\":{\"active\":%s,\"army\":%.0f,\"armyMax\":%.0f,\"lastWon\":%s,\"fought\":%s},",
	    W.siegeId > W.lastSiegeId ? "true" : "false", W.army, W.armyMax,
	    W.lastSiegeWon ? "true" : "false", W.lastSiegeId > 0 ? "true" : "false");
    fprintf(f, "\"project\":{\"index\":%d,\"total\":%d", W.project, NUM_PROJECTS);
    if (W.project < NUM_PROJECTS) {
	fprintf(f, ",\"name\":");
	Do_json_str(f, projects[W.project].name);
	fprintf(f, ",\"effect\":");
	Do_json_str(f, projects[W.project].effect);
	fprintf(f, ",\"gold\":%.0f,\"goldNeeded\":%.0f,\"kills\":%ld,\"killsNeeded\":%ld,\"donors\":[",
		W.projectGold, projects[W.project].gold, W.projectKills, projects[W.project].kills);
	for (i = 0; i < MAX_DONORS && W.donors[i].name[0]; i++) {
	    fprintf(f, "%s{\"name\":", i ? "," : "");
	    Do_json_str(f, W.donors[i].name);
	    fprintf(f, ",\"gold\":%.0f}", W.donors[i].gold);
	}
	fprintf(f, "]");
    }
    fprintf(f, ",\"done\":[");
    for (i = 0; i < W.project && i < NUM_PROJECTS; i++) {
	fprintf(f, "%s{\"name\":", i ? "," : "");
	Do_json_str(f, projects[i].name);
	fprintf(f, ",\"effect\":");
	Do_json_str(f, projects[i].effect);
	fprintf(f, "}");
    }
    fprintf(f, "]},\"nemeses\":[");
    {
	int first = TRUE;
	for (i = 0; i < MAX_NEMESES; i++) {
	    struct nemesis_t *m = &W.nemesis[i];
	    char name[SZ_LINE], cap[64];
	    if (!m->active) continue;
	    Do_nemesis_name(i, name);
	    fprintf(f, "%s{\"name\":", first ? "" : ",");
	    Do_json_str(f, name);
	    fprintf(f, ",\"kills\":%d,\"bounty\":%.0f,\"region\":", m->kills, m->bounty);
	    Do_json_str(f, Cap(regionNames[m->region], cap));
	    fprintf(f, "}");
	    first = FALSE;
	}
    }
    fprintf(f, "],\"news\":[");
    for (i = W.numNews - 1; i >= 0; i--) {
	fprintf(f, "%s{\"when\":%ld,\"text\":", i == W.numNews - 1 ? "" : ",", (long) W.news[i].when);
	Do_json_str(f, W.news[i].text);
	fprintf(f, "}");
    }
    fprintf(f, "]}\n");
    fclose(f);
    rename(TEMP_WORLD_JSON_FILE, WORLD_JSON_FILE);
}

static void Do_world_ensure_locked(void)
{
    if (!worldLoaded) Do_world_load_locked();
}

/************************************************************************
/ nemeses
*************************************************************************/

	/* "Grishnak the Troll, the Player-Eater" */
void Do_nemesis_name(int i, char *out)
{
    struct nemesis_t *m = &W.nemesis[i];
    char title[SZ_LINE];

    if (m->kills >= 10) strcpy(title, "the Unstoppable");
    else if (m->kills >= 6) strcpy(title, "the Dread");
    else if (m->kills >= 3) strcpy(title, "the Player-Eater");
    else if (m->victim[0] && strcmp(m->victim, "-")) {
	snprintf(title, sizeof title, "Bane of %c%s", toupper((unsigned char) m->victim[0]), m->victim + 1);
    }
    else strcpy(title, "the Hunter");
    snprintf(out, SZ_MONSTER_NAME, "%s, %s", m->base, title);
}

/************************************************************************
/ the minute tick
*************************************************************************/

static int Do_count_online(struct client_t *c)
{
    struct game_t *g;
    int n = 0;

    Do_lock_mutex(&c->realm->realm_lock);
    for (g = c->realm->games; g != NULL; g = g->next_game) {
	if (g->description != NULL) ++n;
    }
    Do_unlock_mutex(&c->realm->realm_lock);
    return n;
}

static void Do_world_minutes_locked(double minutes)
{
    int i, j;
    double growth = SHADOW_PER_MINUTE * (Do_project_done(0) ? 0.75 : 1.0);
    char msg[SZ_LINE], cap[64];

    for (i = 0; i < NUM_REGIONS; i++) {
	struct region_t *r = &W.region[i];
	double before = r->shadow;

	r->shadow = MIN(100.0, r->shadow + growth * minutes);
	r->prosperity = MAX(0.0, r->prosperity - PROSPERITY_DECAY * minutes);

	if (before < BLIGHTED && r->shadow >= BLIGHTED) {
	    sprintf(msg, "Shadow has fallen over %s. Its monsters grow stronger, and richer.", regionNames[i]);
	    Do_world_news_locked(Cap(msg, msg), TRUE);
	}
	if (!r->fallen && r->shadow >= TOWN_FALLS) {
	    r->fallen = TRUE;
	    r->prosperity = 0;
	    sprintf(msg, "%s has fallen to the Shadow! Its trading posts lie in ruins.", townNames[i]);
	    Do_world_news_locked(msg, TRUE);
	}
    }

	/* hunted species slowly recover */
    for (i = 0; i < NUM_SPECIES; i++) {
	if (W.abundance[i] < 1.0) {
	    double before = W.abundance[i];
	    W.abundance[i] = MIN(1.0, W.abundance[i] + ABUNDANCE_REGROWTH * minutes);
	    (void) before;
	}
    }

	/* nemeses roam once an hour */
    if (time(NULL) - W.lastRoam >= 3600) {
	W.lastRoam = time(NULL);
	for (j = 0; j < MAX_NEMESES; j++) {
	    if (W.nemesis[j].active && RND() < 0.5) {
		W.nemesis[j].region = (W.nemesis[j].region + (RND() < 0.5 ? 1 : NUM_REGIONS - 1)) % NUM_REGIONS;
	    }
	}
    }
    (void) cap;
}

	/* advance the world (at most once a minute) and start sieges */
static void Do_world_tick(struct client_t *c)
{
    time_t now = time(NULL);
    int startSiege = FALSE, players, minutes;
    double hours;

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    if (W.lastTick == 0) W.lastTick = now;
    if (now - W.lastTick < 60) {
	pthread_mutex_unlock(&world_lock);
	return;
    }
    minutes = (int) MIN(24 * 60, (now - W.lastTick) / 60);
    W.lastTick = now;
    Do_world_minutes_locked(minutes);

    hours = Do_env_minutes("PHANTASIA_SIEGE_HOURS", 8);
    if (W.nextSiege == 0) W.nextSiege = now + (time_t) (hours * 3600 * (0.4 + 0.6 * RND()));
    if (now >= W.nextSiege && W.siegeId <= W.lastSiegeId) startSiege = TRUE;
    Do_world_save_locked();
    pthread_mutex_unlock(&world_lock);

    if (startSiege && Do_current_world_event(c) == WE_NONE) {
	players = Do_count_online(c);
	Do_world_start_siege(c, players);
    }
    Do_world_flush(c);
}

/************************************************************************
/ sieges
*************************************************************************/

void Do_world_start_siege(struct client_t *c, int players)
{
    char msg[SZ_LINE];
    double hours = Do_env_minutes("PHANTASIA_SIEGE_HOURS", 8);

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    W.siegeId = W.lastSiegeId + 1;
    W.armyMax = W.army = MIN(1500.0, 100.0 + 60.0 * MAX(1, players));
    W.nextSiege = time(NULL) + (time_t) (hours * 3600 * (0.6 + 0.8 * RND()));
    sprintf(msg, "A monster army %.0f strong marches on the throne!", W.armyMax);
    Do_world_news_locked(msg, FALSE);
    Do_world_save_locked();
    pthread_mutex_unlock(&world_lock);

    Do_world_event_start(c, WE_SIEGE, 45);
    Do_broadcast(c, "*** Every monster slain anywhere in the realm pushes the army back. Defend the throne! ***\n");
}

	/* the banner text while a siege is on */
void Do_world_siege_effect(char *out)
{
    double army, max;

    pthread_mutex_lock(&world_lock);
    army = W.army; max = W.armyMax;
    pthread_mutex_unlock(&world_lock);
    sprintf(out, "a monster army marches on the throne: %.0f of %.0f remain. Slay monsters anywhere to push it back",
	    MAX(0.0, army), max);
}

long Do_world_siege_bucket(void)
{
    long b;

    pthread_mutex_lock(&world_lock);
    b = (long) (W.army / 10);
    pthread_mutex_unlock(&world_lock);
    return b;
}

	/* the siege timer ran out (called when the world event stops) */
void Do_world_siege_over(struct client_t *c)
{
    char msg[SZ_LINE];
    int i, lost = FALSE;

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    if (W.siegeId > W.lastSiegeId) {		/* not yet won: lost */
	W.lastSiegeId = W.siegeId;
	W.lastSiegeWon = FALSE;
	W.overrunUntil = time(NULL) + 24 * 3600;
	for (i = 0; i < NUM_REGIONS; i++) W.region[i].shadow = MIN(100.0, W.region[i].shadow + 15);
	Do_world_news_locked("The siege is lost! Monsters overrun the Lord's Chamber, and Shadow spreads across the realm.", TRUE);
	Do_world_save_locked();
	lost = TRUE;
    }
    pthread_mutex_unlock(&world_lock);

    if (lost) {
	Do_lock_mutex(&c->realm->kings_gold_lock);
	c->realm->kings_gold = floor(c->realm->kings_gold * 0.75);
	Do_unlock_mutex(&c->realm->kings_gold_lock);
	Do_world_flush(c);
	Do_broadcast(c, "*** For a day the throne room is held by monsters: no decrees can be made, and the Heartlands are deadly. ***\n");
    }
    (void) msg;
}

int Do_world_throne_overrun(void)
{
    return time(NULL) < W.overrunUntil;
}

static void Do_world_siege_won_locked(struct client_t *c)
{
    W.lastSiegeId = W.siegeId;
    W.lastSiegeWon = TRUE;
    W.army = 0;
    Do_world_news_locked("The siege is broken! The realm's heroes have driven the army from the throne.", TRUE);
}

/************************************************************************
/ hooks from the game
*************************************************************************/

	/* every turn for every player */
void Do_world_turn(struct client_t *c)
{
    char line[SZ_LINE], cap[64];
    int r, season = Do_world_season(), night = Do_world_is_night();
    double pay;
    long lastId;
    int won;

    Do_world_tick(c);

	/* Bloom: wounds mend as you travel */
    if (season == 0 && c->player.energy < c->player.max_energy + c->player.shield &&
	    c->player.energy > 0) {
	Do_energy(c, MIN(c->player.max_energy + c->player.shield,
		c->player.energy + ceil((c->player.max_energy + c->player.shield) * 0.01)),
		c->player.max_energy, c->player.shield, c->battle.force_field, FALSE);
    }

	/* paid for defending the throne */
    pthread_mutex_lock(&world_lock);
    lastId = W.lastSiegeId; won = W.lastSiegeWon;
    pthread_mutex_unlock(&world_lock);
    if (c->siegeKills > 0 && c->siegeId == lastId && c->siegeId > 0) {
	if (won) {
	    pay = MIN(50000.0, floor(c->siegeKills * (100.0 + 25.0 * c->player.circle)));
	    c->player.gold += pay;
	    Do_gold(c, 0, TRUE);
	    sprintf(line, "For defending the throne (%.0f of the army slain), you are paid %.0f gold.\n",
		    c->siegeKills, pay);
	    Do_send_line(c, line);
	    Do_send_chat_self(c, line);
	}
	c->siegeKills = 0;
    }

	/* tell the player where they are, and the browser what to show */
    pthread_mutex_lock(&world_lock);
    r = Do_player_region(c);
    if (r != c->worldRegion) {
	c->worldRegion = r;
	if (r == HEARTLANDS) {
	    sprintf(line, "You enter the Heartlands%s.\n",
		    Do_world_throne_overrun() ? ", overrun by monsters" : "");
	}
	else {
	    double px, py;
	    Do_nearest_post(r, c->player.x, c->player.y, &px, &py);
	    sprintf(line, "You enter %s (%s, %.0f%% Shadow). The town of %s is %s; its nearest trading post is at %.0f, %.0f.\n",
		    regionNames[r], Do_region_state(r), W.region[r].shadow,
		    townNames[r], Do_town_tier(r), px, py);
	}
	Do_send_line(c, Cap(line, line));
    }
    if (r == HEARTLANDS) {
	sprintf(line, "The Heartlands\n%s\n0\n\n\n%d\n%s\n", Do_region_state(r), night,
		seasonNames[season]);
    }
    else {
	sprintf(line, "%s\n%s\n%.0f\n%s\n%s\n%d\n%s\n", Cap(regionNames[r], cap),
		Do_region_state(r), W.region[r].shadow, townNames[r], Do_town_tier(r),
		night, seasonNames[season]);
    }
    pthread_mutex_unlock(&world_lock);

    if (c->isBrowser && strcmp(line, c->worldShown) != 0) {
	snprintf(c->worldShown, sizeof c->worldShown, "%s", line);
	Do_send_int(c, WORLD_PACKET);
	Do_send_string(c, line);
	Do_send_buffer(c);
    }
}

	/* a monster fell to this player */
void Do_world_kill(struct client_t *c)
{
    struct opponent_t *o = c->battle.opponent;
    char msg[SZ_LINE], name[SZ_LINE], cap[64];
    int r = Do_player_region(c), finished = FALSE, nem = c->nemesisFight, i;
    double weight = c->championFight ? 5 : 1, bounty = 0, before;
    struct gear_item_t item;

    if (nem >= 0) weight = 20;

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();

	/* push back the Shadow and help the town */
    if (r != HEARTLANDS) {
	struct region_t *rg = &W.region[r];
	before = rg->shadow;
	rg->shadow = MAX(0.0, rg->shadow - SHADOW_PER_KILL * weight);
	rg->prosperity = MIN(100.0, rg->prosperity + 0.05 * weight);
	if (before >= LIBERATED && rg->shadow < LIBERATED) {
	    sprintf(msg, "%s struck the blow that liberated %s!", c->modifiedName, regionNames[r]);
	    Do_world_news_locked(msg, TRUE);
	}
	if (rg->fallen && rg->shadow < TOWN_RETAKEN) {
	    rg->fallen = FALSE;
	    rg->prosperity = 10;
	    sprintf(msg, "%s has been retaken from the Shadow, thanks to %s!", townNames[r], c->modifiedName);
	    Do_world_news_locked(msg, TRUE);
	}
    }

	/* the species thins out */
    if (o->type >= 0 && o->type < NUM_SPECIES && nem < 0) {
	before = W.abundance[o->type];
	W.abundance[o->type] = MAX(0.05, W.abundance[o->type] - ABUNDANCE_PER_KILL);
	if (before >= 0.25 && W.abundance[o->type] < 0.25) {
	    sprintf(msg, "Hunters have all but wiped out %s. Their predators grow bold.",
		    Do_strip_article(c->realm->monster[o->type].name));
	    Do_world_news_locked(msg, FALSE);
	}
    }

	/* the siege */
    if (W.siegeId > W.lastSiegeId && Do_current_world_event(c) == WE_SIEGE) {
	if (c->siegeId != W.siegeId) { c->siegeId = W.siegeId; c->siegeKills = 0; }
	c->siegeKills += weight;
	W.army -= weight;
	if (W.army <= 0) {
	    Do_world_siege_won_locked(c);
	    sprintf(msg, "%s cut down the last of the besieging army!", c->modifiedName);
	    Do_world_news_locked(msg, FALSE);
	    finished = TRUE;
	}
    }

	/* the realm project */
    if (W.project < NUM_PROJECTS) {
	W.projectKills++;
	if (W.projectGold >= projects[W.project].gold && W.projectKills >= projects[W.project].kills) {
	    sprintf(msg, "The realm has completed %s! From now on, %s.",
		    projects[W.project].name, projects[W.project].effect);
	    Do_world_news_locked(msg, TRUE);
	    W.project++; W.projectGold = 0; W.projectKills = 0;
	    memset(W.donors, 0, sizeof W.donors);
	}
    }

	/* a nemesis falls */
    if (nem >= 0 && nem < MAX_NEMESES && W.nemesis[nem].active) {
	Do_nemesis_name(nem, name);
	bounty = W.nemesis[nem].bounty;
	sprintf(msg, "%s has slain %s and claims a bounty of %.0f gold!", c->modifiedName, name, bounty);
	Do_world_news_locked(msg, TRUE);
	W.nemesis[nem].active = FALSE;
	if (r != HEARTLANDS) W.region[r].shadow = MAX(0.0, W.region[r].shadow - 10);
    }

	/* rare creatures */
    if (c->rareFight) {
	sprintf(msg, "%s has slain %s near %s!", c->modifiedName, o->name, c->player.area);
	Do_world_news_locked(msg, TRUE);
    }

    Do_world_save_locked();
    pthread_mutex_unlock(&world_lock);

    if (bounty > 0) {
	c->player.gold += bounty;
	Do_gold(c, 0, TRUE);
	Do_gear_make(&item, -1, c->player.circle + 3, TIER_ENCHANTED);
	Do_send_line(c, "The nemesis's hoard is yours.\n");
	Do_gear_offer(c, &item);
    }
    c->nemesisFight = -1;
    c->rareFight = 0;
    Do_world_flush(c);
    if (finished) {
	Do_lock_mutex(&c->realm->kings_gold_lock);
	c->realm->kings_gold = MIN(2000000.0, c->realm->kings_gold + 25000);
	Do_unlock_mutex(&c->realm->kings_gold_lock);
	Do_world_event_stop(c);
    }
    (void) cap; (void) i;
}

	/* a monster killed this player: it may become (or grow as) a nemesis */
void Do_world_player_slain(struct client_t *c)
{
    struct opponent_t *o = c->battle.opponent;
    char msg[SZ_LINE], name[SZ_LINE];
    int i, slot = -1, r = Do_player_region(c), grew = FALSE;

    if (o->special_type == SM_IT_COMBAT || c->wizard >= 3) return;
    if (c->nemesisFight < 0 && o->special_type != SM_NONE) return;
    if (c->nemesisFight < 0 && !c->championFight && RND() >= 0.2) return;
    if (r == HEARTLANDS) r = (int) ROLL(0.0, NUM_REGIONS) % NUM_REGIONS;

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    if (c->nemesisFight >= 0 && W.nemesis[c->nemesisFight].active) {
	struct nemesis_t *m = &W.nemesis[c->nemesisFight];
	m->kills++;
	m->bounty += 500.0 * MAX(1.0, c->player.circle) + floor(c->player.gold * 0.1);
	m->size = MAX(m->size, o->size);
	strncpy(m->victim, c->player.lcname, SZ_NAME - 1);
	Do_nemesis_name(c->nemesisFight, name);
	sprintf(msg, "%s has claimed another victim: %s. The bounty on it is now %.0f gold.",
		name, c->modifiedName, m->bounty);
	Do_world_news_locked(msg, TRUE);
	grew = TRUE;
    }
    else {
	for (i = 0; i < MAX_NEMESES; i++) if (!W.nemesis[i].active) { slot = i; break; }
	if (slot < 0) {				/* replace the weakest */
	    slot = 0;
	    for (i = 1; i < MAX_NEMESES; i++) if (W.nemesis[i].bounty < W.nemesis[slot].bounty) slot = i;
	}
	struct nemesis_t *m = &W.nemesis[slot];
	memset(m, 0, sizeof *m);
	m->active = TRUE;
	m->type = o->type;
	m->size = MAX(1.0, o->size);
	m->region = r;
	m->kills = 1;
	m->bounty = 1000.0 + 300.0 * m->size + floor(c->player.gold * 0.1);
	m->born = time(NULL);
	strncpy(m->victim, c->player.lcname, SZ_NAME - 1);
	if (c->championFight) {
	    snprintf(m->base, sizeof m->base, "%s", o->name);
	}
	else {
	    static const char *names[] = { "Gorrak", "Vexis", "Mordrith", "Skulgar", "Nyx",
		"Brakka", "Hollowmaw", "Ruinfang", "Grimtooth", "Sablethorn" };
	    snprintf(m->base, sizeof m->base, "%s the %s",
		    names[(int) ROLL(0.0, 10.0) % 10], Do_strip_article(o->realName));
	}
	Do_nemesis_name(slot, name);
	sprintf(msg, "%s has slain %s and become a nemesis of the realm! It roams %s with a bounty of %.0f gold.",
		name, c->modifiedName, regionNames[r], m->bounty);
	Do_world_news_locked(msg, TRUE);
    }
    Do_world_save_locked();
    pthread_mutex_unlock(&world_lock);
    Do_world_flush(c);
    c->nemesisFight = -1;
    (void) grew;
}

	/* gold spent at a shop helps the local town */
void Do_world_spend(struct client_t *c, double gold)
{
    int r = Do_player_region(c);

    if (r == HEARTLANDS || gold <= 0) return;
    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    W.region[r].prosperity = MIN(100.0, W.region[r].prosperity +
	    MIN(5.0, gold / (2000.0 + 100.0 * c->player.circle)));
    pthread_mutex_unlock(&world_lock);
}

/************************************************************************
/ shaping encounters
*************************************************************************/

	/* after the monster type is rolled: a nemesis, a rare creature, or a
	   neighbour of a species that has been hunted out */
int Do_world_pick_monster(struct client_t *c, int type)
{
    int r = Do_player_region(c), i, t, season = Do_world_season(), night = Do_world_is_night();
    double chance;

    c->nemesisFight = -1;
    c->rareFight = 0;
    c->emboldened = FALSE;
    if (c->wizard >= 3) return type;

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();

	/* a nemesis prowls here */
    for (i = 0; i < MAX_NEMESES; i++) {
	struct nemesis_t *m = &W.nemesis[i];
	if (!m->active || (m->region != r && !forceNemesis)) continue;
	chance = forceNemesis ? 1.0 : strcmp(m->victim, c->player.lcname) == 0 ? 0.25 : 0.06;
	forceNemesis = FALSE;
	if (RND() < chance) {
	    c->nemesisFight = i;
	    t = m->type;
	    pthread_mutex_unlock(&world_lock);
	    return t;
	}
    }

	/* rare creatures, only when the conditions are right */
    if (r != HEARTLANDS) {
	if (W.region[r].shadow >= BLIGHTED && night && RND() < 0.02) c->rareFight = 1;
	else if (W.region[r].shadow < LIBERATED && !night && RND() < 0.015) c->rareFight = 2;
    }
    if (!c->rareFight && season == 3 && c->player.circle >= 10 && RND() < 0.015) c->rareFight = 3;
    if (c->rareFight) {
	pthread_mutex_unlock(&world_lock);
	return type;
    }

	/* hunted out: a neighbour takes its place, and grows bold */
    if (type >= 0 && type < NUM_MONSTERS && c->realm->monster[type].special_type == SM_NONE &&
	    W.abundance[type] < 0.4 && RND() > W.abundance[type]) {
	for (i = 1; i <= 5; i++) {
	    t = type + (RND() < 0.5 ? i : -i);
	    if (t >= 14 && t < NUM_MONSTERS && c->realm->monster[t].special_type == SM_NONE &&
		    W.abundance[t] >= 0.4) {
		c->emboldened = TRUE;
		type = t;
		break;
	    }
	}
    }
    pthread_mutex_unlock(&world_lock);
    return type;
}

int Do_world_special_encounter(struct client_t *c)
{
    return c->nemesisFight >= 0 || c->rareFight;
}

double Do_world_nemesis_size(struct client_t *c, double size)
{
    double s = size;

    if (c->nemesisFight < 0) return size;
    pthread_mutex_lock(&world_lock);
    if (W.nemesis[c->nemesisFight].active) s = MAX(size, W.nemesis[c->nemesisFight].size);
    pthread_mutex_unlock(&world_lock);
    return s;
}

	/* after the monster's stats are set; returns TRUE if it must not
	   also become a champion */
int Do_world_shape_monster(struct client_t *c)
{
    struct opponent_t *o = c->battle.opponent;
    int r = Do_player_region(c), season = Do_world_season();
    double str = 1, xp = 1;
    char msg[SZ_LINE], name[SZ_LINE];
    int special = FALSE;

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    if (r == HEARTLANDS) {
	if (time(NULL) < W.overrunUntil) { str *= 1.3; xp *= 1.3; }
    }
    else if (W.region[r].shadow >= BLIGHTED) { str *= 1.2; xp *= 1.25; }
    if (Do_world_is_night()) { str *= 1.15; xp *= 1.15; }
    if (season == 3) str *= 1.1;
    if (season == 2) xp *= 1.1;
    if (Do_project_done(4)) xp *= 1.05;

    if (c->nemesisFight >= 0 && W.nemesis[c->nemesisFight].active) {
	struct nemesis_t *m = &W.nemesis[c->nemesisFight];
	Do_nemesis_name(c->nemesisFight, name);
	snprintf(o->name, SZ_MONSTER_NAME, "%s", name);
	str *= 1.5 + 0.3 * m->kills;
	o->energy = o->max_energy = floor(o->max_energy * (2.0 + 0.5 * m->kills));
	xp *= 5.0 + m->kills;
	o->treasure_type = MIN(13, MAX(o->treasure_type, 3) + 3);
	o->flock_percent = 0;
	special = TRUE;
	sprintf(msg, "%s stalks %s near %s! A bounty of %.0f gold rides on its head.\n",
		name, c->player.name, c->player.area, m->bounty);
    }
    else if (c->rareFight) {
	static const char *rare[] = { "", "A Shadow Wyrm", "The Golden Stag", "A Frost Giant" };
	snprintf(o->name, SZ_MONSTER_NAME, "%s", rare[c->rareFight]);
	snprintf(o->realName, SZ_MONSTER_NAME, "%s", rare[c->rareFight]);
	if (c->rareFight == 1) { str *= 2.5; o->energy = o->max_energy = floor(o->max_energy * 4); xp *= 8; o->treasure_type = MIN(13, o->treasure_type + 3); }
	if (c->rareFight == 2) { str *= 0.3; o->speed = o->max_speed = o->max_speed * 2; xp *= 3; o->treasure_type = 13; }
	if (c->rareFight == 3) { str *= 3; o->energy = o->max_energy = floor(o->max_energy * 5); xp *= 10; o->treasure_type = MIN(13, o->treasure_type + 2); }
	o->flock_percent = 0;
	special = TRUE;
	sprintf(msg, "%s, a rare creature, has been sighted near %s!\n", o->name, c->player.area);
    }
    pthread_mutex_unlock(&world_lock);

    if (c->emboldened && !special) {
	str *= 1.15; xp *= 1.15;
	sprintf(name, "With its prey hunted out, %s grows bold!\n", o->name);
	Do_send_line(c, name);
    }

    o->strength *= str;
    o->max_strength *= str;
    if (str > 1) o->energy = o->max_energy = floor(o->max_energy * str);
    o->experience *= xp;

    if (special) {
	Do_broadcast(c, msg);
    }
    return special;
}

/************************************************************************
/ prices, rewards and the rest
*************************************************************************/

double Do_world_monster_chance_factor(struct client_t *c)
{
    int r = Do_player_region(c);
    double f = Do_world_is_night() ? 1.2 : 1.0;

    if (r != HEARTLANDS) {
	if (W.region[r].shadow >= BLIGHTED) f *= 1.25;
	else if (W.region[r].shadow < LIBERATED) f *= 0.8;
    }
    return f;
}

double Do_world_gold_factor(struct client_t *c)
{
    int r = Do_player_region(c);
    double f = Do_world_season() == 1 ? 1.1 : 1.0;

    if (r != HEARTLANDS && W.region[r].shadow >= BLIGHTED) f *= 1.25;
    return f;
}

double Do_world_gear_factor(struct client_t *c)
{
    return (Do_world_season() == 3 ? 1.25 : 1.0) * (Do_project_done(3) ? 1.1 : 1.0);
}

double Do_world_spell_factor(void)
{
    return Do_project_done(2) ? 0.9 : 1.0;
}

double Do_world_price_factor(struct client_t *c)
{
    int r = Do_player_region(c);
    double f = Do_project_done(1) ? 0.95 : 1.0;
    const char *tier;

    if (r == HEARTLANDS) return f;
    pthread_mutex_lock(&world_lock);
    tier = Do_town_tier(r);
    if (!strcmp(tier, "Struggling")) f *= 1.1;
    else if (!strcmp(tier, "Thriving")) f *= 0.95;
    else if (!strcmp(tier, "Flourishing")) f *= 0.9;
    if (W.region[r].shadow < LIBERATED) f *= 0.9;
    pthread_mutex_unlock(&world_lock);
    return f;
}

	/* at a trading post: TRUE if its town has fallen (no trade) */
int Do_world_trading_post(struct client_t *c)
{
    int r = Do_player_region(c), fallen = FALSE, flourishing = FALSE;
    char line[SZ_LINE];

    if (r == HEARTLANDS) return FALSE;
    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    fallen = W.region[r].fallen;
    flourishing = !fallen && W.region[r].prosperity >= 85;
    pthread_mutex_unlock(&world_lock);

    if (fallen) {
	sprintf(line, "The trading post of %s lies in ruins, overrun by the Shadow. Drive the Shadow back to retake the town.\n",
		townNames[r]);
	Do_send_line(c, line);
	Do_more(c);
	Do_send_clear(c);
	return TRUE;
    }
    if (flourishing && c->player.energy < c->player.max_energy + c->player.shield) {
	Do_energy(c, c->player.max_energy + c->player.shield, c->player.max_energy,
		c->player.shield, c->battle.force_field, FALSE);
	sprintf(line, "The inn of flourishing %s feeds you and dresses your wounds.\n", townNames[r]);
	Do_send_line(c, line);
    }
    return FALSE;
}

	/* trading post: give gold to the realm project */
void Do_world_donate(struct client_t *c)
{
    char line[SZ_LINE], msg[SZ_LINE];
    double gold;
    int i, done = FALSE;

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    if (W.project >= NUM_PROJECTS) {
	pthread_mutex_unlock(&world_lock);
	Do_send_line(c, "Every realm project is complete. The realm thanks you.\n");
	Do_more(c);
	return;
    }
    sprintf(line, "Realm project: %s (when done, %s).\n", projects[W.project].name,
	    projects[W.project].effect);
    Do_send_line(c, line);
    sprintf(line, "Gold: %.0f of %.0f.  Monsters slain: %ld of %ld.\n", W.projectGold,
	    projects[W.project].gold, W.projectKills, projects[W.project].kills);
    pthread_mutex_unlock(&world_lock);
    Do_send_line(c, line);

    if (Do_double_dialog(c, &gold, "How much gold will you give?\n") || gold < 1) {
	return;
    }
    gold = floor(gold);
    if (gold > c->player.gold) {
	Do_send_line(c, "You don't have that much gold.\n");
	Do_more(c);
	return;
    }
    Do_gold(c, -gold, FALSE);

    pthread_mutex_lock(&world_lock);
    if (W.project < NUM_PROJECTS) {
	W.projectGold += gold;
	for (i = 0; i < MAX_DONORS && W.donors[i].name[0] &&
		strcmp(W.donors[i].name, c->modifiedName); i++) ;
	if (i < MAX_DONORS) {
	    if (!W.donors[i].name[0]) strncpy(W.donors[i].name, c->modifiedName, SZ_NAME - 1);
	    W.donors[i].gold += gold;
	}
	else if (gold > W.donors[MAX_DONORS - 1].gold) {
	    strncpy(W.donors[MAX_DONORS - 1].name, c->modifiedName, SZ_NAME - 1);
	    W.donors[MAX_DONORS - 1].gold = gold;
	}
	{	/* keep the list ordered by gold */
	    int a, b;
	    for (a = 0; a < MAX_DONORS; a++) for (b = a + 1; b < MAX_DONORS; b++)
		if (W.donors[b].gold > W.donors[a].gold) { struct donor_t t = W.donors[a]; W.donors[a] = W.donors[b]; W.donors[b] = t; }
	}
	if (W.projectGold >= projects[W.project].gold && W.projectKills >= projects[W.project].kills) {
	    sprintf(msg, "The realm has completed %s! From now on, %s.",
		    projects[W.project].name, projects[W.project].effect);
	    Do_world_news_locked(msg, TRUE);
	    W.project++; W.projectGold = 0; W.projectKills = 0;
	    memset(W.donors, 0, sizeof W.donors);
	    done = TRUE;
	}
	Do_world_save_locked();
    }
    pthread_mutex_unlock(&world_lock);

    sprintf(line, "You give %.0f gold to the realm. Your name will be remembered.\n", gold);
    Do_send_line(c, line);
    Do_more(c);
    Do_world_flush(c);
    (void) done;
}

/************************************************************************
/ /realm in chat
*************************************************************************/

void Do_world_chat(struct client_t *c)
{
    char line[SZ_LINE * 2], name[SZ_LINE], cap[64];
    int i, any;

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    sprintf(line, "It is %s, in the season of %s (%s).\n", Do_world_is_night() ? "night" : "day",
	    seasonNames[Do_world_season()], seasonEffects[Do_world_season()]);
    Do_send_chat_self(c, line);
    for (i = 0; i < NUM_REGIONS; i++) {
	sprintf(line, "  %s: %s, %.0f%% Shadow. %s is %s.\n", Cap(regionNames[i], cap),
		Do_region_state(i), W.region[i].shadow, townNames[i], Do_town_tier(i));
	Do_send_chat_self(c, line);
    }
    if (Do_player_region(c) != HEARTLANDS) {
	double px, py;
	Do_nearest_post(Do_player_region(c), c->player.x, c->player.y, &px, &py);
	sprintf(line, "The nearest trading post of %s is at %.0f, %.0f.\n",
		townNames[Do_player_region(c)], px, py);
	Do_send_chat_self(c, line);
    }
    if (time(NULL) < W.overrunUntil) Do_send_chat_self(c, "The Lord's Chamber is overrun by monsters!\n");
    if (W.project < NUM_PROJECTS) {
	sprintf(line, "Realm project: %s - gold %.0f/%.0f, monsters %ld/%ld (then %s).\n",
		projects[W.project].name, W.projectGold, projects[W.project].gold,
		W.projectKills, projects[W.project].kills, projects[W.project].effect);
	Do_send_chat_self(c, line);
    }
    any = FALSE;
    for (i = 0; i < MAX_NEMESES; i++) {
	if (!W.nemesis[i].active) continue;
	if (!any) Do_send_chat_self(c, "Nemeses at large:\n");
	any = TRUE;
	Do_nemesis_name(i, name);
	sprintf(line, "  %s - %d slain, bounty %.0f gold, roams %s.\n", name,
		W.nemesis[i].kills, W.nemesis[i].bounty, regionNames[W.nemesis[i].region]);
	Do_send_chat_self(c, line);
    }
    pthread_mutex_unlock(&world_lock);
}

#ifdef GEAR_TEST
	/* test builds: /worldtest blight|liberate|night|siege|nemesis */
void Do_world_test(struct client_t *c, char *what)
{
    int r = Do_player_region(c);

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    if (!strncmp(what, "blight", 6) && r != HEARTLANDS) W.region[r].shadow = 99;
    else if (!strncmp(what, "liberate", 8) && r != HEARTLANDS) W.region[r].shadow = 26;
    else if (!strncmp(what, "project", 7) && W.project < NUM_PROJECTS) {
	W.projectGold = projects[W.project].gold;
	W.projectKills = projects[W.project].kills - 1;
    }
    else if (!strncmp(what, "nemesis", 7)) {
	int i;
	for (i = 0; i < MAX_NEMESES; i++) if (W.nemesis[i].active) W.nemesis[i].region = r == HEARTLANDS ? 0 : r;
    }
    else if (!strncmp(what, "tick", 4)) W.lastTick -= 3600;
    Do_world_save_locked();
    pthread_mutex_unlock(&world_lock);
    if (!strncmp(what, "meet", 4)) forceNemesis = TRUE;
    if (!strncmp(what, "tough", 5)) {
	c->player.max_energy = 200000;
	Do_energy(c, 200000, 200000, c->player.shield, 0, TRUE);
    }
    if (!strncmp(what, "posteast", 8)) Do_location(c, 900, 0, TRUE);	/* an axis post */
    else if (!strncmp(what, "gotonorth", 9)) Do_location(c, 60, 900, TRUE);	/* the Northern Reach */
    else if (!strncmp(what, "goto", 4)) Do_location(c, 900, 120, TRUE);	/* the Eastern Marches */
    if (!strncmp(what, "doom", 4)) {
	c->forceChampion = TRUE;
	Do_energy(c, 1, c->player.max_energy, c->player.shield, 0, FALSE);
    }
    if (!strncmp(what, "siege", 5)) {
	Do_world_start_siege(c, 1);
	pthread_mutex_lock(&world_lock);
	W.army = 3;
	pthread_mutex_unlock(&world_lock);
    }
    Do_send_chat_self(c, "World test done.\n");
}
#endif
