/*
 * gameplay.c - newer gameplay features for Phantasia
 *
 *   World events   timed realm-wide events (Blood Moon, Gold Rush,
 *                  Champion Hunt) announced to every player
 *   Champions      rare, named, tougher versions of ordinary monsters
 *   New monsters   extra monsters loaded from EXTRA_MONSTER_FILE
 *   Daily quests   one quest per character per day, with rewards
 *   Chat commands  /quest, /event and /help typed into chat
 *
 * Settings (environment variables, all optional):
 *   PHANTASIA_EVENT_MINUTES   average minutes between events (default 90)
 *   PHANTASIA_EVENT_LENGTH    minutes an event lasts (default 20)
 */

#include "include.h"

#ifndef CHAMPION_CHANCE
#define CHAMPION_CHANCE		0.03	/* normal chance of a champion */
#endif
#ifndef CHAMPION_HUNT_CHANCE
#define CHAMPION_HUNT_CHANCE	0.15	/* chance during a Champion Hunt */
#endif
#ifndef EXTRA_MONSTER_CHANCE
#define EXTRA_MONSTER_CHANCE	0.12	/* chance of one of the new monsters */
#endif

static const char *championNames[] = {
    "Grishnak", "Morwen", "Ulfang", "Brannoc", "Skarth", "Velka",
    "Ghorbash", "Isolde", "Thrain", "Malgor", "Ysolde", "Drogath",
    "Fenwick", "Kaelith", "Orrin", "Zarnak", "Hespera", "Vorn",
    "Baldric", "Nimra", "Craig", "Ashka", "Torvald", "Seraphine"
};

#define NUM_CHAMPION_NAMES (sizeof(championNames) / sizeof(championNames[0]))


/************************************************************************
/ helpers
*************************************************************************/

static int Do_env_minutes(const char *name, int fallback)
{
    char *value = getenv(name);
    int minutes;

    if (value == NULL || (minutes = atoi(value)) <= 0) {
	return fallback;
    }

    return minutes;
}

	/* a line in the player's chat window, seen only by them */
void Do_send_chat_self(struct client_t *c, char *message)
{
    Do_send_int(c, CHAT_PACKET);
    Do_send_string(c, message);
    Do_send_buffer(c);
}

	/* "A Troll" -> "Troll" */
static const char *Do_strip_article(const char *name)
{
    if (strncmp(name, "A ", 2) == 0) {
	return name + 2;
    }

    if (strncmp(name, "An ", 3) == 0) {
	return name + 3;
    }

    if (strncmp(name, "Some ", 5) == 0) {
	return name + 5;
    }

    return name;
}

static long Do_today(void)
{
    return (long) (time(NULL) / 86400);
}


/************************************************************************
/ world events
*************************************************************************/

const char *Do_world_event_name(int theEvent)
{
    switch (theEvent) {

    case WE_BLOOD_MOON:
	return "Blood Moon";

    case WE_GOLD_RUSH:
	return "Gold Rush";

    case WE_CHAMPION_HUNT:
	return "Champion Hunt";
    }

    return "none";
}

static const char *Do_world_event_effect(int theEvent)
{
    switch (theEvent) {

    case WE_BLOOD_MOON:
	return "monsters roam more often and give double experience";

    case WE_GOLD_RUSH:
	return "treasure is far more common and gold finds are doubled";

    case WE_CHAMPION_HUNT:
	return "champions stalk the realm in great numbers";
    }

    return "";
}

int Do_current_world_event(struct client_t *c)
{
    int theEvent;

    Do_lock_mutex(&c->realm->realm_lock);
    theEvent = c->realm->worldEvent;
    Do_unlock_mutex(&c->realm->realm_lock);

    return theEvent;
}

	/* describe the current event into buffer; returns FALSE if none */
int Do_world_event_status(struct client_t *c, char *buffer)
{
    int theEvent;
    time_t ends;

    Do_lock_mutex(&c->realm->realm_lock);
    theEvent = c->realm->worldEvent;
    ends = c->realm->worldEventEnds;
    Do_unlock_mutex(&c->realm->realm_lock);

    if (theEvent == WE_NONE) {
	strcpy(buffer, "No world event is under way.\n");
	return FALSE;
    }

    long minutes = (long) ((ends - time(NULL) + 59) / 60);

    sprintf(buffer, "World event: %s - %s (%ld more minute%s).\n",
	    Do_world_event_name(theEvent), Do_world_event_effect(theEvent),
	    minutes, minutes == 1 ? "" : "s");

    return TRUE;
}

