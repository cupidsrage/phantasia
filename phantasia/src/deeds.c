/*
 * deeds.c - account-wide deeds: lifetime achievements that give every
 * character on the account a small permanent bonus.
 *
 * Progress is kept per account in DEEDS_FILE, one line per account:
 *   account count0 count1 ... countN artifactMask
 * Each deed has tiers; every tier earned adds its bonus (experience from
 * fights, gold found, or the chance of finding gear).
 */

#include "include.h"

enum {
    DEED_SLAYER, DEED_CHAMPION, DEED_STORM, DEED_QUESTS, DEED_RELICS,
    DEED_SEASONED, DEED_STEWARD, DEED_CROWN, DEED_VALAR, DEED_DARKLORD,
    NUM_DEEDS
};

#define MAX_TIERS 4

static const struct deed_def {
    const char *name;
    const char *counts;		/* what is counted, for /deeds */
    long tier[MAX_TIERS];	/* thresholds, 0 = no such tier */
    double xp, gold, gear;	/* bonus per tier, in percent */
} deeds[NUM_DEEDS] = {
    { "Monster Slayer",       "monsters slain",              { 100, 1000, 10000, 100000 }, 1, 0, 0 },
    { "Champion's Bane",      "champions slain",             { 5, 50, 250, 0 },            0, 0, 5 },
    { "Stormrider",           "monsters slain in world events", { 100, 1000, 0, 0 },       1, 1, 0 },
    { "Dutiful",              "daily quests completed",      { 7, 30, 100, 365 },          0, 1, 0 },
    { "Relic Hunter",         "different artifacts held",    { 1, 5, 11, 0 },              0, 0, 5 },
    { "Seasoned",             "highest level reached",       { 100, 500, 1000, 3000 },     1, 0, 0 },
    { "Served as Steward",    "times made Steward",          { 1, 0, 0, 0 },               0, 1, 0 },
    { "Wore the Crown",       "times crowned King or Queen", { 1, 0, 0, 0 },               0, 1, 0 },
    { "Ascended to the Valar", "times raised to the Valar",  { 1, 0, 0, 0 },               2, 0, 0 },
    { "Dark Lord's Doom",     "times the Dark Lord fell to you", { 1, 0, 0, 0 },           2, 2, 0 },
};

struct deed_rec {
    char account[SZ_NAME];
    long count[NUM_DEEDS];
    long artifacts;		/* bit n set: artifact n has been held */
};

static pthread_mutex_t deeds_lock = PTHREAD_MUTEX_INITIALIZER;
static struct deed_rec *recs = NULL;
static int numRecs = 0, capRecs = 0, loaded = FALSE;
static time_t lastSave = 0;

static const char *roman[] = { "", " I", " II", " III", " IV" };

/************************************************************************
/ storage (deeds_lock held)
*************************************************************************/

static void Do_deeds_load_locked(void)
{
    FILE *f;
    char line[512];

    loaded = TRUE;
    if ((f = fopen(DEEDS_FILE, "r")) == NULL) {
	return;
    }
    while (fgets(line, sizeof line, f)) {
	struct deed_rec r;
	char *p = line, *end;
	int i, n;

	memset(&r, 0, sizeof r);
	if (sscanf(p, "%32s%n", r.account, &n) != 1) continue;
	p += n;
	for (i = 0; i < NUM_DEEDS; i++) {
	    r.count[i] = strtol(p, &end, 10);
	    if (end == p) break;
	    p = end;
	}
	r.artifacts = strtol(p, &end, 10);

	if (numRecs == capRecs) {
	    capRecs = capRecs ? capRecs * 2 : 64;
	    recs = (struct deed_rec *) realloc(recs, capRecs * sizeof *recs);
	}
	recs[numRecs++] = r;
    }
    fclose(f);
}

static void Do_deeds_save_locked(void)
{
    FILE *f;
    int i, j;

    if ((f = fopen(TEMP_DEEDS_FILE, "w")) == NULL) {
	return;
    }
    for (i = 0; i < numRecs; i++) {
	fprintf(f, "%s", recs[i].account);
	for (j = 0; j < NUM_DEEDS; j++) fprintf(f, " %ld", recs[i].count[j]);
	fprintf(f, " %ld\n", recs[i].artifacts);
    }
    fclose(f);
    rename(TEMP_DEEDS_FILE, DEEDS_FILE);
    lastSave = time(NULL);
}

	/* this account's record, made if new (deeds_lock held) */
static struct deed_rec *Do_deeds_find_locked(const char *account, int make)
{
    int i;

