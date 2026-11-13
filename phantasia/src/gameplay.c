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
 * Start world event theEvent (1-3; 0 picks one at random) for the given
 * minutes and tell everyone.  Used by the timer and by wizards.
 */
void Do_world_event_start(struct client_t *c, int theEvent, int minutes)
{
    time_t now = time(NULL);
    char message[SZ_LINE];

    if (theEvent < 1 || theEvent > WE_COUNT) {
	theEvent = MIN(WE_COUNT, 1 + (int) ROLL(0.0, (double) WE_COUNT));
    }
    minutes = MAX(1, minutes);

    Do_lock_mutex(&c->realm->realm_lock);
    c->realm->worldEvent = theEvent;
    c->realm->worldEventEnds = now + minutes * 60;
    Do_unlock_mutex(&c->realm->realm_lock);

    Do_world_event_publish(theEvent, now + minutes * 60);

    sprintf(message, "*** %s! For the next %d minute%s %s. ***\n",
	    Do_world_event_name(theEvent), minutes, minutes == 1 ? "" : "s",
	    Do_world_event_effect(theEvent));

    Do_broadcast(c, message);
    Do_log(EVENT_LOG, message);
}

	/* end the current world event, if any, and schedule the next */
void Do_world_event_stop(struct client_t *c)
{
    int interval = Do_env_minutes("PHANTASIA_EVENT_MINUTES", 90) * 60;
    int ended;
    char message[SZ_LINE];

    Do_lock_mutex(&c->realm->realm_lock);
    ended = c->realm->worldEvent;
    c->realm->worldEvent = WE_NONE;
    c->realm->nextWorldEvent = time(NULL) +
	    (time_t) (interval * (0.5 + RND()));
    Do_unlock_mutex(&c->realm->realm_lock);

    if (ended == WE_NONE) {
	return;
    }

    Do_world_event_publish(WE_NONE, 0);

    sprintf(message, "*** The %s has ended. ***\n",
	    Do_world_event_name(ended));
    Do_broadcast(c, message);
}

/*
 * Called on every player turn.  Starts and ends world events; whichever
 * player's turn notices the change announces it to everyone.
 */