/*
 * Called on every player turn.  Starts and ends world events; whichever
 * player's turn notices the change announces it to everyone.
 */
void Do_world_event_tick(struct client_t *c)
{
    time_t now = time(NULL);
    int interval = Do_env_minutes("PHANTASIA_EVENT_MINUTES", 90) * 60;
    int length = Do_env_minutes("PHANTASIA_EVENT_LENGTH", 20) * 60;
    int started = WE_NONE, ended = WE_NONE;
    char message[SZ_LINE];

    Do_lock_mutex(&c->realm->realm_lock);

    if (c->realm->nextWorldEvent == 0) {
	c->realm->nextWorldEvent = now + (time_t) (interval * (0.5 + RND()));
    }

    if (c->realm->worldEvent != WE_NONE) {

	if (now >= c->realm->worldEventEnds) {
	    ended = c->realm->worldEvent;
	    c->realm->worldEvent = WE_NONE;
	    c->realm->nextWorldEvent = now +
		    (time_t) (interval * (0.5 + RND()));
	}
    }
    else if (now >= c->realm->nextWorldEvent) {

	started = 1 + (int) ROLL(0.0, (double) WE_COUNT);
	if (started > WE_COUNT) {
	    started = WE_COUNT;
	}
	c->realm->worldEvent = started;
	c->realm->worldEventEnds = now + length;
    }

    Do_unlock_mutex(&c->realm->realm_lock);

    if (started != WE_NONE || ended != WE_NONE) {
	Do_world_event_publish(started, started != WE_NONE ? now + length : 0);
    }

    if (started != WE_NONE) {

	sprintf(message, "*** %s! For the next %d minute%s %s. ***\n",
		Do_world_event_name(started), length / 60,
		length / 60 == 1 ? "" : "s", Do_world_event_effect(started));

	Do_broadcast(c, message);
	Do_log(EVENT_LOG, message);
    }

    if (ended != WE_NONE) {

	sprintf(message, "*** The %s has ended. ***\n",
		Do_world_event_name(ended));

	Do_broadcast(c, message);
    }
}

/*
 * Tell a browser player which world event is on (so the page can change
 * its look), whenever it differs from what they were last told.
 */
void Do_world_event_sync(struct client_t *c)
{
    int theEvent;
    time_t ends;
    char line[SZ_LINE];

    if (!c->isBrowser) {
	return;
    }

    Do_lock_mutex(&c->realm->realm_lock);
    theEvent = c->realm->worldEvent;
    ends = c->realm->worldEventEnds;
    Do_unlock_mutex(&c->realm->realm_lock);

    if (theEvent == c->shownEvent && (theEvent == WE_NONE ||
	    ends == c->shownEventEnds)) {
	return;
    }

    c->shownEvent = theEvent;
    c->shownEventEnds = ends;

    Do_send_int(c, WORLD_EVENT_PACKET);
    sprintf(line, "%d\n%s\n%ld\n%s\n", theEvent,
	    theEvent == WE_NONE ? "" : Do_world_event_name(theEvent),
	    theEvent == WE_NONE ? 0L : (long) MAX(0, ends - time(NULL)),
	    theEvent == WE_NONE ? "" : Do_world_event_effect(theEvent));
    Do_send_string(c, line);
    Do_send_buffer(c);
}

	/* the event, for the website: "<event> <ends> <name>" (0 if none) */
void Do_world_event_publish(int theEvent, time_t ends)
{
    FILE *the_file;

    if ((the_file = fopen(WORLD_EVENT_FILE, "w")) == NULL) {
	return;
    }

    fprintf(the_file, "%d %ld %s\n", theEvent, (long) ends,
	    theEvent == WE_NONE ? "-" : Do_world_event_name(theEvent));
    fclose(the_file);
}

	/* chance of a random monster encounter per turn */
double Do_monster_chance(struct client_t *c)
{
    return Do_current_world_event(c) == WE_BLOOD_MOON ? 0.32 : 0.20;
}

	/* multiplier for chances of finding treasure */
