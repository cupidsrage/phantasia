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

	/* the War of the Seasons: each full rotation of the four seasons (4
	   weeks) the realm must build all its projects; who helped, and the
	   bonus the winners carry through the next war */
#define MAX_WAR		300
#define WAR_DAYS	28
struct warpart_t { char acct[SZ_NAME], who[SZ_NAME]; double gold; long kills; };
struct warbonus_t { char acct[SZ_NAME]; double pct; };

static struct {
    time_t lastTick, nextSiege, overrunUntil, lastRoam;
    int callRegion;			/* the Call to Arms, or -1 */
    time_t callUntil;
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
    long war;				/* which war this is */
    time_t warStart;			/* when it began here (the first may be short) */
    int lastWar;			/* the war before: 1 won, 0 lost, -1 none */
    struct warpart_t part[MAX_WAR];
    int numPart;
    struct warbonus_t bonus[MAX_WAR];	/* from the last war won */
    int numBonus;
    char bannerAcct[SZ_NAME], bannerWho[SZ_NAME];	/* the last won war's greatest helper */
    time_t musterUntil, musterLast;	/* the Banner's Muster: running until, last called */
    char dragon[SZ_NAME];		/* who rides the one Dragon Hatchling */
} W;

static pthread_mutex_t world_lock = PTHREAD_MUTEX_INITIALIZER;

static time_t Do_war_ends(void);
static time_t siegeOpenedAt = 0;	/* not saved: after a restart, 0 */
static double Do_war_share_locked(int i);
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
 * Trading posts stand along the eight compass roads out from 0,0.  On the
 * diagonals they are where they always were: at (100 n*n, 100 n*n) and its
 * mirrors.  On the four main roads they stand at the same distance from
 * the throne as their diagonal partners (100 n*n times the square root of
 * 2, rounded), so every post is in the same circle, among the same
 * monsters, as before.  Returns n (the post's rank, 1 = nearest the
 * throne), or 0 if (x, y) is not a post.  *diagonal says which road.
 */
static double Do_axis_post_distance(int n)
{
    return floor(100.0 * n * n * M_SQRT2 + 0.5);
}

int Do_trading_post_rank(double x, double y, int *diagonal)
{
    double ax = fabs(x), ay = fabs(y), d, n;
    int rank;

    if (diagonal) *diagonal = FALSE;
    if (ax == 0 && ay == 0) return 0;
    if (ax == ay) {				/* a diagonal road */
	n = sqrt(ax / 100.0);
	if (floor(n) != n) return 0;
	if (diagonal) *diagonal = TRUE;
	return (int) n;
    }
    if (ax != 0 && ay != 0) return 0;
    d = ax + ay;				/* a main road */
    rank = (int) floor(sqrt(d / (100.0 * M_SQRT2)) + 0.5);
    return rank >= 1 && Do_axis_post_distance(rank) == d ? rank : 0;
}