void Do_world_event_tick(struct client_t *c)
{
    time_t now = time(NULL);
    int interval = Do_env_minutes("PHANTASIA_EVENT_MINUTES", 90) * 60;
    int length = Do_env_minutes("PHANTASIA_EVENT_LENGTH", 20);
    int start = FALSE, stop = FALSE;

    Do_lock_mutex(&c->realm->realm_lock);

    if (c->realm->nextWorldEvent == 0) {
	c->realm->nextWorldEvent = now + (time_t) (interval * (0.5 + RND()));
    }

    if (c->realm->worldEvent != WE_NONE) {
	stop = now >= c->realm->worldEventEnds;
    }
    else {
	start = now >= c->realm->nextWorldEvent;
    }

    Do_unlock_mutex(&c->realm->realm_lock);

    if (stop) {
	Do_world_event_stop(c);
    }
    else if (start) {
	Do_world_event_start(c, 0, length);
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
    return (Do_current_world_event(c) == WE_GOLD_RUSH ? 2.0 : 1.0) *
	    Do_reign_gold_factor(c);
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

	/* a wizard sent one */
    if (c->forceChampion) {
	c->forceChampion = FALSE;
	chance = 1.0;
    }

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
static void Do_ceremony_chat(struct client_t *c, char *args);

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
    else if (strncasecmp(text, "/event", 6) == 0) {

	char verb[16] = "", kind[16] = "";
	int minutes = 20, theEvent = 0;

	sscanf(text + 6, "%15s %15s %d", verb, kind, &minutes);

	if (c->wizard >= 3 && strcasecmp(verb, "start") == 0) {
	    if (strncasecmp(kind, "blood", 5) == 0) theEvent = WE_BLOOD_MOON;
	    else if (strncasecmp(kind, "gold", 4) == 0) theEvent = WE_GOLD_RUSH;
	    else if (strncasecmp(kind, "hunt", 4) == 0 ||
		    strncasecmp(kind, "champ", 5) == 0) theEvent = WE_CHAMPION_HUNT;
	    else if (strncasecmp(kind, "random", 6) != 0) {
		Do_send_chat_self(c, "Use: /event start blood|gold|hunt|random [minutes]\n");
		return TRUE;
	    }
	    Do_world_event_start(c, theEvent, MAX(1, MIN(1440, minutes)));
	    Do_wizard_log(c, "started a world event");
	}
	else if (c->wizard >= 3 && strcasecmp(verb, "stop") == 0) {
	    Do_world_event_stop(c);
	    Do_wizard_log(c, "stopped the world event");
	}
	else {
	    Do_world_event_status(c, message);
	    Do_send_chat_self(c, message);
	}
    }
#ifdef GEAR_TEST
	/* test builds only: /ceremonytest N shows ceremony N to everyone */
    else if (strncasecmp(text, "/ceremonytest", 13) == 0) {
	Do_announce_ceremony(c, atoi(text + 13));
    }
#endif
    else if (strncasecmp(text, "/ceremony", 9) == 0 && text[9] != 't' &&
	    c->wizard >= 2) {
	Do_ceremony_chat(c, text + 9);
    }
    else if (strncasecmp(text, "/court", 6) == 0 ||
	    strncasecmp(text, "/treasury", 9) == 0) {
	Do_court_chat(c);
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
	Do_send_chat_self(c, "/court - who rules, the royal coffers and the Valar's blessing\n");
	if (c->wizard >= 3) {
	    Do_send_chat_self(c, "Wizards: /event start blood|gold|hunt|random [minutes], /event stop\n");
	    Do_send_chat_self(c, "Wizards: Administrate > Realm Tools for artifacts, gear, bags, quests, champions, ceremonies\n");
	    Do_send_chat_self(c, "Wizards: /ceremony to preview a ceremony on your screen\n");
	}
    }
    else {
	Do_send_chat_self(c, "Unknown command.  Type /help for a list.\n");
    }

    return TRUE;
}


/************************************************************************
/ wizard tools: Administrate > Realm Tools
*************************************************************************/

	/* record what a wizard did */
void Do_wizard_log(struct client_t *c, char *what)
{
    char line[SZ_LINE];

    sprintf(line, "[%s] Wizard %s %s.\n", c->connection_id, c->player.name,
	    what);
    Do_log(EVENT_LOG, line);
}

	/* the target player's thread applies a wizard's action */
void Do_wizard_event(struct client_t *c, struct event_t *the_event)
{
    struct event_t *event_ptr;

    switch (the_event->type) {

    case WIZ_GEAR_EVENT:
	Do_gear_wizard_gift(c, (int) the_event->arg1, (int) the_event->arg2,
		(double) the_event->arg3);
	break;

    case WIZ_BAG_EVENT:
	Do_gear_wizard_bag(c, (int) the_event->arg3);
	break;

    case WIZ_ARTIFACT_EVENT:
	Do_gear_wizard_artifact(c, (int) the_event->arg3);
	break;

    case WIZ_QUEST_EVENT:
	Do_quest_ensure(c);
	if (!c->questLoaded) {
	    break;
	}
	if (the_event->arg3 == 1) {
	    if (!c->quest.done) {
		c->quest.progress = c->quest.target;
		Do_quest_complete(c);
	    }
	}
	else {
	    Do_quest_new(c);
	    Do_send_line(c, "The Wizards have set you a new quest.\n");
	}
	{
	    char message[SZ_LINE];
	    Do_quest_describe(&c->quest, message);
	    Do_send_chat_self(c, message);
	}
	Do_lock_mutex(&c->realm->quest_lock);
	Do_quest_save(c);
	Do_unlock_mutex(&c->realm->quest_lock);
	break;

    case WIZ_CHAMPION_EVENT:
	c->forceChampion = TRUE;
	event_ptr = (struct event_t *) Do_create_event();
	event_ptr->type = MONSTER_EVENT;
	event_ptr->arg1 = MONSTER_RANDOM;
	event_ptr->arg3 = SM_RANDOM;
	Do_file_event(c, event_ptr);
	break;
    }
}

	/* send a wizard event to a player chosen from those online */
static void Do_wizard_send(struct client_t *c, int type, double arg1,
	double arg2, long arg3, char *what)
{
    struct event_t *event_ptr;
    char name[SZ_NAME], prompt[SZ_LINE], log[SZ_LINE];

    sprintf(prompt, "Who do you want to %s?\n", what);
    if (Do_player_dialog(c, prompt, name) != S_NORM) {
	return;
    }

    event_ptr = (struct event_t *) Do_create_event();
    event_ptr->type = type;
    event_ptr->arg1 = arg1;
    event_ptr->arg2 = arg2;
    event_ptr->arg3 = arg3;
    event_ptr->from = c->game;

    if (!Do_send_character_event(c, event_ptr, name)) {
	free((void *)event_ptr);
	Do_send_line(c, "That character just left the game.\n");
	Do_more(c);
	return;
    }

    sprintf(log, "used Realm Tools to %s %s", what, name);
    Do_wizard_log(c, log);
    Do_send_line(c, "Done.\n");
    Do_more(c);
}

	/* Administrate > Send Event: pick what happens from a list instead
	   of remembering event numbers */
enum { ASK_NONE, ASK_EXPERIENCE, ASK_MONSTER, ASK_COORDS, ASK_RAW };

static const struct {
    const char *label;
    const char *verb;		/* "Who do you want to ...?" */
    short type;
    double arg1;
    int ask;
    int named;			/* the event carries the wizard's name */
} wizard_sends[] = {
    { "Bless",                          "bless",               BLESS_EVENT,      0,          ASK_NONE,       TRUE  },
    { "Restore (fully heal)",           "restore",             HEAL_EVENT,       P_RESTORE,  ASK_NONE,       TRUE  },
    { "Cure poison",                    "cure",                HEAL_EVENT,       P_CURE,     ASK_NONE,       TRUE  },
    { "Knight",                         "knight",              KNIGHT_EVENT,     0,          ASK_NONE,       TRUE  },
    { "Give experience...",             "give experience to",  EXPERIENCE_EVENT, 0,          ASK_EXPERIENCE, FALSE },
    { "Send a monster...",              "send the monster to", SUMMON_EVENT,     0,          ASK_MONSTER,    TRUE  },
    { "Find a treasure trove",          "send a trove to",     TROVE_EVENT,      0,          ASK_NONE,       FALSE },
    { "Find a trading post",            "send a trading post to", TRADING_EVENT, 0,          ASK_NONE,       FALSE },
    { "Meet a medic",                   "send a medic to",     MEDIC_EVENT,      0,          ASK_NONE,       FALSE },
    { "Meet a guru",                    "send a guru to",      GURU_EVENT,       0,          ASK_NONE,       FALSE },
    { "Find a village",                 "send a village to",   VILLAGE_EVENT,    0,          ASK_NONE,       FALSE },
    { "Meet the tax collector",         "tax",                 TAX_EVENT,        0,          ASK_NONE,       FALSE },
    { "Energy void",                    "trap in a void",      ENERGY_VOID_EVENT, 0,         ASK_NONE,       FALSE },
    { "Plague",                         "infect",              PLAGUE_EVENT,     0,          ASK_NONE,       FALSE },
    { "Curse",                          "curse",               CURSE_EVENT,      P_CURSE,    ASK_NONE,       TRUE  },
    { "Execrate (strong curse)",        "execrate",            CURSE_EVENT,      P_EXECRATE, ASK_NONE,       TRUE  },
    { "Smite",                          "smite",               CURSE_EVENT,      P_SMITE,    ASK_NONE,       TRUE  },
    { "Slap",                           "slap",                SLAP_EVENT,       0,          ASK_NONE,       TRUE  },
    { "Blind",                          "blind",               BLIND_EVENT,      0,          ASK_NONE,       TRUE  },
    { "Degenerate",                     "degenerate",          DEGENERATE_EVENT, 0,          ASK_NONE,       TRUE  },
    { "Reprimand",                      "reprimand",           REPRIMAND_EVENT,  0,          ASK_NONE,       TRUE  },
    { "Move to coordinates...",         "move",                RELOCATE_EVENT,   0,          ASK_COORDS,     TRUE  },
    { "Change gender",                  "change",              SEX_CHANGE_EVENT, 0,          ASK_NONE,       FALSE },
    { "Make them rest",                 "rest",                REST_EVENT,       0,          ASK_NONE,       FALSE },
    { "Save their game",                "save",                SAVE_EVENT,       0,          ASK_NONE,       FALSE },
    { "Make Steward",                   "make Steward",        STEWARD_EVENT,    0,          ASK_NONE,       FALSE },
    { "Make King / Queen",              "crown",               KING_EVENT,       0,          ASK_NONE,       FALSE },
    { "Make Valar (or end their Valar)", "raise to the Valar",  VALAR_EVENT,      0,          ASK_NONE,       FALSE },
    { "Take their throne (Steward/King)", "dethrone",          DETHRONE_EVENT,   0,          ASK_NONE,       FALSE },
    { "Advanced: event by number...",   NULL,                  0,                0,          ASK_RAW,        FALSE },
};

#define NUM_WIZARD_SENDS ((int)(sizeof wizard_sends / sizeof wizard_sends[0]))

	/* returns TRUE if the wizard asked for the old event-by-number
	   screen, which the caller then shows */
int Do_wizard_send_event(struct client_t *c)
{
    const char *labels[NUM_MONSTERS + MAX_EXTRA_MONSTERS];
    struct event_t *event_ptr;
    char name[SZ_LINE], prompt[SZ_LINE], log[SZ_LINE * 2];
    double arg1, arg2 = 0;
    long pick, monster = 0;
    int i, n;

    for (i = 0; i < NUM_WIZARD_SENDS; i++) labels[i] = wizard_sends[i].label;
    if (Do_select_dialog(c, &pick, "What should happen?\n", labels,
	    NUM_WIZARD_SENDS) != S_NORM) {
	return FALSE;
    }
    if (wizard_sends[pick].ask == ASK_RAW) return TRUE;

    arg1 = wizard_sends[pick].arg1;

    switch (wizard_sends[pick].ask) {
    case ASK_EXPERIENCE:
	if (Do_double_dialog(c, &arg1, "How much experience?\n") || arg1 <= 0) {
	    return FALSE;
	}
	break;

    case ASK_MONSTER:
	n = NUM_MONSTERS + c->realm->numExtraMonsters;
	for (i = 0; i < n; i++) labels[i] = c->realm->monster[i].name;
	if (Do_select_dialog(c, &monster, "Which monster?\n", labels, n)
		!= S_NORM) {
	    return FALSE;
	}
	arg1 = (double) monster;
	break;

    case ASK_COORDS:
	if (Do_coords_dialog(c, &arg1, &arg2, "Move them to where?\n")) {
	    return FALSE;
	}
	break;
    }

    sprintf(prompt, "Who do you want to %s?\n", wizard_sends[pick].verb);
    if (Do_player_dialog(c, prompt, name) != S_NORM) {
	return FALSE;
    }

    event_ptr = (struct event_t *) Do_create_event();
    event_ptr->type = wizard_sends[pick].type;
    event_ptr->arg1 = arg1;
    event_ptr->arg2 = arg2;
    event_ptr->from = c->game;
    if (wizard_sends[pick].named) {
	event_ptr->arg3 = strlen(c->modifiedName) + 1;
	event_ptr->arg4 = (void *) Do_malloc(event_ptr->arg3);
	strcpy(event_ptr->arg4, c->modifiedName);
    }

    if (!Do_send_character_event(c, event_ptr, name)) {
	if (event_ptr->arg4) free((void *)event_ptr->arg4);
	free((void *)event_ptr);
	Do_send_line(c, "That character just left the game.\n");
	Do_more(c);
	return FALSE;
    }

    if (wizard_sends[pick].ask == ASK_MONSTER) {
	sprintf(log, "sent the monster %s to %s", c->realm->monster[monster].name, name);
    } else {
	sprintf(log, "sent \"%s\" to %s", wizard_sends[pick].label, name);
    }
    Do_wizard_log(c, log);
    Do_send_line(c, "What you ask shall be done, eminence.\n");
    Do_more(c);
    return FALSE;
}

static void Do_wizard_events_menu(struct client_t *c)
{
    struct button_t buttons;
    char message[SZ_LINE];
    long answer, minutes = 20;

    Do_world_event_status(c, message);
    Do_send_line(c, message);
    Do_send_line(c, "Start or end a world event:\n");

    Do_clear_buttons(&buttons, 0);
    strcpy(buttons.button[0], "Blood Moon\n");
    strcpy(buttons.button[1], "Gold Rush\n");
    strcpy(buttons.button[2], "Champion Hunt\n");
    strcpy(buttons.button[3], "Random\n");
    strcpy(buttons.button[4], "End Event\n");
    strcpy(buttons.button[7], "Back\n");

    if (Do_buttons(c, &answer, &buttons) != S_NORM || answer == 7 ||
	    answer < 0 || answer > 4) {
	return;
    }

    if (answer == 4) {
	Do_world_event_stop(c);
	Do_wizard_log(c, "stopped the world event");
	return;
    }

    if (Do_long_dialog(c, &minutes, "For how many minutes? (1-1440)\n")) {
	return;
    }

    Do_world_event_start(c, answer == 3 ? 0 : (int) answer + 1,
	    (int) MAX(1, MIN(1440, minutes)));
    Do_wizard_log(c, "started a world event");
}

static void Do_wizard_artifacts_menu(struct client_t *c)
{
    struct button_t buttons;
    struct event_t *event_ptr;
    char line[SZ_LINE], holder[SZ_NAME];
    long answer, id;
    int i;

    for (i = 1; i <= NUM_ARTIFACTS; i++) {
	Do_artifact_holder(c, i, holder);
	sprintf(line, "%2d. %s: %s\n", i, Do_artifact_name(i),
		holder[0] ? holder : "lost in the realm");
	Do_send_line(c, line);
    }

    Do_clear_buttons(&buttons, 0);
    strcpy(buttons.button[0], "Return One\n");
    strcpy(buttons.button[1], "Give One\n");
    strcpy(buttons.button[7], "Back\n");

    if (Do_buttons(c, &answer, &buttons) != S_NORM || answer < 0 ||
	    answer > 1) {
	return;
    }

    {
	const char *labels[NUM_ARTIFACTS];
	char text[NUM_ARTIFACTS][SZ_LINE];

	for (i = 1; i <= NUM_ARTIFACTS; i++) {
	    Do_artifact_holder(c, i, holder);
	    snprintf(text[i - 1], SZ_LINE, "%s (%s)", Do_artifact_name(i),
		    holder[0] ? holder : "unclaimed");
	    labels[i - 1] = text[i - 1];
	}
	if (Do_select_dialog(c, &id, "Which artifact?\n", labels,
		NUM_ARTIFACTS) != S_NORM) {
	    return;
	}
	id += 1;
    }

    Do_artifact_holder(c, (int) id, holder);

    if (answer == 0) {
	if (!holder[0]) {
	    Do_send_line(c, "It is already lost in the realm.\n");
	    Do_more(c);
	    return;
	}

	    /* the holder's own thread removes it if they are online */
	event_ptr = (struct event_t *) Do_create_event();
	event_ptr->type = WIZ_ARTIFACT_EVENT;
	event_ptr->arg3 = -id;
	event_ptr->from = c->game;

	if (!Do_send_character_event(c, event_ptr, holder)) {
	    free((void *)event_ptr);
	    Do_artifact_release_offline(c, (int) id);
	}

	sprintf(line, "returned %s from %s to the realm",
		Do_artifact_name((int) id), holder);
	Do_wizard_log(c, line);
	Do_send_line(c, "Done.\n");
	Do_more(c);
	return;
    }

    if (holder[0]) {
	sprintf(line, "%s holds it.  Return it to the realm first.\n", holder);
	Do_send_line(c, line);
	Do_more(c);
	return;
    }

    Do_wizard_send(c, WIZ_ARTIFACT_EVENT, 0, 0, id, "give the artifact to");
}

static void Do_wizard_gear_menu(struct client_t *c)
{
    struct button_t buttons;
    long slot, tier, circle = 0;

    Do_send_line(c, "Which kind of gear?\n");
    Do_clear_buttons(&buttons, 0);
    strcpy(buttons.button[0], "Weapon\n");
    strcpy(buttons.button[1], "Armor\n");
    strcpy(buttons.button[2], "Helm\n");
    strcpy(buttons.button[3], "Boots\n");
    strcpy(buttons.button[4], "Trinket\n");
    strcpy(buttons.button[5], "Any\n");
    strcpy(buttons.button[7], "Back\n");
    if (Do_buttons(c, &slot, &buttons) != S_NORM || slot < 0 || slot > 5) {
	return;
    }

    Do_send_line(c, "Which tier?\n");
    Do_clear_buttons(&buttons, 0);
    strcpy(buttons.button[0], "Common\n");
    strcpy(buttons.button[1], "Fine\n");
    strcpy(buttons.button[2], "Masterwork\n");
    strcpy(buttons.button[3], "Enchanted\n");
    strcpy(buttons.button[7], "Back\n");
    if (Do_buttons(c, &tier, &buttons) != S_NORM || tier < 0 || tier > 3) {
	return;
    }

    if (Do_long_dialog(c, &circle,
	    "Made for which circle? (0 = where the player is)\n")) {
	return;
    }

    Do_wizard_send(c, WIZ_GEAR_EVENT, slot == 5 ? -1 : slot,
	    TIER_COMMON + tier, MAX(0, circle), "give the gear to");
}

static void Do_wizard_bag_menu(struct client_t *c)
{
    static const int sizes[] = { 6, 10, 15, 20, 25, 30, 40, 50 };
    struct button_t buttons;
    long answer;
    int i;

    Do_send_line(c, "Which bag? (Items already carried are never lost.)\n");
    Do_clear_buttons(&buttons, 0);
    for (i = 0; i < 8; i++) {
	sprintf(buttons.button[i], "%.16s\n", Do_bag_name(sizes[i]));
    }
    if (Do_buttons(c, &answer, &buttons) != S_NORM || answer < 0 ||
	    answer > 7) {
	return;
    }

    Do_wizard_send(c, WIZ_BAG_EVENT, 0, 0, sizes[answer], "give the bag to");
}

static void Do_wizard_quest_menu(struct client_t *c)
{
    struct button_t buttons;
    long answer;

    Do_clear_buttons(&buttons, 0);
    strcpy(buttons.button[0], "New Quest\n");
    strcpy(buttons.button[1], "Complete Quest\n");
    strcpy(buttons.button[7], "Back\n");
    if (Do_buttons(c, &answer, &buttons) != S_NORM || answer < 0 ||
	    answer > 1) {
	return;
    }

    Do_wizard_send(c, WIZ_QUEST_EVENT, 0, 0, answer,
	    answer ? "complete the quest of" : "give a new quest to");
}

	/* ceremonies a wizard can preview */
static const struct { const char *label, *word; int kind; } ceremonies[] = {
    { "Wizard arrives (full wizard)", "wizard",     4 },
    { "Wizard arrives (wizard)",      "wizard3",    3 },
    { "Apprentice arrives",           "apprentice", 2 },
    { "New Steward",                  "steward",    CEREMONY_STEWARD },
    { "New King",                     "king",       CEREMONY_KING },
    { "New Queen",                    "queen",      CEREMONY_QUEEN },
    { "New Valar",                    "valar",      CEREMONY_VALAR },
};
#define NUM_CEREMONIES ((int)(sizeof ceremonies / sizeof ceremonies[0]))

	/* play a ceremony on this player's own screen only */
static void Do_preview_ceremony(struct client_t *c, int kind)
{
    char line[SZ_LINE];

    if (!c->isBrowser) {
	Do_send_chat_self(c, "Ceremonies are shown in the browser client.\n");
	return;
    }
    sprintf(line, "%d\n", kind);
    Do_send_int(c, WIZARD_PACKET);
    Do_send_string(c, line);
    Do_send_string(c, c->modifiedName);
    Do_send_string(c, "\n");
    Do_send_buffer(c);
}

	/* /ceremony [name] [all] */
static void Do_ceremony_chat(struct client_t *c, char *args)
{
    char word[32] = "", who[8] = "";
    int i;

    sscanf(args, "%31s %7s", word, who);
    for (i = 0; i < NUM_CEREMONIES; i++) {
	if (!strcasecmp(word, ceremonies[i].word)) break;
    }
    if (i == NUM_CEREMONIES) {
	Do_send_chat_self(c, "/ceremony wizard|wizard3|apprentice|steward|king|queen|valar - preview it on your screen\n");
	if (c->wizard >= 4) {
	    Do_send_chat_self(c, "Add \"all\" to show it to everyone, e.g. /ceremony king all\n");
	}
	return;
    }
    if (!strcasecmp(who, "all") && c->wizard >= 4) {
	Do_announce_ceremony(c, ceremonies[i].kind);
	Do_wizard_log(c, "showed everyone a ceremony");
    } else {
	Do_preview_ceremony(c, ceremonies[i].kind);
    }
}

static void Do_wizard_ceremony_menu(struct client_t *c)
{
    const char *labels[NUM_CEREMONIES];
    struct button_t buttons;
    long pick, answer = 0;
    int i;

    for (i = 0; i < NUM_CEREMONIES; i++) labels[i] = ceremonies[i].label;
    if (Do_select_dialog(c, &pick, "Which ceremony?\n", labels,
	    NUM_CEREMONIES) != S_NORM) {
	return;
    }

    if (c->wizard >= 4) {
	Do_send_line(c, "Show it to whom?\n");
	Do_clear_buttons(&buttons, 0);
	strcpy(buttons.button[0], "Just Me\n");
	strcpy(buttons.button[1], "Everyone\n");
	strcpy(buttons.button[7], "Back\n");
	if (Do_buttons(c, &answer, &buttons) != S_NORM || answer < 0 ||
		answer > 1) {
	    return;
	}
    }
    Do_send_clear(c);

    if (answer == 1) {
	Do_announce_ceremony(c, ceremonies[pick].kind);
	Do_wizard_log(c, "showed everyone a ceremony");
    } else {
	Do_preview_ceremony(c, ceremonies[pick].kind);
    }
}

	/* one action per visit, so queued results (even gifts to yourself)
	   take effect as soon as you are back on the main menu */
void Do_wizard_tools(struct client_t *c)
{
    static const char *labels[] = {
	"World Event", "Artifacts", "Give Gear", "Give Bag", "Quests",
	"Champion", "Preview a Ceremony", "Moderate",
    };
    long answer;

    Do_send_clear(c);
    if (Do_select_dialog(c, &answer, "Realm Tools: what would you like to do?\n",
	    labels, 8) != S_NORM) {
	Do_send_clear(c);
	return;
    }
    Do_send_clear(c);

    switch (answer) {
    case 0: Do_wizard_events_menu(c); break;
    case 1: Do_wizard_artifacts_menu(c); break;
    case 2: Do_wizard_gear_menu(c); break;
    case 3: Do_wizard_bag_menu(c); break;
    case 4: Do_wizard_quest_menu(c); break;
    case 5:
	Do_wizard_send(c, WIZ_CHAMPION_EVENT, 0, 0, 0,
		"send a champion against");
	break;
    case 6: Do_wizard_ceremony_menu(c); break;
    case 7: Do_moderate(c); break;
    }
    Do_send_clear(c);
}


	/* tell every game about a ceremony (Do_file_event shows browsers):
	   a wizard arriving (2-4) or a new Steward, King, Queen or Valar */
void Do_announce_ceremony(struct client_t *c, int kind)
{
    struct event_t *event_ptr;

	/* a new title: put on the regalia straight away */
    if (kind >= CEREMONY_STEWARD) {
	Do_royal_refresh(c);
    }

    event_ptr = (struct event_t *) Do_create_event();
    event_ptr->type = WIZARD_ARRIVAL_EVENT;
    event_ptr->arg1 = (double) kind;
    event_ptr->arg3 = strlen(c->modifiedName) + 1;
    event_ptr->arg4 = (void *) Do_malloc(event_ptr->arg3);
    strcpy(event_ptr->arg4, c->modifiedName);
    event_ptr->from = c->game;

    Do_broadcast_event(c, event_ptr);
}

void Do_announce_wizard(struct client_t *c)
{
    Do_announce_ceremony(c, c->wizard);
}