    if (!loaded) Do_deeds_load_locked();
    if (account == NULL || account[0] == '\0') return NULL;

    for (i = 0; i < numRecs; i++) {
	if (strcmp(recs[i].account, account) == 0) return &recs[i];
    }
    if (!make) return NULL;

    if (numRecs == capRecs) {
	capRecs = capRecs ? capRecs * 2 : 64;
	recs = (struct deed_rec *) realloc(recs, capRecs * sizeof *recs);
    }
    memset(&recs[numRecs], 0, sizeof recs[numRecs]);
    strncpy(recs[numRecs].account, account, SZ_NAME - 1);
    return &recs[numRecs++];
}

static int Do_deed_tiers(int d, long count)
{
    int t, n = 0;

    for (t = 0; t < MAX_TIERS; t++) {
	if (deeds[d].tier[t] > 0 && count >= deeds[d].tier[t]) n = t + 1;
    }
    return n;
}

static int Do_deed_max_tiers(int d)
{
    int t, n = 0;

    for (t = 0; t < MAX_TIERS; t++) if (deeds[d].tier[t] > 0) n = t + 1;
    return n;
}

/************************************************************************
/ progress
*************************************************************************/

static void Do_deed_announce(struct client_t *c, int d, int tier)
{
    char line[SZ_LINE], bonus[SZ_LINE] = "", part[64];

    if (deeds[d].xp > 0) {
	sprintf(part, "+%.0f%% experience", deeds[d].xp);
	strcat(bonus, part);
    }
    if (deeds[d].gold > 0) {
	sprintf(part, "%s+%.0f%% gold", bonus[0] ? " and " : "", deeds[d].gold);
	strcat(bonus, part);
    }
    if (deeds[d].gear > 0) {
	sprintf(part, "%s+%.0f%% gear finds", bonus[0] ? " and " : "", deeds[d].gear);
	strcat(bonus, part);
    }
    sprintf(line, "*** Deed earned: %s%s! All your characters gain %s. ***\n",
	    deeds[d].name, Do_deed_max_tiers(d) > 1 ? roman[tier] : "", bonus);
    Do_send_line(c, line);
    Do_send_chat_self(c, line);
}

	/* add to a deed's count (or raise it to at least `value` when
	   `isMax`); announces any tier earned */
static void Do_deed_bump(struct client_t *c, int d, long amount, int isMax)
{
    struct deed_rec *r;
    int before, after, t;
    long old;

    if (c->wizard >= 3) {		/* wizard powers don't earn deeds */
	return;
    }
    pthread_mutex_lock(&deeds_lock);
    if ((r = Do_deeds_find_locked(c->lcaccount, TRUE)) == NULL) {
	pthread_mutex_unlock(&deeds_lock);
	return;
    }
    old = r->count[d];
    if (isMax) {
	if (amount <= old) {
	    pthread_mutex_unlock(&deeds_lock);
	    return;
	}
	r->count[d] = amount;
    }
    else {
	r->count[d] += amount;
    }
    before = Do_deed_tiers(d, old);
    after = Do_deed_tiers(d, r->count[d]);

	/* save when something is earned, otherwise at most once a minute */
    if (after != before || time(NULL) - lastSave >= 60) {
	Do_deeds_save_locked();
    }
    pthread_mutex_unlock(&deeds_lock);

    for (t = before + 1; t <= after; t++) {
	Do_deed_announce(c, d, t);
    }
}

	/* a monster fell to this player */
void Do_deeds_kill(struct client_t *c)
{
    Do_deed_bump(c, DEED_SLAYER, 1, FALSE);
    if (c->championFight) {
	Do_deed_bump(c, DEED_CHAMPION, 1, FALSE);
    }
    if (Do_current_world_event(c) != WE_NONE) {
	Do_deed_bump(c, DEED_STORM, 1, FALSE);
    }
    if (c->battle.opponent->special_type == SM_DARKLORD) {
	Do_deed_bump(c, DEED_DARKLORD, 1, FALSE);
    }
}

void Do_deeds_quest(struct client_t *c)
{
    Do_deed_bump(c, DEED_QUESTS, 1, FALSE);
}

	/* kind is the ceremony: CEREMONY_STEWARD, _KING, _QUEEN or _VALAR */