double Do_treasure_chance_factor(struct client_t *c)
{
    return Do_current_world_event(c) == WE_GOLD_RUSH ? 3.0 : 1.0;
}

	/* multiplier for gold found in treasure */
double Do_gold_find_factor(struct client_t *c)
{
    return Do_current_world_event(c) == WE_GOLD_RUSH ? 2.0 : 1.0;
}


/************************************************************************
/ extra monsters
*************************************************************************/

/*
 * EXTRA_MONSTER_FILE has the same layout as the monster file, with two
 * more numbers on each stat line: the lowest and highest circle the
 * monster appears in.
 */
void Do_load_extra_monsters(struct realm_t *the_realm)
{
    FILE *monster_file;
    struct monster_t *m;
    char error_msg[SZ_ERROR_MESSAGE], line[256];
    int i;

    the_realm->numExtraMonsters = 0;

    if ((monster_file = fopen(EXTRA_MONSTER_FILE, "r")) == NULL) {
	return;
    }

    for (i = 0; i < MAX_EXTRA_MONSTERS; i++) {

	m = &the_realm->monster[NUM_MONSTERS + i];

	    /* next non-blank, non-comment line is the name */
	do {
	    if (fgets(line, sizeof(line), monster_file) == NULL) {
		line[0] = '\0';
		break;
	    }
	    line[strcspn(line, "\r\n")] = '\0';
	} while (line[0] == '\0' || line[0] == '#');

	if (line[0] == '\0') {
	    break;
	}

	strncpy(m->name, line, SZ_MONSTER_NAME - 1);
	m->name[SZ_MONSTER_NAME - 1] = '\0';
	Do_truncstring(m->name);

	if (fscanf(monster_file, "%lf %lf %lf %lf %lf %hd %hd %hd %hd %hd\n",
		&m->strength, &m->brains, &m->speed, &m->energy,
		&m->experience, &m->treasure_type, &m->special_type,
		&m->flock_percent, &the_realm->extraMinCircle[i],
		&the_realm->extraMaxCircle[i]) != 10) {

	    sprintf(error_msg,
		    "[0.0.0.0:?] Bad stat line for \"%s\" in %s; extra monsters after it are ignored.\n",
		    m->name, EXTRA_MONSTER_FILE);

	    Do_log_error(error_msg);
	    break;
	}

	the_realm->numExtraMonsters = i + 1;
    }

    fclose(monster_file);

    sprintf(error_msg, "Loaded %d extra monsters.\n",
	    the_realm->numExtraMonsters);
    Do_log(SERVER_LOG, error_msg);
}

	/* maybe swap a random encounter for a new monster; -1 if not */
int Do_pick_extra_monster(struct client_t *c)
{
    int eligible[MAX_EXTRA_MONSTERS];
    int count = 0, i;

    if (c->realm->numExtraMonsters == 0 || RND() >= EXTRA_MONSTER_CHANCE) {
	return -1;
    }

    for (i = 0; i < c->realm->numExtraMonsters; i++) {

	if (c->player.circle >= c->realm->extraMinCircle[i] &&
		c->player.circle <= c->realm->extraMaxCircle[i]) {

	    eligible[count++] = NUM_MONSTERS + i;
	}
    }

    if (count == 0) {
	return -1;
    }

    i = (int) ROLL(0.0, (double) count);
    return eligible[i < count ? i : count - 1];
}


/************************************************************************
/ champions
*************************************************************************/

/*
 * Called once the opponent is set up.  Occasionally turns an ordinary
 * random monster into a named champion and tells the realm.
 */
void Do_maybe_make_champion(struct client_t *c)
{
    struct opponent_t *o = c->battle.opponent;
    double chance;
    char message[SZ_LINE];
    const char *who;

    c->championFight = FALSE;

    if (o->special_type != SM_NONE || o->size < 1) {
	return;
    }

    chance = Do_current_world_event(c) == WE_CHAMPION_HUNT ?
	    CHAMPION_HUNT_CHANCE : CHAMPION_CHANCE;

    if (RND() >= chance) {
	return;
    }

    who = championNames[(int) ROLL(0.0, (double) NUM_CHAMPION_NAMES) %
	    NUM_CHAMPION_NAMES];

    snprintf(o->name, SZ_MONSTER_NAME, "%s the %s", who,
	    Do_strip_article(o->realName));

	/* tougher, wiser and far more rewarding */
    o->strength *= 2.0;
    o->max_strength *= 2.0;
    o->energy = o->max_energy = floor(o->max_energy * 3.0);
    o->brains *= 1.5;
    o->experience *= 5.0;
    o->treasure_type = o->treasure_type < 1 ? 2 : MIN(o->treasure_type + 2, 13);
    o->flock_percent = 0;

    c->championFight = TRUE;

    Do_send_line(c, "A champion of the realm bars your way!\n");

    sprintf(message, "%s, a champion, has challenged %s near %s!\n",
	    o->name, c->player.name, c->player.area);
    Do_broadcast(c, message);
}

	/* the player beat a champion */