/* the post nearest (x, y) on its region's road */
static void Do_nearest_post(int r, double x, double y, double *px, double *py)
{
    static const int dx[NUM_REGIONS] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    static const int dy[NUM_REGIONS] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    double t = sqrt(x * x + y * y);		/* distance from the throne */
    int n = (int) MAX(1.0, floor(sqrt(t / (100.0 * M_SQRT2)) + 0.5));

    if (dx[r] && dy[r]) {			/* a diagonal road */
	*px = dx[r] * 100.0 * n * n;
	*py = dy[r] * 100.0 * n * n;
    }
    else {					/* a main road: same distance */
	*px = dx[r] * Do_axis_post_distance(n);
	*py = dy[r] * Do_axis_post_distance(n);
    }
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

	/* experience and gold: +1% for every 2% of Shadow past 25% */
static double Do_shadow_reward(double shadow)
{
    return 1.0 + MAX(0.0, shadow - LIBERATED) / 200.0;
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
    W.callRegion = -1;
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

	if (!strcmp(word, "call")) {
	    long until;
	    if (sscanf(rest, "%d %ld", &W.callRegion, &until) == 2) W.callUntil = until;
	    if (W.callRegion < -1 || W.callRegion >= NUM_REGIONS) W.callRegion = -1;
	}
	else if (!strcmp(word, "state")) {
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
	else if (!strcmp(word, "war")) {
	    long ws = 0;
	    sscanf(rest, "%ld %d %ld", &W.war, &W.lastWar, &ws);
	    W.warStart = ws;
	}
	else if (!strcmp(word, "warpart") && W.numPart < MAX_WAR) {
	    struct warpart_t *p = &W.part[W.numPart];
	    if (sscanf(rest, "%32s %32s %lf %ld", p->acct, p->who, &p->gold, &p->kills) == 4) ++W.numPart;
	}
	else if (!strcmp(word, "muster")) {
	    long u = 0, l = 0;
	    sscanf(rest, "%ld %ld", &u, &l);
	    W.musterUntil = u; W.musterLast = l;
	}
	else if (!strcmp(word, "dragon")) {
	    sscanf(rest, "%32s", W.dragon);
	    if (!strcmp(W.dragon, "-")) W.dragon[0] = '\0';
	}
	else if (!strcmp(word, "banner")) {
	    sscanf(rest, "%32s %32s", W.bannerAcct, W.bannerWho);
	    if (!strcmp(W.bannerAcct, "-")) W.bannerAcct[0] = W.bannerWho[0] = '\0';
	}
	else if (!strcmp(word, "warbonus") && W.numBonus < MAX_WAR) {
	    struct warbonus_t *b = &W.bonus[W.numBonus];
	    if (sscanf(rest, "%32s %lf", b->acct, &b->pct) == 2) ++W.numBonus;
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
	fprintf(f, "call %d %ld\n", W.callRegion, (long) W.callUntil);
	fprintf(f, "war %ld %d %ld\n", W.war, W.lastWar, (long) W.warStart);
	fprintf(f, "muster %ld %ld\n", (long) W.musterUntil, (long) W.musterLast);
	fprintf(f, "dragon %s\n", W.dragon[0] ? W.dragon : "-");
	fprintf(f, "banner %s %s\n", W.bannerAcct[0] ? W.bannerAcct : "-", W.bannerWho[0] ? W.bannerWho : "-");
	for (i = 0; i < W.numPart; i++) {
	    fprintf(f, "warpart %s %s %.0f %ld\n", W.part[i].acct, W.part[i].who,
		    W.part[i].gold, W.part[i].kills);
	}
	for (i = 0; i < W.numBonus; i++) {
	    fprintf(f, "warbonus %s %.3f\n", W.bonus[i].acct, W.bonus[i].pct);
	}
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
    fprintf(f, "],\"callToArms\":");
    if (W.callRegion >= 0) {
	char cap2[64];
	fprintf(f, "{\"region\":");
	Do_json_str(f, Cap(regionNames[W.callRegion], cap2));
	fprintf(f, ",\"shadow\":%.0f,\"until\":%ld}", W.region[W.callRegion].shadow, (long) W.callUntil);
    }
    else fprintf(f, "null");
    fprintf(f, ",\"siege\":{\"active\":%s,\"army\":%.0f,\"armyMax\":%.0f,\"lastWon\":%s,\"fought\":%s},",
	    W.siegeId > W.lastSiegeId ? "true" : "false", W.army, W.armyMax,
	    W.lastSiegeWon ? "true" : "false", W.lastSiegeId > 0 ? "true" : "false");
	/* the War of the Seasons, with its five biggest helpers */
    {
	int order[MAX_WAR], k, m, n = W.numPart;
	for (k = 0; k < n; k++) order[k] = k;
	for (k = 0; k < n; k++) for (m = k + 1; m < n; m++)
	    if (Do_war_share_locked(order[m]) > Do_war_share_locked(order[k])) { int t = order[k]; order[k] = order[m]; order[m] = t; }
	fprintf(f, "\"war\":{\"index\":%ld,\"endsAt\":%ld,\"lastWar\":%d,\"participants\":%d,\"banner\":",
		W.war, (long) Do_war_ends(), W.lastWar, n);
	Do_json_str(f, W.bannerWho);
	fprintf(f, ",\"musterUntil\":%ld,\"top\":[", (long) W.musterUntil);
	for (k = 0; k < n && k < 5; k++) {
	    fprintf(f, "%s{\"name\":", k ? "," : "");
	    Do_json_str(f, W.part[order[k]].who);
	    fprintf(f, ",\"share\":%.3f}", Do_war_share_locked(order[k]));
	}
	fprintf(f, "]},");
    }
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

static long Do_war_index(void)
{
    return ((long) (time(NULL) / 86400) + 3) / WAR_DAYS;	/* starts with Bloom, on a Monday */
}

static time_t Do_war_ends(void)
{
    return (time_t) ((Do_war_index() + 1) * WAR_DAYS - 3) * 86400;
}

static void Do_world_ensure_locked(void)
{
    if (!worldLoaded) {
	Do_world_load_locked();
	if (W.war == 0) { W.war = Do_war_index(); W.lastWar = -1; W.warStart = time(NULL); }
    }
}

	/* someone helped this war's projects: gold given, or a monster slain */
static void Do_war_help_locked(struct client_t *c, double gold, long kills)
{
    int i;

    if (!c->lcaccount[0] || W.project >= NUM_PROJECTS) return;
    for (i = 0; i < W.numPart && strcmp(W.part[i].acct, c->lcaccount); i++) ;
    if (i == W.numPart) {
	if (W.numPart >= MAX_WAR) return;
	memset(&W.part[i], 0, sizeof W.part[i]);
	snprintf(W.part[i].acct, SZ_NAME, "%s", c->lcaccount);
	++W.numPart;
    }
    snprintf(W.part[i].who, SZ_NAME, "%s", c->player.lcname[0] ? c->player.lcname : "-");
    W.part[i].gold += gold;
    W.part[i].kills += kills;
}

	/* a share of the war's effort: half the gold given, half the kills */
static double Do_war_share_locked(int i)
{
    double g = 0, k = 0;
    int j;

    for (j = 0; j < W.numPart; j++) { g += W.part[j].gold; k += W.part[j].kills; }
    if (g <= 0 && k <= 0) return 0;
    if (g <= 0) return W.part[i].kills / k;
    if (k <= 0) return W.part[i].gold / g;
    return 0.5 * W.part[i].gold / g + 0.5 * W.part[i].kills / k;
}

	/* the bonus a share earns if the war is won: +2% for any help, up
	   to +10% for the one who did the most */
static double Do_war_bonus_for_locked(int i)
{
    double top = 0, sh = Do_war_share_locked(i);
    int j;

    for (j = 0; j < W.numPart; j++) top = MAX(top, Do_war_share_locked(j));
    return top > 0 ? 2.0 + 8.0 * sh / top : 0;
}

	/* the four seasons have turned: was the war won? */
static void Do_war_end_locked(void)
{
    char msg[SZ_LINE];
    int i, won = W.project >= NUM_PROJECTS;
	/* a war that began part way through the seasons (the first one) is
	   too short to lose: it can be won, but losing it costs nothing */
    int shortWar = W.warStart > 0 && Do_war_ends() - W.warStart < 21 * 86400 &&
	    time(NULL) - W.warStart < 21 * 86400;

    W.numBonus = 0;
    W.bannerAcct[0] = W.bannerWho[0] = '\0';	/* the Banner flies only over a won war */
    if (won) {
	int top = -1;
	for (i = 0; i < W.numPart; i++) {
	    if (top < 0 || Do_war_share_locked(i) > Do_war_share_locked(top)) top = i;
	}
	if (top >= 0) {
	    snprintf(W.bannerAcct, SZ_NAME, "%s", W.part[top].acct);
	    snprintf(W.bannerWho, SZ_NAME, "%s", W.part[top].who);
	    sprintf(msg, "%c%s did the most to win the war and carries the Banner of the Seasons until the next one ends!",
		    toupper((unsigned char) W.bannerWho[0]), W.bannerWho + 1);
	    Do_world_news_locked(msg, TRUE);
	}
	for (i = 0; i < W.numPart && W.numBonus < MAX_WAR; i++) {
	    snprintf(W.bonus[W.numBonus].acct, SZ_NAME, "%s", W.part[i].acct);
	    W.bonus[W.numBonus].pct = Do_war_bonus_for_locked(i);
	    ++W.numBonus;
	}
	sprintf(msg, "The War of the Seasons is won! Every realm project stands. %d adventurer%s a bonus of up to 10%% more experience and gold through the next war.",
		W.numPart, W.numPart == 1 ? " carries" : "s carry");
    }
    else if (shortWar) {
	sprintf(msg, "The short first War of the Seasons ends with %d of %d realm projects built. No harm done: the real war starts now.",
		W.project, NUM_PROJECTS);
    }
    else {
	sprintf(msg, "The War of the Seasons is lost: only %d of %d realm projects were built. For the next war monsters are 10%% stronger, the Shadow spreads faster and shops cost more.",
		W.project, NUM_PROJECTS);
    }
    Do_world_news_locked(msg, TRUE);

	/* a new war: the projects start over */
    W.lastWar = won ? 1 : (shortWar ? -1 : 0);
    W.war = Do_war_index();
    W.warStart = time(NULL);
    W.numPart = 0;
    W.project = 0; W.projectGold = 0; W.projectKills = 0;
    memset(W.donors, 0, sizeof W.donors);
    sprintf(msg, "A new War of the Seasons begins: build all %d realm projects before the seasons turn again!", NUM_PROJECTS);
    Do_world_news_locked(msg, TRUE);
}

	/* the last war's outcome, for this one */
static int Do_war_lost_locked(void) { return W.lastWar == 0; }

void Do_world_banner_holder(char *out)
{
    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    snprintf(out, SZ_NAME, "%s", W.bannerAcct);
    pthread_mutex_unlock(&world_lock);
}

	/* the one Dragon Hatchling: who rides it ("" when it's free) */
void Do_world_dragon_holder(char *out)
{
    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    snprintf(out, SZ_NAME, "%s", W.dragon);
    pthread_mutex_unlock(&world_lock);
}

void Do_world_set_dragon(const char *who)
{
    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    snprintf(W.dragon, SZ_NAME, "%s", who);
    Do_world_save_locked();
    pthread_mutex_unlock(&world_lock);
}

double Do_world_war_factor(struct client_t *c)
{
    double f = Do_gear_ability(c, AB_CONQUEROR) > 0 ? 1.1 : 1.0;	/* the Banner */
    int i;

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    for (i = 0; i < W.numBonus; i++) {
	if (!strcmp(W.bonus[i].acct, c->lcaccount)) { f *= 1.0 + W.bonus[i].pct / 100.0; break; }
    }
    pthread_mutex_unlock(&world_lock);
    return f;
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
    double growth = SHADOW_PER_MINUTE * (Do_project_done(0) ? 0.75 : 1.0) *
	    (Do_war_lost_locked() ? 1.25 : 1.0);	/* a lost war: the Shadow spreads faster */
    char msg[SZ_LINE], cap[64];

    if (Do_war_index() != W.war) Do_war_end_locked();

	/* the Muster's hour is over */
    {
	static time_t endedFor = 0;
	if (W.musterUntil && time(NULL) >= W.musterUntil && endedFor != W.musterUntil &&
		time(NULL) - W.musterUntil < 600) {
	    endedFor = W.musterUntil;
	    Do_world_news_locked("The Muster is over. The Banner of the Seasons rests until tomorrow.", TRUE);
	}
    }

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

	/* the Call to Arms: every hour, the region deepest in Shadow (at
	   least Contested) is named, and kills there earn Valor */
    if (time(NULL) >= W.callUntil) {
	int worst = -1;
	for (j = 0; j < NUM_REGIONS; j++) {
	    if (W.region[j].shadow >= LIBERATED &&
		    (worst < 0 || W.region[j].shadow > W.region[worst].shadow)) worst = j;
	}
	W.callUntil = time(NULL) + 3600;
	if (worst != W.callRegion && worst >= 0) {
	    sprintf(msg, "Call to Arms! %s lies deepest in Shadow (%.0f%%). Every monster slain there this hour earns Valor.",
		    Cap(regionNames[worst], cap), W.region[worst].shadow);
	    Do_world_news_locked(msg, TRUE);
	}
	W.callRegion = worst;
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

	/* how hard a monster pushes back a siege: its size times a power for
	   its kind (square root of its experience against a typical
	   monster's: a Moron 0.2, most monsters about 1, the deadliest up to
	   7); champions count triple and nemeses ten times */
static double Do_monster_might(struct client_t *c)
{
    struct opponent_t *o = c->battle.opponent;
    double base = 800, power;

    if (o->type >= 0 && o->type < NUM_SPECIES) base = c->realm->monster[o->type].experience;
    power = MAX(0.2, MIN(7.0, sqrt(MAX(1.0, base) / 800.0)));
    power *= MAX(1.0, o->size);
    if (c->nemesisFight >= 0) power *= 10;
    else if (c->championFight) power *= 3;
    return power;
}

	/* the army is sized to who is online: about 35 kills each at the
	   depth a character of their level usually hunts */
static double Do_siege_army_size(struct client_t *c)
{
    struct game_t *g;
    double total = 0;

    Do_lock_mutex(&c->realm->realm_lock);
    for (g = c->realm->games; g != NULL; g = g->next_game) {
	if (g->description != NULL) {
	    total += 35.0 * (1.0 + 1.2 * sqrt(MAX(0.0, g->description->level)));
	}
    }
    Do_unlock_mutex(&c->realm->realm_lock);
    return floor(MAX(60.0, total));
}

void Do_world_start_siege(struct client_t *c, int players)
{
    char msg[SZ_LINE];
    double hours = Do_env_minutes("PHANTASIA_SIEGE_HOURS", 8);
    double army = Do_siege_army_size(c);

    (void) players;
    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    W.siegeId = W.lastSiegeId + 1;
    siegeOpenedAt = time(NULL);
    W.armyMax = W.army = army;
    W.nextSiege = time(NULL) + (time_t) (hours * 3600 * (0.6 + 0.8 * RND()));
    sprintf(msg, "A monster army %.0f strong marches on the throne!", W.armyMax);
    Do_world_news_locked(msg, FALSE);
    Do_world_save_locked();
    pthread_mutex_unlock(&world_lock);

    Do_world_event_start(c, WE_SIEGE, 45);
    Do_broadcast(c, "*** Every monster slain anywhere pushes the army back - bigger, deadlier monsters far from the throne push hardest. Defend the throne! ***\n");
}

	/* the banner text while a siege is on */
void Do_world_siege_effect(char *out)
{
    double army, max;

    pthread_mutex_lock(&world_lock);
    army = W.army; max = W.armyMax;
    pthread_mutex_unlock(&world_lock);
    sprintf(out, "a monster army marches on the throne: %.0f of its %.0f strength remains. Slay monsters anywhere to push it back; bigger, deadlier monsters push hardest",
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

	/* a siege still open with no siege under way (the server restarted
	   during one, and the world event was forgotten): the army has gone,
	   at no one's cost */
    if (Do_current_world_event(c) != WE_SIEGE) {
	pthread_mutex_lock(&world_lock);
	Do_world_ensure_locked();
	if (W.siegeId > W.lastSiegeId && time(NULL) - siegeOpenedAt > 60) {
	    W.lastSiegeId = W.siegeId;
	    W.army = 0;
	    Do_world_news_locked("The besieging army has melted away into the hills.", FALSE);
	    Do_world_save_locked();
	}
	pthread_mutex_unlock(&world_lock);
    }

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
	    /* paid for your share, in keeping with your level (no more than
	       a character of that level can comfortably carry) */
	    pay = floor(MIN(0.6 * Do_gold_capacity(c),
		    c->siegeKills * (30.0 + 5.0 * c->player.level)));
	    c->player.gold += pay;
	    Do_gold(c, 0, TRUE);
	    sprintf(line, "For defending the throne (%.0f of the army's strength cut down), you are paid %.0f gold.\n",
		    c->siegeKills, pay);
	    Do_send_line(c, line);
	    Do_send_chat_self(c, line);
	    Do_gear_artifact_chance(c, 0.10, "defending the throne");
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
	sprintf(line, "The Heartlands\n%s\n0\n\n\n%d\n%s\n%s\n", Do_region_state(r), night,
		seasonNames[season], W.callRegion >= 0 ? Cap(regionNames[W.callRegion], cap) : "");
    }
    else {
	char call[64] = "";
	if (W.callRegion >= 0) Cap(regionNames[W.callRegion], call);
	sprintf(line, "%s\n%s\n%.0f\n%s\n%s\n%d\n%s\n%s\n", Cap(regionNames[r], cap),
		Do_region_state(r), W.region[r].shadow, townNames[r], Do_town_tier(r),
		night, seasonNames[season], call);
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
    double weight = c->championFight ? 5 : 1, bounty = 0, before, valor = 0;
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
		/* a great deed: a chance of an artifact (offered after the fight) */
	    c->deedChance = MAX(c->deedChance, 0.25);
	    snprintf(c->deedName, sizeof c->deedName, "liberating %s", regionNames[r]);
	}
	if (rg->fallen && rg->shadow < TOWN_RETAKEN) {
	    rg->fallen = FALSE;
	    rg->prosperity = 10;
	    sprintf(msg, "%s has been retaken from the Shadow, thanks to %s!", townNames[r], c->modifiedName);
	    Do_world_news_locked(msg, TRUE);
	}
    }

	/* answering the Call to Arms */
    if (r != HEARTLANDS && r == W.callRegion) {
	valor = floor(25.0 + 12.0 * MAX(1.0, c->player.circle)) * weight;
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

	/* the siege: pushed back by the might of the monster slain */
    if (W.siegeId > W.lastSiegeId && Do_current_world_event(c) == WE_SIEGE) {
	double might = Do_monster_might(c);
	if (c->siegeId != W.siegeId) { c->siegeId = W.siegeId; c->siegeKills = 0; }
	c->siegeKills += might;
	W.army -= might;
	if (W.army <= 0) {
	    Do_world_siege_won_locked(c);
	    sprintf(msg, "%s cut down the last of the besieging army!", c->modifiedName);
	    Do_world_news_locked(msg, FALSE);
	    finished = TRUE;
	}
    }

	/* the realm project */
    if (W.project < NUM_PROJECTS) {
	    /* the Muster: everything counts double */
	long count = time(NULL) < W.musterUntil ? 2 : 1;
	W.projectKills += count;
	Do_war_help_locked(c, 0, count);
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
	c->deedChance = MAX(c->deedChance, 0.25);	/* a great deed */
	snprintf(c->deedName, sizeof c->deedName, "slaying the nemesis %s", name);
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

    if (valor > 0) {
	c->player.gold += valor;
	Do_gold(c, 0, TRUE);
	sprintf(msg, "Valor! For answering the Call to Arms you earn %.0f gold.\n", valor);
	Do_send_line(c, msg);
	Do_deeds_valor(c);
    }
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
    if (Do_war_lost_locked()) str *= 1.1;	/* a lost war: monsters 10% stronger */
    if (r == HEARTLANDS) {
	if (time(NULL) < W.overrunUntil) { str *= 1.3; xp *= 1.3; }
    }
    else {
	    /* the darker the region, the richer every kill */
	xp *= Do_shadow_reward(W.region[r].shadow);
	if (W.region[r].shadow >= BLIGHTED) str *= 1.2;
    }
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
    double f = (Do_world_is_night() && !Do_mount_night_ok(c)) ? 1.2 : 1.0;

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

    if (r != HEARTLANDS) f *= Do_shadow_reward(W.region[r].shadow);
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
    double f = (Do_project_done(1) ? 0.95 : 1.0) * Do_mount_price_factor(c);
    const char *tier;

    pthread_mutex_lock(&world_lock);
    if (Do_war_lost_locked()) f *= 1.1;		/* a lost war: shops cost more */
    if (r == HEARTLANDS) { pthread_mutex_unlock(&world_lock); return f; }
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
	    /* the Muster: everything counts double */
	if (time(NULL) < W.musterUntil) gold *= 2;
	W.projectGold += gold;
	Do_war_help_locked(c, gold, 0);
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
    if (W.callRegion >= 0) {
	sprintf(line, "Call to Arms: %s (%.0f%% Shadow). Kills there earn Valor (bonus gold).\n",
		regionNames[W.callRegion], W.region[W.callRegion].shadow);
	Do_send_chat_self(c, Cap(line, line));
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
    else if (!strncmp(what, "tick", 4)) { W.lastTick -= 3600; W.callUntil = 0; }
    Do_world_save_locked();
    pthread_mutex_unlock(&world_lock);
    if (!strncmp(what, "meet", 4)) forceNemesis = TRUE;
    if (!strncmp(what, "warbuild", 8)) {	/* test builds: finish the projects */
	pthread_mutex_lock(&world_lock);
	Do_world_ensure_locked();
	Do_war_help_locked(c, 1000, 10);
	W.project = NUM_PROJECTS;
	pthread_mutex_unlock(&world_lock);
    }
    if (!strncmp(what, "warend", 6)) {	/* test builds: the seasons turn now */
	pthread_mutex_lock(&world_lock);
	Do_world_ensure_locked();
	Do_war_end_locked();
	pthread_mutex_unlock(&world_lock);
	Do_world_flush(c);
    }
    if (!strncmp(what, "pickscroll", 10)) {	/* test builds: a pick-your-monster scroll */
	struct event_t *ev = (struct event_t *) Do_create_event();
	ev->type = TREASURE_EVENT;
	ev->arg1 = c->player.circle;
	ev->arg2 = 5;
	ev->arg3 = 4;
	Do_file_event(c, ev);
    }
    if (!strncmp(what, "level", 5)) {	/* test builds: a few levels */
	Do_experience(c, 20000.0, FALSE);
    }
    if (!strncmp(what, "rich", 4)) {	/* test builds: gold and gems to spend */
	Do_gold(c, 2500.0 + 500.0 * c->player.level, FALSE);
	Do_gems(c, 3.0, FALSE);
    }
    if (!strncmp(what, "tough", 5)) {
	c->player.max_energy = 200000;
	Do_energy(c, 200000, 200000, c->player.shield, 0, TRUE);
    }
    if (!strncmp(what, "postnear", 8)) Do_location(c, 140, 0, TRUE);	/* by the rank 1 east post */
    else if (!strncmp(what, "posteast", 8)) Do_location(c, 1273, 0, TRUE);	/* rank 3, main road */
    else if (!strncmp(what, "gotonorth", 9)) Do_location(c, 60, 900, TRUE);	/* the Northern Reach */
    else if (!strncmp(what, "goto", 4)) Do_location(c, 900, 120, TRUE);	/* the Eastern Marches */
    if (!strncmp(what, "doom", 4)) {
	c->forceChampion = TRUE;
	Do_energy(c, 1, c->player.max_energy, c->player.shield, 0, FALSE);
    }
    if (!strncmp(what, "siegefull", 9)) Do_world_start_siege(c, 1);
    else if (!strncmp(what, "siege", 5)) {
	Do_world_start_siege(c, 1);
	pthread_mutex_lock(&world_lock);
	W.army = 3;
	pthread_mutex_unlock(&world_lock);
    }
    Do_send_chat_self(c, "World test done.\n");
}
#endif


/************************************************************************
/
/ FUNCTION NAME: Do_world_war_chat(struct client_t *c)
/
/ FUNCTION: /war in chat: how the War of the Seasons stands
/
/************************************************************************/

void Do_world_war_chat(struct client_t *c)
{
    char line[SZ_LINE];
    double days, mine = 0, bonus = 0, have = 0;
    int i, me = -1;

    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    days = (Do_war_ends() - time(NULL)) / 86400.0;
    sprintf(line, "The War of the Seasons: build all %d realm projects before the seasons turn (%.1f days left). %d of %d built so far.\n",
	    NUM_PROJECTS, MAX(0.0, days), MIN(W.project, NUM_PROJECTS), NUM_PROJECTS);
    Do_send_chat_self(c, line);
    if (W.project < NUM_PROJECTS) {
	sprintf(line, "Now building %s: %.0f of %.0f gold, %ld of %ld monsters slain.\n",
		projects[W.project].name, W.projectGold, projects[W.project].gold,
		W.projectKills, projects[W.project].kills);
	Do_send_chat_self(c, line);
    }
    for (i = 0; i < W.numPart; i++) if (!strcmp(W.part[i].acct, c->lcaccount)) me = i;
    if (me >= 0) {
	mine = Do_war_share_locked(me);
	bonus = Do_war_bonus_for_locked(me);
	sprintf(line, "Your account has given %.0f gold and slain %ld monsters for the projects: %.1f%% of the effort. If the war is won you'll carry +%.1f%% experience and gold through the next one.\n",
		W.part[me].gold, W.part[me].kills, 100 * mine, bonus);
    }
    else {
	sprintf(line, "Your account hasn't helped this war yet: give gold to the realm project at any trading post, or slay monsters.\n");
    }
    Do_send_chat_self(c, line);
    for (i = 0; i < W.numBonus; i++) if (!strcmp(W.bonus[i].acct, c->lcaccount)) have = W.bonus[i].pct;
    if (W.bannerWho[0]) {
	sprintf(line, "%c%s carries the Banner of the Seasons, for doing the most to win the last war.\n",
		toupper((unsigned char) W.bannerWho[0]), W.bannerWho + 1);
	Do_send_chat_self(c, line);
    }
    if (time(NULL) < W.musterUntil) {
	sprintf(line, "The Muster is called: for %ld more minutes the realm project counts double and everyone gains 25%% more experience.\n",
		(long) ((W.musterUntil - time(NULL)) / 60 + 1));
	Do_send_chat_self(c, line);
    }
    if (W.lastWar == 1) {
	sprintf(line, have > 0 ? "The last war was won: you carry +%.1f%% experience and gold until this one ends.\n" :
		"The last war was won (you didn't take part, so you carry no bonus).\n", have);
	Do_send_chat_self(c, line);
    }
    else if (W.lastWar == 0) {
	Do_send_chat_self(c, "The last war was lost: until this one ends, monsters are 10% stronger, the Shadow spreads 25% faster and shops cost 10% more.\n");
    }
    pthread_mutex_unlock(&world_lock);
}


/************************************************************************
/
/ FUNCTION NAME: Do_world_muster(struct client_t *c)
/
/ FUNCTION: /muster: the Banner's holder calls the realm to arms
/
/ DESCRIPTION:
/	Once a day the holder of the Banner of the Seasons may call the
/	Muster: for an hour every monster slain and every coin given counts
/	double toward the realm project (and toward everyone's part in the
/	war), and every adventurer gains a quarter more experience.
/
/************************************************************************/

double Do_world_muster_factor(void)
{
    return time(NULL) < W.musterUntil ? 1.25 : 1.0;
}

void Do_world_muster(struct client_t *c)
{
    char line[SZ_LINE];
    time_t now = time(NULL);
    int ok = FALSE;

    if (Do_gear_ability(c, AB_RALLY) <= 0) {
	Do_send_chat_self(c, "Only the bearer of the Banner of the Seasons can call the Muster.\n");
	return;
    }
    pthread_mutex_lock(&world_lock);
    Do_world_ensure_locked();
    if (now < W.musterUntil) {
	sprintf(line, "The Muster is already under way (%ld minutes left).\n", (long) ((W.musterUntil - now) / 60 + 1));
    }
    else if (now - W.musterLast < 24 * 3600) {
	sprintf(line, "The realm is still recovering from the last Muster. You can call it again in %.1f hours.\n",
		(24 * 3600 - (now - W.musterLast)) / 3600.0);
    }
    else {
	W.musterLast = now;
	W.musterUntil = now + 3600;
	snprintf(line, sizeof line, "%s raises the Banner of the Seasons and calls the Muster! For the next hour every monster slain and every coin given counts double toward the realm project, and everyone gains 25%% more experience.", c->modifiedName);
	Do_world_news_locked(line, TRUE);
	Do_world_save_locked();
	ok = TRUE;
    }
    pthread_mutex_unlock(&world_lock);
    if (ok) Do_world_flush(c);
    else Do_send_chat_self(c, line);
}