void Do_deeds_title(struct client_t *c, int kind)
{
    if (kind == CEREMONY_STEWARD) Do_deed_bump(c, DEED_STEWARD, 1, FALSE);
    else if (kind == CEREMONY_KING || kind == CEREMONY_QUEEN) Do_deed_bump(c, DEED_CROWN, 1, FALSE);
    else if (kind == CEREMONY_VALAR) Do_deed_bump(c, DEED_VALAR, 1, FALSE);
}

	/* an artifact came into this player's hands */
void Do_deeds_artifact(struct client_t *c, int id)
{
    struct deed_rec *r;
    long mask, n = 0;

    if (c->wizard >= 3 || id < 1 || id > 30) {
	return;
    }
    pthread_mutex_lock(&deeds_lock);
    if ((r = Do_deeds_find_locked(c->lcaccount, TRUE)) == NULL) {
	pthread_mutex_unlock(&deeds_lock);
	return;
    }
    r->artifacts |= 1L << id;
    for (mask = r->artifacts; mask; mask &= mask - 1) ++n;
    pthread_mutex_unlock(&deeds_lock);

    Do_deed_bump(c, DEED_RELICS, n, TRUE);
}

	/* every turn: highest level reached */
void Do_deeds_turn(struct client_t *c)
{
    long level = (long) c->player.level;

    if (level > c->deedLevel) {
	c->deedLevel = level;
	Do_deed_bump(c, DEED_SEASONED, level, TRUE);
    }
}

/************************************************************************
/ bonuses
*************************************************************************/

	/* total bonus in percent: 0 experience, 1 gold, 2 gear */
static double Do_deeds_bonus(struct client_t *c, int which)
{
    struct deed_rec *r;
    double total = 0;
    int d, t;

    pthread_mutex_lock(&deeds_lock);
    r = Do_deeds_find_locked(c->lcaccount, FALSE);
    if (r != NULL) {
	for (d = 0; d < NUM_DEEDS; d++) {
	    t = Do_deed_tiers(d, r->count[d]);
	    total += t * (which == 0 ? deeds[d].xp : which == 1 ? deeds[d].gold : deeds[d].gear);
	}
    }
    pthread_mutex_unlock(&deeds_lock);
    return total;
}

double Do_deeds_xp_factor(struct client_t *c)   { return 1.0 + Do_deeds_bonus(c, 0) / 100.0; }
double Do_deeds_gold_factor(struct client_t *c) { return 1.0 + Do_deeds_bonus(c, 1) / 100.0; }
double Do_deeds_gear_factor(struct client_t *c) { return 1.0 + Do_deeds_bonus(c, 2) / 100.0; }

/************************************************************************
/ /deeds in chat
*************************************************************************/

void Do_deeds_chat(struct client_t *c)
{
    struct deed_rec r, *p;
    char line[SZ_LINE], next[64];
    int d, t, max;

    pthread_mutex_lock(&deeds_lock);
    p = Do_deeds_find_locked(c->lcaccount, FALSE);
    if (p) r = *p; else memset(&r, 0, sizeof r);
    pthread_mutex_unlock(&deeds_lock);

    Do_send_chat_self(c, "Deeds of your account (bonuses apply to all your characters):\n");
    for (d = 0; d < NUM_DEEDS; d++) {
	t = Do_deed_tiers(d, r.count[d]);
	max = Do_deed_max_tiers(d);
	if (max == 1) {				/* a one-time deed */
	    sprintf(line, "  %s: %s\n", deeds[d].name, t ? "earned" : "not yet");
	}
	else {
	    if (t < max) sprintf(next, "%s tier at %ld", t ? "next" : "first", deeds[d].tier[t]);
	    else strcpy(next, "all tiers earned");
	    sprintf(line, "  %s%s: %ld %s (%s)\n", deeds[d].name, roman[t],
		    r.count[d], deeds[d].counts, next);
	}
	Do_send_chat_self(c, line);
    }
    sprintf(line, "Total: +%.0f%% experience, +%.0f%% gold, +%.0f%% gear finds.\n",
	    Do_deeds_bonus(c, 0), Do_deeds_bonus(c, 1), Do_deeds_bonus(c, 2));
    Do_send_chat_self(c, line);
}

#ifdef GEAR_TEST
	/* test builds: put the account just short of a few deeds */
void Do_deeds_test(struct client_t *c)
{
    int saved = c->wizard;

    c->wizard = 0;
    Do_deed_bump(c, DEED_SLAYER, 99, FALSE);
    Do_deed_bump(c, DEED_QUESTS, 6, FALSE);
    c->wizard = saved;
    Do_send_chat_self(c, "Deeds: 99 kills and 6 quests added.\n");
}
#endif