void Do_champion_slain(struct client_t *c)
{
    char message[SZ_LINE];
    double gems, gold;

    if (!c->championFight) {
	return;
    }

	/* championFight stays set so the gear drop that follows knows; it is
	   reset when the next monster appears */
    gold = floor(c->battle.opponent->size * 250.0 * (1.0 + RND()));
    gems = floor(1.0 + c->battle.opponent->size / 4.0 + RND() * 3.0);

    sprintf(message, "You claim the champion's hoard: %.0lf gold and %.0lf gem%s!\n",
	    gold, gems, gems == 1 ? "" : "s");
    Do_send_line(c, message);

    Do_gold(c, gold, FALSE);
    Do_gems(c, gems, FALSE);

    sprintf(message, "%s has slain the champion %s!\n", c->player.name,
	    c->battle.opponent->name);
    Do_broadcast(c, message);

    Do_quest_progress(c, QT_CHAMPION, 1);
}


/************************************************************************
/ daily quests
*************************************************************************/

static void Do_quest_new(struct client_t *c)
{
    struct quest_t *q = &c->quest;
    double roll = RND();
    int circle = (int) c->player.circle;

    memset(q, 0, sizeof(*q));
    strncpy(q->lcname, c->player.lcname, SZ_NAME - 1);
    q->day = Do_today();

    if (roll < 0.30) {
	q->type = QT_SLAY;
	q->target = 6 + (int) ROLL(0.0, 7.0);
    }
    else if (roll < 0.55) {
	q->type = QT_SLAY_DEEP;
	q->target = 4 + (int) ROLL(0.0, 5.0);
	q->param = MAX(2, circle + 1 + (int) ROLL(0.0, 3.0));
    }
    else if (roll < 0.75) {
	q->type = QT_REACH;
	q->param = MAX(3, circle + 3 + (int) ROLL(0.0, 4.0));
	q->target = 1;
    }
    else if (roll < 0.92) {
	q->type = QT_TREASURE;
	q->target = 3 + (int) ROLL(0.0, 3.0);
    }
    else {
	q->type = QT_CHAMPION;
	q->target = 1;
    }
}

void Do_quest_describe(struct quest_t *q, char *buffer)
{
    char goal[SZ_LINE];

    switch (q->type) {

    case QT_SLAY:
	sprintf(goal, "Slay %d monsters", q->target);
	break;

    case QT_SLAY_DEEP:
	sprintf(goal, "Slay %d monsters at circle %d or beyond", q->target,
		q->param);
	break;

    case QT_REACH:
	sprintf(goal, "Journey to circle %d", q->param);
	break;

    case QT_TREASURE:
	sprintf(goal, "Discover treasure %d times", q->target);
	break;

    case QT_CHAMPION:
	sprintf(goal, "Defeat a champion");
	break;

    default:
	sprintf(goal, "Rest");
    }

    if (q->done) {
	sprintf(buffer, "Today's quest: %s - complete! A new quest comes tomorrow.\n", goal);
    }
    else if (q->type == QT_REACH) {
	sprintf(buffer, "Today's quest: %s.\n", goal);
    }
    else {
	sprintf(buffer, "Today's quest: %s (%d/%d).\n", goal, q->progress,
		q->target);
    }
}

	/* read this character's quest line from QUEST_FILE */
static int Do_quest_read(struct client_t *c)
{
    FILE *the_file;
    struct quest_t q;
    char line[SZ_LINE];
    int found = FALSE;

    if ((the_file = fopen(QUEST_FILE, "r")) == NULL) {
	return FALSE;
    }

    while (fgets(line, sizeof(line), the_file) != NULL) {

	memset(&q, 0, sizeof(q));

	if (sscanf(line, "%32s %ld %d %d %d %d %d", q.lcname, &q.day, &q.type,
		&q.target, &q.param, &q.progress, &q.done) == 7 &&
		strcmp(q.lcname, c->player.lcname) == 0) {

	    c->quest = q;
	    found = TRUE;
	}
    }

    fclose(the_file);
    return found;
}

	/* rewrite QUEST_FILE with this character's quest, dropping old ones */
static void Do_quest_save(struct client_t *c)
{
    FILE *in_file, *out_file;
    struct quest_t q;
    char line[SZ_LINE], error_msg[SZ_ERROR_MESSAGE];
    long today = Do_today();

    if ((out_file = fopen(TEMP_QUEST_FILE, "w")) == NULL) {

	sprintf(error_msg, "[%s] fopen of %s failed in Do_quest_save: %s\n",
		c->connection_id, TEMP_QUEST_FILE, strerror(errno));
	Do_log_error(error_msg);
	return;
    }

    if ((in_file = fopen(QUEST_FILE, "r")) != NULL) {

	while (fgets(line, sizeof(line), in_file) != NULL) {

	    if (sscanf(line, "%32s %ld %d %d %d %d %d", q.lcname, &q.day,
		    &q.type, &q.target, &q.param, &q.progress, &q.done) == 7 &&
		    strcmp(q.lcname, c->quest.lcname) != 0 &&
		    q.day >= today - 1) {

		fputs(line, out_file);
	    }
	}

	fclose(in_file);
    }

    fprintf(out_file, "%s %ld %d %d %d %d %d\n", c->quest.lcname,
	    c->quest.day, c->quest.type, c->quest.target, c->quest.param,
	    c->quest.progress, c->quest.done);

    fclose(out_file);
    rename(TEMP_QUEST_FILE, QUEST_FILE);
}

	/* make sure c->quest is today's quest for the loaded character */
static void Do_quest_ensure(struct client_t *c)
{
    if (!c->characterLoaded) {
	return;
    }

    if (c->questLoaded && c->quest.day == Do_today() &&
	    strcmp(c->quest.lcname, c->player.lcname) == 0) {
	return;
    }

    Do_lock_mutex(&c->realm->quest_lock);

    if (!Do_quest_read(c) || c->quest.day != Do_today()) {
	Do_quest_new(c);
	Do_quest_save(c);
    }

    Do_unlock_mutex(&c->realm->quest_lock);

    c->questLoaded = TRUE;
}

static void Do_quest_complete(struct client_t *c)
{
    struct quest_t *q = &c->quest;
    char message[SZ_LINE];
    double gold, gems, level = c->player.level;

    q->done = TRUE;

    gold = floor((200.0 + level * 60.0) * (q->type == QT_CHAMPION ? 3.0 : 1.0));
    gems = floor((1.0 + level / 25.0) * (q->type == QT_CHAMPION ? 2.0 : 1.0));

    Do_send_line(c, "*** Daily quest complete! ***\n");
    sprintf(message, "You are rewarded with %.0lf gold and %.0lf gem%s.\n",
	    gold, gems, gems == 1 ? "" : "s");
    Do_send_line(c, message);

    Do_gold(c, gold, FALSE);
    Do_gems(c, gems, FALSE);

    sprintf(message, "%s has completed today's quest.\n", c->player.name);
    Do_broadcast(c, message);

	/* half of all quests also reward a piece of gear */
    if (q->type == QT_CHAMPION || RND() < 0.5) {
	Do_gear_quest_reward(c);
    }
}

	/* something quest-worthy happened */
void Do_quest_progress(struct client_t *c, int type, int amount)
{
    struct quest_t *q = &c->quest;
    char message[SZ_LINE];
    int counts = FALSE;

    Do_quest_ensure(c);

    if (!c->questLoaded || q->done) {
	return;
    }

    if (type == q->type) {
	counts = TRUE;
    }
	/* a deep kill also counts for "slay N monsters" quests */
    if (q->type == QT_SLAY && type == QT_SLAY_DEEP) {
	counts = TRUE;
    }
    if (q->type == QT_SLAY_DEEP && type == QT_SLAY_DEEP &&
	    c->player.circle < q->param) {
	counts = FALSE;
    }

    if (!counts) {
	return;
    }

    q->progress += amount;

    if (q->progress >= q->target) {
	q->progress = q->target;
	Do_quest_complete(c);
    }
    else {
	Do_quest_describe(q, message);
	Do_send_chat_self(c, message);
    }

    Do_lock_mutex(&c->realm->quest_lock);
    Do_quest_save(c);
    Do_unlock_mutex(&c->realm->quest_lock);
}

	/* a monster was killed */
void Do_quest_kill(struct client_t *c)
{
	/* QT_SLAY_DEEP covers both kinds of slaying quest */
    Do_quest_progress(c, QT_SLAY_DEEP, 1);
}

	/* called every turn: check "reach a circle" quests */
void Do_quest_check_location(struct client_t *c)
{
    Do_quest_ensure(c);

    if (c->questLoaded && !c->quest.done && c->quest.type == QT_REACH &&
	    c->player.circle >= c->quest.param) {

	Do_quest_progress(c, QT_REACH, 1);
    }
}

	/* greet a character entering the realm */
void Do_gameplay_login(struct client_t *c)
{
    char message[SZ_LINE];

    c->championFight = FALSE;
    c->questLoaded = FALSE;
    Do_quest_ensure(c);

	/* equipment (old shield/sword/quicksilver become gear) */
    Do_gear_load(c);
    Do_gear_apply(c, FALSE);

	/* the browser shows any world event under way */
    c->shownEvent = -1;
    Do_world_event_sync(c);

    if (c->questLoaded) {
	Do_quest_describe(&c->quest, message);
	Do_send_chat_self(c, message);
    }

    if (Do_world_event_status(c, message)) {
	Do_send_chat_self(c, message);
    }

    Do_send_chat_self(c, "Type /help in chat for quest and event commands.\n");
}


/************************************************************************
/ chat commands
*************************************************************************/

	/* returns TRUE if the chat line was a command (and handled) */
int Do_chat_command(struct client_t *c, char *text)
{
    char message[SZ_LINE];

    if (text[0] != '/') {
	return FALSE;
    }

    if (strncasecmp(text, "/quest", 6) == 0) {

	if (!c->characterLoaded) {
	    Do_send_chat_self(c, "Enter the realm with a character to get a quest.\n");
	}
	else {
	    Do_quest_ensure(c);
	    Do_quest_describe(&c->quest, message);
	    Do_send_chat_self(c, message);
	}
    }
#ifdef GEAR_TEST
	/* test builds only: /eventtest N starts world event N for 5 minutes */
    else if (strncasecmp(text, "/eventtest", 10) == 0) {
	int n = atoi(text + 10);
	Do_lock_mutex(&c->realm->realm_lock);
	c->realm->worldEvent = (n >= WE_NONE && n <= WE_COUNT) ? n : WE_NONE;
	c->realm->worldEventEnds = time(NULL) + 300;
	Do_unlock_mutex(&c->realm->realm_lock);
	Do_world_event_publish(c->realm->worldEvent, c->realm->worldEventEnds);
	Do_broadcast(c, "*** (test) world event changed ***\n");
    }
#endif
    else if (strncasecmp(text, "/event", 6) == 0) {

	Do_world_event_status(c, message);
	Do_send_chat_self(c, message);
    }
    else if (strncasecmp(text, "/gear", 5) == 0) {

	Do_gear_chat(c);
    }
    else if (strncasecmp(text, "/artifacts", 10) == 0 ||
	    strncasecmp(text, "/legends", 8) == 0) {

	Do_artifacts_chat(c);
    }
    else if (strncasecmp(text, "/filter", 7) == 0) {

	c->game->chatFilter = !c->game->chatFilter;
	Do_send_chat_self(c, c->game->chatFilter ?
		"Chat filter on.\n" : "Chat filter off.\n");
    }
    else if (strncasecmp(text, "/help", 5) == 0) {

	Do_send_chat_self(c, "/quest - show today's quest and your progress\n");
	Do_send_chat_self(c, "/event - show the current world event\n");
	Do_send_chat_self(c, "/gear - list your gear (Info > Gear to change it)\n");
	Do_send_chat_self(c, "/artifacts - who holds each artifact\n");
	Do_send_chat_self(c, "/filter - turn the chat filter on or off\n");
    }
    else {
	Do_send_chat_self(c, "Unknown command.  Type /help for a list.\n");
    }

    return TRUE;
}
