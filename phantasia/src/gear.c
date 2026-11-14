/*
 * gear.c - equipment and one-of-a-kind artifacts
 *
 * Shield, sword and quicksilver are no longer bought and found as bare
 * numbers.  Instead characters wear named gear in five slots (weapon,
 * armor, helm, boots, trinket) and carry a small backpack.  The equipped
 * items add up to the old shield/sword/quicksilver values, so the game's
 * combat formulas are unchanged, and many items also carry an ability.
 *
 * Artifacts are unique: only one of each exists.  The holder is kept in
 * ARTIFACT_FILE; when the holder dies, offers it for a boon, has it
 * stolen, or is not seen for ARTIFACT_IDLE_DAYS, it returns to the realm
 * for someone else to find.
 *
 * Gear lives in GEAR_FILE (one line per item), so character files are
 * unchanged.  Existing characters' shield/sword/quicksilver are converted
 * to "Worn" gear the first time they play.
 */

#include "include.h"

#define ARTIFACT_IDLE_DAYS	14
#ifndef GEAR_DROP_CHANCE
#define GEAR_DROP_CHANCE	0.07	/* chance a monster drops gear */
#endif
#ifndef ARTIFACT_TREASURE_CHANCE
#define ARTIFACT_TREASURE_CHANCE 0.0	/* artifacts come from great deeds, not treasure */
#endif
#ifndef ARTIFACT_CHAMPION_CHANCE
#define ARTIFACT_CHAMPION_CHANCE 0.03	/* chance a champion has one */
#endif

static const char *slotNames[NUM_SLOTS] = {
    "Weapon", "Armor", "Helm", "Boots", "Trinket"
};

	/* base item names per slot, roughly weakest to strongest */
static const char *baseNames[NUM_SLOTS][8] = {
    { "Dagger", "Short Sword", "Mace", "Longsword", "War Axe",
      "Warhammer", "Glaive", "Greatsword" },
    { "Padded Armor", "Leather Armor", "Studded Leather", "Chainmail",
      "Scale Mail", "Splint Mail", "Plate Armor", "Mithril Mail" },
    { "Leather Cap", "Iron Cap", "Iron Helm", "Barbute", "Great Helm",
      "Horned Helm", "Winged Helm", "Crowned Helm" },
    { "Sandals", "Leather Boots", "Hide Boots", "Ranger Boots",
      "Riding Boots", "Striders", "Seven-league Boots", "Winged Boots" },
    { "Copper Ring", "Bone Charm", "Silver Ring", "Jade Amulet",
      "Rune Stone", "Gold Ring", "Star Pendant", "Moonstone" }
};

static const char *tierNames[] = {
    "Worn", "", "Fine", "Masterwork", "Enchanted", ""
};

static const char *abilityNames[NUM_ABILITIES] = {
    "", "Evasion", "Lifesteal", "Swiftness", "Crushing Blows", "Thorns",
    "Fortune", "Warding", "Mending", "Prospecting", "Phoenix",
    "Seafarer", "Dragonbane", "Insight", "Arcana", "Wellspring", "Focus",
    "Soulreaper", "Execution", "Radiance", "Tideward", "Eagle's Flight",
    "Tailwind", "Dragonscale", "Fireborn", "Hoard", "Midas Touch",
    "First Blood", "Shadowmeld", "Unseen", "Timeless", "Sage's Wisdom",
    "Windwalker", "Gale", "Archmage", "Manafont", "Conqueror's Due", "Call the Muster"
};

static const char *abilitySuffix[NUM_ABILITIES] = {
    "", "of Evasion", "of the Leech", "of Swiftness", "of Crushing",
    "of Thorns", "of Fortune", "of Warding", "of Mending",
    "of Prospecting", "", "", "", "of Insight", "of Arcana",
    "of the Wellspring", "of Focus",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};

static const char *abilityHelp[NUM_ABILITIES] = {
    "",
    "may dodge a monster's blow",
    "heals you for part of the damage you deal",
    "you always strike first",
    "may land a crushing blow for double damage",
    "hurts monsters that hit you",
    "more gold from treasure",
    "resists poison",
    "regain energy as you travel",
    "find treasure more often",
    "saves you from death, then rests an hour before it can again",
    "the creatures of the water will not fight you",
    "double damage against dragons and wyrms",
    "sharpens your wits (more brains)",
    "deepens your magic (higher magic level)",
    "regain mana as you travel",
    "your spells cost less mana",
    "each monster you slay restores a fifth of your energy",
    "a blow that leaves a monster under a fifth of its energy finishes it",
    "every blow you strike restores some of your energy",
    "poison cannot touch you",
    "Move To carries you anywhere in a single turn",
    "you are twice as quick in battle",
    "monsters' blows do a quarter less damage",
    "curses cannot touch you",
    "gold never weighs you down",
    "you find twice the gold",
    "your first blow in every fight does triple damage",
    "you can always get away from a fight",
    "your luckouts succeed as if you were twice as clever",
    "you do not age while you wear it",
    "you gain a fifth more experience",
    "your steps carry you twice as far",
    "monsters are half as quick against you",
    "your spells are half again as strong",
    "your spells cost half the mana",
    "you gain a tenth more experience and gold",
    "once a day, /muster calls the Muster: for an hour the realm project counts double and everyone gains 25% more experience"
};

struct artifact_def {
    const char *name;
    short slot;
    short ability;
    short ability2;
    short minCircle;		/* where it can first be found */
};

	/* index 0 unused so that 0 means "not an artifact" */
static const struct artifact_def artifacts[NUM_ARTIFACTS + 1] = {
    { "", 0, 0, 0, 0 },
    { "Mourneblade", SLOT_WEAPON, AB_SOULREAP, AB_EXECUTE, 10 },
    { "The Phoenix Amulet", SLOT_TRINKET, AB_PHOENIX, AB_RADIANCE, 5 },
    { "The Crown of the Deep", SLOT_HELM, AB_SEAFARER, AB_TIDEWARD, 20 },
    { "Gwaihir's Feather", SLOT_BOOTS, AB_EAGLEFLIGHT, AB_TAILWIND, 8 },
    { "The Dragonscale Aegis", SLOT_ARMOR, AB_DRAGONSCALE, AB_FIREBORN, 15 },
    { "The Ring of Hoarding", SLOT_TRINKET, AB_HOARD, AB_MIDAS, 3 },
    { "Wyrmslayer", SLOT_WEAPON, AB_DRAGONBANE, AB_FIRSTBLOOD, 25 },
    { "The Mantle of Shadows", SLOT_ARMOR, AB_SHADOWMELD, AB_UNSEEN, 12 },
    { "The Helm of Ages", SLOT_HELM, AB_TIMELESS, AB_SAGE, 6 },
    { "The Wind-lord's Boots", SLOT_BOOTS, AB_WINDWALK, AB_GALE, 18 },
    { "The Archmage's Circlet", SLOT_HELM, AB_ARCHMAGE, AB_MANAFONT, 10 }
};


	/* bags, bought one upgrade at a time at trading posts */
struct bag_def {
    int size;
    const char *name;
    double price;
};

static const struct bag_def bags[] = {
    { 6, "Belt Pouch", 0 },
    { 10, "Satchel", 400 },
    { 15, "Leather Rucksack", 1500 },
    { 20, "Traveler's Pack", 4000 },
    { 25, "Adventurer's Pack", 9000 },
    { 30, "Reinforced Pack", 18000 },
    { 40, "Bag of Holding", 40000 },
    { 50, "Bag of the Endless Road", 80000 }
};

#define NUM_BAGS (int) (sizeof(bags) / sizeof(bags[0]))

	/* which bag a capacity belongs to */
static int Do_bag_index(int size)
{
    int i, best = 0;

    for (i = 0; i < NUM_BAGS; i++) {
	if (bags[i].size <= size) {
	    best = i;
	}
    }

    return best;
}

	/* how many times more gold each bag lets you carry before it slows
	   you down (in step with the bags table above) */
static const double bagGold[] = { 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0 };

double Do_bag_gold_factor(int size)
{
    int i = Do_bag_index(size);

    return (i >= 0 && i < (int) (sizeof bagGold / sizeof bagGold[0])) ? bagGold[i] : 1.0;
}

	/* gold (and gems at half) a character can carry without slowing */
double Do_gold_capacity(struct client_t *c)
{
    if (Do_gear_ability(c, AB_HOARD) > 0) return 1e15;	/* the Ring of Hoarding */
    return (1000.0 + MAX(0.0, c->player.level) *
	    c->realm->charstats[c->player.type].goldtote) *
	    Do_bag_gold_factor(c->bagSize);
}

const char *Do_bag_name(int size)
{
    return bags[Do_bag_index(size)].name;
}


/************************************************************************
/ naming and describing items
*************************************************************************/

	/* how strong an item's ability is, by tier */
static double Do_tier_power(int tier)
{
    switch (tier) {
    case TIER_FINE:		return 1.0;
    case TIER_MASTERWORK:	return 1.5;
    case TIER_ENCHANTED:	return 2.0;
    case TIER_ARTIFACT:		return 3.0;
    }
    return 1.0;
}

void Do_gear_name(struct gear_item_t *item, char *buffer)
{
    const char *base;

    if (item->artifact > 0 && item->artifact <= NUM_ARTIFACTS) {
	strcpy(buffer, artifacts[item->artifact].name);
	return;
    }
    if (item->ability == AB_CONQUEROR) {
	strcpy(buffer, "The Banner of the Seasons");
	return;
    }

    base = baseNames[item->slot][MAX(0, MIN(7, item->base))];

    buffer[0] = '\0';
    if (tierNames[item->tier][0] != '\0') {
	strcat(buffer, tierNames[item->tier]);
	strcat(buffer, " ");
    }
    strcat(buffer, base);

    if (item->ability != AB_NONE && abilitySuffix[item->ability][0] != '\0') {
	strcat(buffer, " ");
	strcat(buffer, abilitySuffix[item->ability]);
    }
}

	/* short name for a 20-character button */
static void Do_gear_label(struct gear_item_t *item, char *buffer)
{
    char name[SZ_LINE];

    if (item->artifact > 0) {
	Do_gear_name(item, name);
	if (strncmp(name, "The ", 4) == 0) {
	    memmove(name, name + 4, strlen(name + 4) + 1);
	}
    }
    else {
	strcpy(name, baseNames[item->slot][MAX(0, MIN(7, item->base))]);
    }

    snprintf(buffer, SZ_LABEL - 1, "%.19s\n", name);
}

	/* "+120 energy, +3 damage" */
static void Do_gear_stats(struct gear_item_t *item, char *buffer)
{
    char part[64];

    buffer[0] = '\0';

    if (item->shield >= 1) {
	sprintf(part, "+%.0f energy", item->shield);
	strcat(buffer, part);
    }
    if (item->sword >= 1) {
	sprintf(part, "%s+%.0f damage", buffer[0] ? ", " : "", item->sword);
	strcat(buffer, part);
    }
    if (item->quicksilver >= 1) {
	sprintf(part, "%s+%.0f quicksilver", buffer[0] ? ", " : "",
		item->quicksilver);
	strcat(buffer, part);
    }
    if (buffer[0] == '\0') {
	strcpy(buffer, "no bonus");
    }
}

	/* full one-line description */
void Do_gear_describe(struct gear_item_t *item, char *buffer)
{
    char name[SZ_LINE], stats[SZ_LINE];

    Do_gear_name(item, name);
    Do_gear_stats(item, stats);

    sprintf(buffer, "%s (%s) %s", name, slotNames[item->slot], stats);

    if (item->ability != AB_NONE) {
	strcat(buffer, " - ");
	strcat(buffer, abilityNames[item->ability]);
	strcat(buffer, ": ");
	strcat(buffer, abilityHelp[item->ability]);
    }
    if (item->ability2 != AB_NONE) {
	strcat(buffer, "; ");
	strcat(buffer, abilityNames[item->ability2]);
	strcat(buffer, ": ");
	strcat(buffer, abilityHelp[item->ability2]);
    }
}


/************************************************************************
/ making items
*************************************************************************/

	/* fill in the stats for an item of this slot and quality */
/*
 * How hard an average monster hits at a circle.  Measured from the monster
 * table and the encounter rules (the Dead Marshes are smoothed over, and
 * Gorgoroth at circle 25 is where monsters get much stronger).  Gear is
 * scaled by this so it keeps pace as players move out.
 */
static double Do_gear_threat_old(double circle)
{
    static const double at[][2] = {
	{ 1, 7 }, { 5, 31 }, { 10, 70 }, { 15, 200 }, { 20, 400 },
	{ 25, 1000 }, { 30, 1450 }, { 40, 2250 }, { 50, 2900 },
	{ 75, 4400 }, { 100, 5800 }
    };
    int n = (int) (sizeof(at) / sizeof(at[0])), i;
    double c = MAX(1.0, circle);

    if (c >= at[n - 1][0]) {
	return at[n - 1][1] * c / at[n - 1][0];
    }

    for (i = 1; i < n; i++) {
	if (c <= at[i][0]) {
	    return at[i - 1][1] + (at[i][1] - at[i - 1][1]) *
		    (c - at[i - 1][0]) / (at[i][0] - at[i - 1][0]);
	}
    }

    return at[n - 1][1];
}

/*
 * How hard an average monster hits, and how much energy it has, at each
 * circle: measured from the monster table with the encounter rules of
 * Do_monster (which monsters a circle picks from, size = circle, strength
 * (1 + size/2) x base, energy size x base).  Never lower than a nearer
 * circle (the Dead Marshes' weaker monsters don't make weaker gear), and
 * from circle 36, where every monster can appear, it grows with size.
 * Armor follows the hits; weapons follow the energy, so a weapon from a
 * circle kills there in about as many blows as earlier ones did nearer in.
 */
static double gearHit[37], gearEnergy[37], gearHitBase, gearEnergyBase;
static int gearMeasured = FALSE;

static void Do_gear_measure(struct realm_t *realm)
{
    int c, i, j, t, G = 20;
    double hit, en, best = 0, bestEn = 0;

    for (c = 1; c <= 36; c++) {
	hit = en = 0;
	for (i = 0; i < G; i++) for (j = 0; j < G; j++) {
	    t = Do_circle_monster(c, (i + 0.5) / G, (j + 0.5) / G);
	    hit += ((1.0 + c / 2.0) * realm->monster[t].strength + 1.0) / 2.0;
	    en += c * realm->monster[t].energy;
	}
	best = MAX(best, hit / (G * G));
	bestEn = MAX(bestEn, en / (G * G));
	gearHit[c] = best;
	gearEnergy[c] = bestEn;
    }
	/* circle 36 on: every non-water monster (14 to 99), sized to the circle */
    hit = en = 0;
    for (t = 14; t < 100; t++) {
	hit += realm->monster[t].strength;
	en += realm->monster[t].energy;
    }
    gearHitBase = hit / 86.0;
    gearEnergyBase = en / 86.0;
    gearMeasured = TRUE;
}

static double Do_gear_table(struct realm_t *realm, double circle, int energy)
{
    double c = MAX(1.0, circle), lo, hi, f;
    int k;

    if (!gearMeasured) Do_gear_measure(realm);
    if (c >= 36) {
	return energy ? MAX(gearEnergy[36], c * gearEnergyBase) :
		MAX(gearHit[36], ((1.0 + c / 2.0) * gearHitBase + 1.0) / 2.0);
    }
    k = (int) floor(c);
    f = c - k;
    lo = energy ? gearEnergy[k] : gearHit[k];
    hi = energy ? gearEnergy[k + 1] : gearHit[k + 1];
    return lo + (hi - lo) * f;
}

static struct realm_t *gearRealm = NULL;

double Do_gear_threat(double circle)
{
    /* never below the old table, so no gear comes out weaker than before */
    return gearRealm ? MAX(Do_gear_threat_old(circle), Do_gear_table(gearRealm, circle, FALSE)) :
	    Do_gear_threat_old(circle);
}

double Do_gear_monster_energy(struct client_t *c, double circle)
{
    gearRealm = c->realm;
    return Do_gear_table(c->realm, circle, TRUE);
}

	/* bonuses for an item of a slot, circle and quality */
#define GEAR_VERSION	2	/* gear saved before this is rescaled on load */
#define WEAPON_SHARE	(0.2 / 1.4)	/* weapon damage per point of quality, as a share of monster energy */
static void Do_gear_set_stats(struct gear_item_t *item, double circle,
	double q)
{
    double c = MAX(1.0, circle), threat = Do_gear_threat(c);
    double energy = gearRealm ? Do_gear_table(gearRealm, c, TRUE) : 10.0 * threat;

    item->shield = item->sword = item->quicksilver = 0;
    item->circle = (short) MIN(30000.0, floor(c));

    switch (item->slot) {
    case SLOT_WEAPON:
	    /* damage on every blow: a Masterwork weapon a fifth of an
	       average monster's energy at its circle */
	item->sword = MAX(1, floor(WEAPON_SHARE * energy * q));
	break;
    case SLOT_ARMOR:
	item->shield = MAX(1, floor(5.5 * threat * q));
	break;
    case SLOT_HELM:
	item->shield = MAX(1, floor(2.0 * threat * q));
	break;
    case SLOT_BOOTS:
	item->quicksilver = MAX(1, floor(0.6 * MAX(1.0, c - 3.0) * q));
	break;
    case SLOT_TRINKET:
	item->shield = floor(0.8 * threat * q);
	item->sword = floor(0.1 * WEAPON_SHARE * energy * q);
	break;
    }
}

static void Do_gear_roll_stats(struct gear_item_t *item, double circle,
	double quality)
{
    double c = MAX(1.0, circle);

    Do_gear_set_stats(item, c, quality * (0.85 + RND() * 0.3));

	/* the base name tracks how far out it was made */
    item->base = MIN(7, (int) (log(c) / log(2.2) + RND() * 1.5));
}

static double Do_tier_quality(int tier)
{
    switch (tier) {
    case TIER_COMMON:		return 0.7;
    case TIER_FINE:		return 1.0;
    case TIER_MASTERWORK:	return 1.4;
    case TIER_ENCHANTED:	return 1.9;
    case TIER_ARTIFACT:		return 2.6;
    }
    return 0.7;
}

	/* pick a tier; better is likelier farther out */
static int Do_gear_roll_tier(double circle, int minTier)
{
    double r = RND() - MIN(0.25, circle / 160.0);
    int tier;

    if (r < 0.04)
	tier = TIER_ENCHANTED;
    else if (r < 0.18)
	tier = TIER_MASTERWORK;
    else if (r < 0.50)
	tier = TIER_FINE;
    else
	tier = TIER_COMMON;

    return MAX(tier, minTier);
}

static const int combatAbilities[] = {
    AB_EVASION, AB_LEECH, AB_SWIFTNESS, AB_CRUSHING, AB_THORNS,
    AB_FORTUNE, AB_WARDING, AB_MENDING, AB_PROSPECTING
};
static const int magicAbilities[] = {
    AB_INSIGHT, AB_ARCANA, AB_WELLSPRING, AB_FOCUS
};

	/* an ability for a slot; r1 and r2 are random numbers in [0, 1) */
static int Do_pick_ability(int slot, double r1, double r2)
{
	/* helms and trinkets carry magic half the time */
    if ((slot == SLOT_HELM || slot == SLOT_TRINKET) && r1 < 0.5) {
	return magicAbilities[(int) (r2 * 4) % 4];
    }

    return combatAbilities[(int) (r2 * NUM_COMMON_ABILITIES) %
	    NUM_COMMON_ABILITIES];
}

static int Do_gear_roll_ability(int tier, int slot)
{
    if (tier == TIER_COMMON || tier == TIER_WORN) {
	return AB_NONE;
    }
    if (tier == TIER_FINE && RND() > 0.4) {
	return AB_NONE;
    }

    return Do_pick_ability(slot, RND(), RND());
}

	/* a random item; slot < 0 means any slot */
void Do_gear_make(struct gear_item_t *item, int slot, double circle,
	int minTier)
{
    memset(item, 0, sizeof(*item));

    item->slot = slot >= 0 ? slot : (int) (RND() * NUM_SLOTS) % NUM_SLOTS;
    item->tier = Do_gear_roll_tier(circle, minTier);
    item->ability = Do_gear_roll_ability(item->tier, item->slot);
    Do_gear_roll_stats(item, circle, Do_tier_quality(item->tier));
}

	/* an item for a slot with a given bonus (old treasure finds) */
static void Do_gear_make_valued(struct gear_item_t *item, int slot,
	double value, double circle)
{
    Do_gear_make(item, slot, circle, TIER_COMMON);

    item->shield = item->sword = item->quicksilver = 0;
    if (slot == SLOT_WEAPON)
	item->sword = MAX(1, floor(value));
    else if (slot == SLOT_BOOTS)
	item->quicksilver = MAX(1, floor(value));
    else
	item->shield = MAX(1, floor(value));
}


/************************************************************************
/ storage
*************************************************************************/

static void Do_gear_save_locked(struct client_t *c)
{
    FILE *in_file, *out_file;
    char line[SZ_LINE], name[SZ_NAME + 1], error_msg[SZ_ERROR_MESSAGE];
    int i;

    if ((out_file = fopen(TEMP_GEAR_FILE, "w")) == NULL) {
	sprintf(error_msg, "[%s] fopen of %s failed in Do_gear_save: %s\n",
		c->connection_id, TEMP_GEAR_FILE, strerror(errno));
	Do_log_error(error_msg);
	return;
    }

	/* keep everyone else's gear */
    if ((in_file = fopen(GEAR_FILE, "r")) != NULL) {
	while (fgets(line, sizeof(line), in_file) != NULL) {
	    if (sscanf(line, "%32s", name) == 1 &&
		    strcmp(name, c->player.lcname) != 0) {
		fputs(line, out_file);
	    }
	}
	fclose(in_file);
    }

    for (i = 0; i < c->numGear; i++) {
	struct gear_item_t *g = &c->gear[i];
	fprintf(out_file, "%s %d %d %d %d %d %d %d %.0f %.0f %.0f %d %d\n",
		c->player.lcname, g->slot, g->equipped, g->tier, g->base,
		g->ability, g->ability2, g->artifact, g->shield, g->sword,
		g->quicksilver, g->circle, GEAR_VERSION);
    }

	/* the bag (slot -2), which also marks the character as converted */
    fprintf(out_file, "%s -2 %d 0 0 0 0 0 0 0 0\n", c->player.lcname,
	    c->bagSize);

    fclose(out_file);
    rename(TEMP_GEAR_FILE, GEAR_FILE);
}

void Do_gear_save(struct client_t *c)
{
    if (!c->gearLoaded) {
	return;
    }

    Do_lock_mutex(&c->realm->gear_lock);
    Do_gear_save_locked(c);
    Do_unlock_mutex(&c->realm->gear_lock);
}

	/* read the artifact holders */
void Do_load_artifacts(struct realm_t *the_realm)
{
    FILE *the_file;
    char line[SZ_LINE], holder[SZ_NAME + 1];
    int id;
    long seen;

    memset(the_realm->artifactHolder, 0, sizeof(the_realm->artifactHolder));
    memset(the_realm->artifactSeen, 0, sizeof(the_realm->artifactSeen));

    if ((the_file = fopen(ARTIFACT_FILE, "r")) == NULL) {
	return;
    }

    while (fgets(line, sizeof(line), the_file) != NULL) {
	if (sscanf(line, "%d %32s %ld", &id, holder, &seen) == 3 &&
		id >= 1 && id <= NUM_ARTIFACTS && strcmp(holder, "-") != 0) {
	    strcpy(the_realm->artifactHolder[id], holder);
	    the_realm->artifactSeen[id] = (time_t) seen;
	}
    }

    fclose(the_file);
}

	/* call with gear_lock held */
static void Do_save_artifacts_locked(struct realm_t *the_realm)
{
    FILE *the_file;
    int id;

    if ((the_file = fopen(TEMP_ARTIFACT_FILE, "w")) == NULL) {
	return;
    }

    for (id = 1; id <= NUM_ARTIFACTS; id++) {
	fprintf(the_file, "%d %s %ld\n", id,
		the_realm->artifactHolder[id][0] ? the_realm->artifactHolder[id]
		: "-", (long) the_realm->artifactSeen[id]);
    }

    fclose(the_file);
    rename(TEMP_ARTIFACT_FILE, ARTIFACT_FILE);
}

	/* artifacts whose holder has vanished go back to the realm */
static void Do_expire_artifacts_locked(struct realm_t *the_realm)
{
    time_t limit = time(NULL) - ARTIFACT_IDLE_DAYS * 86400;
    int id, changed = FALSE;
    char message[SZ_LINE];

    for (id = 1; id <= NUM_ARTIFACTS; id++) {
	if (the_realm->artifactHolder[id][0] &&
		the_realm->artifactSeen[id] < limit) {

	    sprintf(message, "%s has not been seen for %d days; %s returns to the realm.\n",
		    the_realm->artifactHolder[id], ARTIFACT_IDLE_DAYS,
		    artifacts[id].name);
	    Do_log(EVENT_LOG, message);

	    the_realm->artifactHolder[id][0] = '\0';
	    changed = TRUE;
	}
    }

    if (changed) {
	Do_save_artifacts_locked(the_realm);
    }
}

	/* forget items that are artifacts this character no longer holds */
static void Do_gear_drop_lost_artifacts(struct client_t *c)
{
    int i, j = 0;

    for (i = 0; i < c->numGear; i++) {
	struct gear_item_t *g = &c->gear[i];

	if (g->artifact > 0 && (g->artifact > NUM_ARTIFACTS ||
		strcmp(c->realm->artifactHolder[g->artifact],
		c->player.lcname) != 0)) {
	    continue;
	}
	c->gear[j++] = *g;
    }

    c->numGear = j;
}

/*
 * Upgrade an item saved before gear scaled with monster strength (an
 * 11-column line).  Its circle is worked out from its bonus under the old
 * formula and its tier, then its bonuses are scaled to the new formula at
 * that circle, keeping its luck of the roll.  Worn gear (the original
 * shield/sword/quicksilver) and boots are unchanged, and nothing goes down.
 * Returns TRUE if the item changed.
 */
static int Do_gear_upgrade_old(struct gear_item_t *g)
{
    double q, circle, threat, before = g->shield + g->sword;

    if (g->tier == TIER_WORN || g->slot == SLOT_BOOTS) {
	return FALSE;
    }

    q = Do_tier_quality(g->tier);

	/* old formulas: weapon 1.6C, armor 40C, helm 15C, trinket 6C / 0.3C */
    switch (g->slot) {
    case SLOT_WEAPON:  circle = g->sword / (1.6 * q); break;
    case SLOT_ARMOR:   circle = g->shield / (40.0 * q); break;
    case SLOT_HELM:    circle = g->shield / (15.0 * q); break;
    default:           circle = g->shield >= 1 ? g->shield / (6.0 * q) :
			       g->sword / (0.3 * q);
    }

    circle = MAX(1.0, floor(circle + 0.5));
    threat = Do_gear_threat_old(circle);	/* the next step rescales to the real monsters */
    g->circle = (short) MIN(30000.0, circle);

    switch (g->slot) {
    case SLOT_WEAPON:
	g->sword = floor(g->sword * MAX(1.0, 0.4 * threat / (1.6 * circle)));
	break;
    case SLOT_ARMOR:
	g->shield = floor(g->shield * MAX(1.0, 5.5 * threat / (40.0 * circle)));
	break;
    case SLOT_HELM:
	g->shield = floor(g->shield * MAX(1.0, 2.0 * threat / (15.0 * circle)));
	break;
    default:
	g->shield = floor(g->shield * MAX(1.0, 0.8 * threat / (6.0 * circle)));
	g->sword = floor(g->sword * MAX(1.0, 0.08 * threat / (0.3 * circle)));
    }

    return g->shield + g->sword > before;
}

/*
 * Gear made before it followed the real monsters (version 1): weapons
 * were 0.4 x the old threat table x quality and added only a little to
 * might; armor, helms and trinkets used the old table too.  Work out the
 * item's own roll against the old formula, and give it the new formula's
 * value with the same roll.  Nothing gets worse.
 */
static int Do_gear_upgrade_v2(struct gear_item_t *g)
{
    double q, c, oldT, newT, energy, roll, before = g->shield + g->sword;

    if (g->slot == SLOT_BOOTS || !gearRealm) {
	return FALSE;
    }
    q = Do_tier_quality(g->tier);
    c = MAX(1.0, g->circle);
    oldT = Do_gear_threat_old(c);
    newT = Do_gear_threat(c);
    energy = Do_gear_table(gearRealm, c, TRUE);

    switch (g->slot) {
    case SLOT_WEAPON:
	roll = g->sword / MAX(1.0, 0.4 * oldT * q);
	g->sword = MAX(g->sword, floor(WEAPON_SHARE * energy * q * roll));
	break;
    case SLOT_ARMOR:
	roll = g->shield / MAX(1.0, 5.5 * oldT * q);
	g->shield = MAX(g->shield, floor(5.5 * newT * q * roll));
	break;
    case SLOT_HELM:
	roll = g->shield / MAX(1.0, 2.0 * oldT * q);
	g->shield = MAX(g->shield, floor(2.0 * newT * q * roll));
	break;
    case SLOT_TRINKET:
	roll = g->shield / MAX(1.0, 0.8 * oldT * q);
	g->shield = MAX(g->shield, floor(0.8 * newT * q * roll));
	roll = g->sword / MAX(1.0, 0.08 * oldT * q);
	g->sword = MAX(g->sword, floor(0.1 * WEAPON_SHARE * energy * q * roll));
	break;
    }
    return g->shield + g->sword > before;
}

	/* load (or create) this character's gear when entering the realm */
void Do_gear_load(struct client_t *c)
{
    FILE *the_file;
    char line[SZ_LINE], name[SZ_NAME + 1];
    struct gear_item_t g;
    int slot, equipped, tier, base, ab, ab2, art, found = FALSE, id, fields;
    int circle, upgraded = 0, version;
    double shield, sword, quick;

    c->numGear = 0;
    c->bagSize = BACKPACK_SIZE;
    gearRealm = c->realm;	/* gear is measured against this realm's monsters */

    Do_lock_mutex(&c->realm->gear_lock);

    if ((the_file = fopen(GEAR_FILE, "r")) != NULL) {
	while (fgets(line, sizeof(line), the_file) != NULL) {

	    circle = 0;
	    version = 1;
	    fields = sscanf(line, "%32s %d %d %d %d %d %d %d %lf %lf %lf %d %d",
		    name, &slot, &equipped, &tier, &base, &ab, &ab2, &art,
		    &shield, &sword, &quick, &circle, &version);

	    if (fields < 11 || strcmp(name, c->player.lcname) != 0) {
		continue;
	    }

		/* items saved before circles were kept: assume this one */
	    if (circle <= 0) {
		circle = (int) MAX(1.0, c->player.circle);
	    }

	    found = TRUE;

		/* the bag */
	    if (slot == -2) {
		c->bagSize = MAX(BACKPACK_SIZE, MIN(MAX_BAG_SIZE, equipped));
		continue;
	    }

	    if (slot < 0 || slot >= NUM_SLOTS || c->numGear >= MAX_GEAR) {
		continue;
	    }

	    memset(&g, 0, sizeof(g));
	    g.slot = slot; g.equipped = equipped ? TRUE : FALSE;
	    g.tier = MAX(0, MIN(TIER_ARTIFACT, tier));
	    g.base = MAX(0, MIN(7, base));
	    g.ability = (ab >= 0 && ab < NUM_ABILITIES) ? ab : 0;
	    g.ability2 = (ab2 >= 0 && ab2 < NUM_ABILITIES) ? ab2 : 0;
	    g.artifact = (art >= 0 && art <= NUM_ARTIFACTS) ? art : 0;
		/* an artifact always carries its own powers */
	    if (g.artifact > 0) {
		g.ability = artifacts[g.artifact].ability;
		g.ability2 = artifacts[g.artifact].ability2;
	    }
	    g.shield = shield; g.sword = sword; g.quicksilver = quick;
	    g.circle = (short) MIN(30000, circle);

		/* saved before the rebalance: bring it up to date once */
	    if (fields == 11 && Do_gear_upgrade_old(&g)) {
		++upgraded;
	    }
		/* saved before gear followed the real monsters: rescale once */
	    if (fields < 13 && Do_gear_upgrade_v2(&g)) {
		++upgraded;
	    }

	    c->gear[c->numGear++] = g;
	}
	fclose(the_file);
    }

	/* an artifact may have been lost while away, or this holder seen */
    Do_expire_artifacts_locked(c->realm);
    for (id = 1; id <= NUM_ARTIFACTS; id++) {
	if (strcmp(c->realm->artifactHolder[id], c->player.lcname) == 0) {
	    c->realm->artifactSeen[id] = time(NULL);
	}
    }
    Do_save_artifacts_locked(c->realm);
    Do_gear_drop_lost_artifacts(c);

	/* first time: turn old shield/sword/quicksilver into worn gear */
    if (!found) {

	if (c->player.sword >= 1) {
	    memset(&g, 0, sizeof(g));
	    g.slot = SLOT_WEAPON; g.equipped = TRUE; g.tier = TIER_WORN;
	    g.circle = (short) MAX(1.0, c->player.circle);
	    g.base = MIN(7, (int) (log(MAX(1.0, c->player.circle)) / log(2.2)));
	    g.sword = floor(c->player.sword);
	    c->gear[c->numGear++] = g;
	}
	if (c->player.shield >= 1) {
	    memset(&g, 0, sizeof(g));
	    g.slot = SLOT_ARMOR; g.equipped = TRUE; g.tier = TIER_WORN;
	    g.circle = (short) MAX(1.0, c->player.circle);
	    g.base = MIN(7, (int) (log(MAX(1.0, c->player.circle)) / log(2.2)));
	    g.shield = floor(c->player.shield);
	    c->gear[c->numGear++] = g;
	}
	if (c->player.quicksilver >= 1) {
	    memset(&g, 0, sizeof(g));
	    g.slot = SLOT_BOOTS; g.equipped = TRUE; g.tier = TIER_WORN;
	    g.circle = (short) MAX(1.0, c->player.circle);
	    g.base = MIN(7, (int) (log(MAX(1.0, c->player.circle)) / log(2.2)));
	    g.quicksilver = floor(c->player.quicksilver);
	    c->gear[c->numGear++] = g;
	}
    }

    c->gearLoaded = TRUE;
    Do_gear_save_locked(c);

    Do_unlock_mutex(&c->realm->gear_lock);

    if (upgraded) {
	    /* save at once: the version mark keeps it from being redone */
	Do_gear_save(c);
	Do_send_chat_self(c, "The realm's smiths have reforged your gear to match the dangers of the deeper circles.  Check it with /gear.\n");
    }
}


/************************************************************************
/ applying gear to the character
*************************************************************************/

	/* recompute shield/sword/quicksilver from equipped gear.
	   playerChoice: the player changed their own gear, so energy moves by
	   the change in shield either way (no healing by swapping armor off
	   and on).  Otherwise (wear, theft, Cerberus) energy is only capped. */
void Do_gear_apply(struct client_t *c, int playerChoice)
{
    double shield = 0, sword = 0, quick = 0, energy;
    struct gear_item_t regalia;
    char name[SZ_LINE];
    int i;

    for (i = 0; i < c->numGear; i++) {
	if (c->gear[i].equipped) {
	    shield += c->gear[i].shield;
	    sword += c->gear[i].sword;
	    quick += c->gear[i].quicksilver;
	}
    }

	/* a ruler's regalia, worn on top of their gear */
    if (Do_regalia(c, &regalia, name)) {
	shield += regalia.shield;
	sword += regalia.sword;
	quick += regalia.quicksilver;
    }

    energy = c->player.energy;
    if (playerChoice && shield != c->player.shield) {
	energy = MAX(1.0, energy + shield - c->player.shield);
    }

    Do_energy(c, energy, c->player.max_energy, shield, c->battle.force_field,
	    FALSE);
    Do_strength(c, c->player.max_strength, sword, c->battle.strengthSpell,
	    FALSE);
    Do_speed(c, c->player.max_quickness, quick, c->battle.speedSpell, FALSE);

    Do_gear_send(c);
}

	/* total strength of an ability over equipped gear */
double Do_gear_ability(struct client_t *c, int ability)
{
    double total = 0;
    int i;

    if (!c->gearLoaded) {
	return 0;
    }

    for (i = 0; i < c->numGear; i++) {
	struct gear_item_t *g = &c->gear[i];

	if (g->equipped && (g->ability == ability || g->ability2 == ability)) {
	    total += Do_tier_power(g->tier);
	}
    }
    total += Do_regalia_ability(c, ability);

    return total;
}

	/* gear list for the browser client (other clients don't know it) */
void Do_gear_send(struct client_t *c)
{
    char line[SZ_LINE * 4], name[SZ_LINE], stats[SZ_LINE];
    struct gear_item_t regalia;
    int i, royal;

    if (!c->isBrowser || !c->gearLoaded) {
	return;
    }

    royal = Do_regalia(c, &regalia, name);
    Do_send_int(c, GEAR_PACKET);
    sprintf(line, "%d\n%s\n%d\n", c->bagSize, Do_bag_name(c->bagSize),
	    c->numGear + (royal ? 1 : 0));
    Do_send_string(c, line);

	/* the regalia comes first, as slot "Regalia" (it can't be removed) */
    if (royal) {
	Do_gear_stats(&regalia, stats);
	sprintf(line, "Regalia\n%s\n%s\n1\n%d\n%s\n%s\n", name, stats,
		TIER_ARTIFACT, abilityNames[regalia.ability],
		abilityNames[regalia.ability2]);
	Do_send_string(c, line);
    }

    for (i = 0; i < c->numGear; i++) {
	struct gear_item_t *g = &c->gear[i];

	Do_gear_name(g, name);
	Do_gear_stats(g, stats);

	sprintf(line, "%s\n%s\n%s\n%d\n%d\n%s\n%s\n", slotNames[g->slot], name,
		stats, g->equipped, g->tier,
		g->ability ? abilityNames[g->ability] : "",
		g->ability2 ? abilityNames[g->ability2] : "");
	Do_send_string(c, line);
    }

	/* autoplay judges its gear by the fight math */
    Do_autoplay_gear_values(c, "gear", c->gear, c->numGear);
}

static int Do_gear_backpack_count(struct client_t *c)
{
    int i, n = 0;

    for (i = 0; i < c->numGear; i++) {
	if (!c->gear[i].equipped) {
	    ++n;
	}
    }

    return n;
}

static int Do_gear_equipped_in(struct client_t *c, int slot)
{
    int i;

    for (i = 0; i < c->numGear; i++) {
	if (c->gear[i].equipped && c->gear[i].slot == slot) {
	    return i;
	}
    }

    return -1;
}

static void Do_gear_remove(struct client_t *c, int index)
{
    int i;

    for (i = index; i < c->numGear - 1; i++) {
	c->gear[i] = c->gear[i + 1];
    }

    --c->numGear;
}

	/* wear item at index, putting what was there into the backpack */
static void Do_gear_equip(struct client_t *c, int index)
{
    int old = Do_gear_equipped_in(c, c->gear[index].slot);

    if (old >= 0 && old != index) {
	c->gear[old].equipped = FALSE;
    }

    c->gear[index].equipped = TRUE;
}


/************************************************************************
/ artifacts
*************************************************************************/

static void Do_artifact_release(struct client_t *c, int id, char *why)
{
    char message[SZ_LINE];

    Do_lock_mutex(&c->realm->gear_lock);
    c->realm->artifactHolder[id][0] = '\0';
    Do_save_artifacts_locked(c->realm);
    Do_unlock_mutex(&c->realm->gear_lock);

    sprintf(message, "%s %s %s returns to the realm, waiting to be found.\n",
	    c->player.name, why, artifacts[id].name);
    Do_broadcast(c, message);
}

	/* try to give this character an unclaimed artifact */
static int Do_artifact_claim(struct client_t *c, struct gear_item_t *item)
{
    int candidates[NUM_ARTIFACTS], count = 0, id;

    Do_lock_mutex(&c->realm->gear_lock);

    for (id = 1; id <= NUM_ARTIFACTS; id++) {
	if (c->realm->artifactHolder[id][0] == '\0' &&
#ifdef GEAR_TEST
		TRUE) {				/* test builds: any circle */
#else
		c->player.circle >= artifacts[id].minCircle) {
#endif
	    candidates[count++] = id;
	}
    }

    if (count == 0) {
	Do_unlock_mutex(&c->realm->gear_lock);
	return FALSE;
    }

    id = candidates[(int) (RND() * count) % count];
    strcpy(c->realm->artifactHolder[id], c->player.lcname);
    c->realm->artifactSeen[id] = time(NULL);
    Do_save_artifacts_locked(c->realm);

    Do_unlock_mutex(&c->realm->gear_lock);
    Do_deeds_artifact(c, id);

    memset(item, 0, sizeof(*item));
    item->slot = artifacts[id].slot;
    item->tier = TIER_ARTIFACT;
    item->artifact = id;
    item->ability = artifacts[id].ability;
    item->ability2 = artifacts[id].ability2;
    Do_gear_roll_stats(item, MAX(c->player.circle, artifacts[id].minCircle),
	    Do_tier_quality(TIER_ARTIFACT));

    return TRUE;
}

	/* a monster steals an artifact */
static void Do_artifact_stolen(struct client_t *c)
{
    char message[SZ_LINE];
    int i, held[MAX_GEAR], count = 0, which;

    for (i = 0; i < c->numGear; i++) {
	if (c->gear[i].artifact > 0) {
	    held[count++] = i;
	}
    }

    if (count == 0) {
	return;
    }

    which = held[(int) (RND() * count) % count];

    sprintf(message, "%s snatches %s from you and vanishes!\n",
	    c->battle.opponent->name, artifacts[c->gear[which].artifact].name);
    Do_send_line(c, message);

    i = c->gear[which].artifact;
    Do_gear_remove(c, which);
    Do_gear_apply(c, FALSE);
    Do_gear_save(c);

    sprintf(message, "was robbed by %s!", c->battle.opponent->name);
    Do_artifact_release(c, i, message);

    Do_cancel_monster(c->battle.opponent);
}


/************************************************************************
/ finding gear
*************************************************************************/

/*
 * Show a found item next to what is worn in that slot and let the player
 * equip it, stow it in the backpack or leave it.
 */
void Do_gear_offer(struct client_t *c, struct gear_item_t *item)
{
    struct button_t buttons;
    char line[SZ_LINE * 2];
    long answer = -1;
    int worn, room, rc;

    Do_send_clear(c);
    Do_gear_describe(item, line);
    Do_send_line(c, "You found:\n");
    strcat(line, "\n");
    Do_send_line(c, line);

    worn = Do_gear_equipped_in(c, item->slot);
    if (worn >= 0) {
	Do_gear_describe(&c->gear[worn], line);
	Do_send_line(c, "You are wearing:\n");
	strcat(line, "\n");
	Do_send_line(c, line);
    }

    room = Do_gear_backpack_count(c) < c->bagSize;

	/* with a full backpack, Swap wears the new item and leaves the old */
    Do_clear_buttons(&buttons, 0);
    if (worn < 0 || room) {
	strcpy(buttons.button[0], "Equip\n");
    }
    if (room) {
	strcpy(buttons.button[1], "Stow\n");
    }
    strcpy(buttons.button[2], item->artifact ? "Refuse\n" : "Leave\n");
    if (worn >= 0 && !room) {
	strcpy(buttons.button[3], "Swap\n");
	Do_send_line(c, "(Your backpack is full.  Swap wears this and leaves your old one behind.)\n");
    }

    Do_autoplay_gear_values(c, "offer", item, 1);
    rc = Do_buttons(c, &answer, &buttons);

    if (rc == S_NORM && answer == 3 && worn >= 0 && !room) {

	int oldArtifact = c->gear[worn].artifact;

	c->gear[worn] = *item;
	c->gear[worn].equipped = TRUE;
	Do_gear_apply(c, TRUE);
	Do_gear_save(c);
	Do_send_clear(c);

	if (oldArtifact > 0) {
	    Do_artifact_release(c, oldArtifact, "left an artifact behind.");
	}
	return;
    }

    if (rc != S_NORM || answer == 2 || answer == 3 ||
	    (answer == 0 && worn >= 0 && !room) || (answer == 1 && !room) ||
	    c->numGear >= MAX_GEAR) {

	Do_send_clear(c);

	if (item->artifact > 0) {
	    Do_artifact_release(c, item->artifact, "left it behind.");
	}
	return;
    }

    c->gear[c->numGear] = *item;
    c->gear[c->numGear].equipped = FALSE;
    ++c->numGear;

    if (answer == 0) {
	Do_gear_equip(c, c->numGear - 1);
	Do_gear_apply(c, TRUE);
    }
    else {
	Do_gear_send(c);
    }

    Do_gear_save(c);
    Do_send_clear(c);
}

	/* after killing a monster */
/* Artifacts are rare, and come from great deeds: a nemesis slain, a region
   liberated, the throne defended, a champion beaten, a quest done.  The
   whole realm hears what it was for. */
int Do_gear_artifact_chance(struct client_t *c, double chance, const char *deed)
{
    struct gear_item_t item;
    char message[SZ_LINE];

    if (!c->gearLoaded || chance <= 0 || RND() >= chance || !Do_artifact_claim(c, &item)) {
	return FALSE;
    }
    snprintf(message, sizeof message, "%s has found %s, for %s!\n", c->player.name,
	    artifacts[item.artifact].name, deed);
    Do_broadcast(c, message);
    Do_gear_offer(c, &item);
    return TRUE;
}

void Do_gear_monster_drop(struct client_t *c)
{
    struct gear_item_t item;
    char message[SZ_LINE];

    if (!c->gearLoaded) {
	return;
    }

	/* a great deed in this fight (the living world marks it) */
    if (c->deedChance > 0) {
	double chance = c->deedChance;

	c->deedChance = 0;
	if (Do_gear_artifact_chance(c, chance, c->deedName)) {
	    return;
	}
    }

    if (c->championFight && Do_gear_artifact_chance(c, ARTIFACT_CHAMPION_CHANCE,
	    "beating a champion")) {
	return;
    }

    if (c->championFight) {
	Do_gear_make(&item, -1, c->player.circle + 2, TIER_MASTERWORK);
	Do_send_line(c, "The champion's gear is yours for the taking.\n");
	Do_more(c);
	Do_gear_offer(c, &item);
	return;
    }

	/* rulers find gear more often, and better */
    if (RND() < GEAR_DROP_CHANCE * Do_reign_gear_factor(c) *
	    Do_deeds_gear_factor(c) * Do_world_gear_factor(c)) {
	Do_gear_make(&item, -1, c->player.circle, Do_reign_min_tier(c));
	Do_gear_offer(c, &item);
    }
}

	/* treasure: an old-style find of a shield/sword/quicksilver */
void Do_gear_treasure_find(struct client_t *c, int slot, double value,
	double circle)
{
    struct gear_item_t item;
    char message[SZ_LINE];

	/* now and then, an artifact instead */
    if (RND() < ARTIFACT_TREASURE_CHANCE && Do_artifact_claim(c, &item)) {
	sprintf(message, "%s has found %s!\n", c->player.name,
		artifacts[item.artifact].name);
	Do_broadcast(c, message);
	Do_gear_offer(c, &item);
	return;
    }

	/* the old fixed bonuses can't keep up farther out: roll real gear */
    Do_gear_make(&item, slot, circle, TIER_COMMON);
    Do_gear_offer(c, &item);
}

	/* treasure from a corpse: the dead character's gear, halved */
void Do_gear_corpse_find(struct client_t *c, double sword, double shield,
	double quicksilver)
{
    struct gear_item_t item;

    if (sword >= 1) {
	Do_gear_make_valued(&item, SLOT_WEAPON, sword, c->player.circle);
	item.tier = TIER_WORN; item.ability = AB_NONE;
	Do_gear_offer(c, &item);
    }
    if (shield >= 1) {
	Do_gear_make_valued(&item, SLOT_ARMOR, shield, c->player.circle);
	item.tier = TIER_WORN; item.ability = AB_NONE;
	Do_gear_offer(c, &item);
    }
    if (quicksilver >= 1) {
	Do_gear_make_valued(&item, SLOT_BOOTS, quicksilver, c->player.circle);
	item.tier = TIER_WORN; item.ability = AB_NONE;
	Do_gear_offer(c, &item);
    }
}

	/* a quest reward */
void Do_gear_quest_reward(struct client_t *c)
{
    struct gear_item_t item;

    if (!c->gearLoaded) {
	return;
    }

    Do_gear_make(&item, -1, c->player.circle + 1,
	    RND() < 0.3 ? TIER_MASTERWORK : TIER_FINE);
    if (item.ability == AB_NONE) {
	item.ability = Do_pick_ability(item.slot, RND(), RND());
    }

    Do_send_line(c, "Your reward includes a piece of gear!\n");
    Do_more(c);
    Do_gear_offer(c, &item);
}


/************************************************************************
/ things that happen to gear
*************************************************************************/

	/* add to (enchant) the worn item for a stat; stat 0 shield 1 sword 2 quick */
void Do_gear_enchant(struct client_t *c, int stat, double amount)
{
    int slots[3][2] = { { SLOT_ARMOR, SLOT_HELM }, { SLOT_WEAPON, -1 },
	    { SLOT_BOOTS, -1 } };
    int i, index = -1;
    char name[SZ_LINE], message[SZ_LINE * 2];

    for (i = 0; i < 2 && index < 0; i++) {
	if (slots[stat][i] >= 0) {
	    index = Do_gear_equipped_in(c, slots[stat][i]);
	}
    }

    if (index < 0) {
	Do_send_line(c, "But you have nothing to enchant.\n");
	return;
    }

    if (stat == 0)
	c->gear[index].shield += floor(amount);
    else if (stat == 1)
	c->gear[index].sword += floor(amount);
    else
	c->gear[index].quicksilver += floor(amount);

    Do_gear_name(&c->gear[index], name);
    sprintf(message, "Your %s glows brighter.\n", name);
    Do_send_line(c, message);

    Do_gear_apply(c, stat == 0);
    Do_gear_save(c);
}

	/* gear wears down; stat 0 shield 1 sword 2 quick.  Artifacts don't. */
void Do_gear_wear(struct client_t *c, int stat, double factor)
{
    int i;

    for (i = 0; i < c->numGear; i++) {
	struct gear_item_t *g = &c->gear[i];

	if (!g->equipped || g->artifact > 0 || g->ability == AB_CONQUEROR) {	/* artifacts and the Banner never wear */
	    continue;
	}
	if (stat == 0)
	    g->shield = floor(g->shield * factor);
	else if (stat == 1)
	    g->sword = floor(g->sword * factor);
	else
	    g->quicksilver = floor(g->quicksilver * factor);
    }

    Do_gear_apply(c, FALSE);
    Do_gear_save(c);
}

	/* Cerberus takes all metal: worn weapon, armor and helm */
void Do_gear_cerberus(struct client_t *c)
{
    int i, id;

    for (i = c->numGear - 1; i >= 0; i--) {
	struct gear_item_t *g = &c->gear[i];

	if (g->equipped && (g->slot == SLOT_WEAPON || g->slot == SLOT_ARMOR ||
		g->slot == SLOT_HELM)) {

	    id = g->artifact;
	    Do_gear_remove(c, i);
	    if (id > 0) {
		Do_artifact_release(c, id, "lost an artifact to Cerberus!");
	    }
	}
    }

    Do_gear_apply(c, FALSE);
    Do_gear_save(c);
}

	/* the character has died for good */
void Do_gear_death(struct client_t *c)
{
    int i;

    for (i = 0; i < c->numGear; i++) {
	if (c->gear[i].artifact > 0) {
	    Do_artifact_release(c, c->gear[i].artifact, "has fallen.");
	}
    }

    c->numGear = 0;
    c->bagSize = BACKPACK_SIZE;
    Do_gear_save(c);
    c->gearLoaded = FALSE;
}

	/* the Phoenix Amulet saves its holder from death, then rests an hour */
int Do_gear_phoenix(struct client_t *c)
{
    int i;

    for (i = 0; i < c->numGear; i++) {
	if (c->gear[i].equipped && (c->gear[i].ability == AB_PHOENIX ||
		c->gear[i].ability2 == AB_PHOENIX)) {
	    if (time(NULL) < c->phoenixReady) {
		return FALSE;
	    }
	    Do_send_line(c, "The Phoenix Amulet blazes with fire and pulls you back from death!  Its fire will need an hour to return.\n");
	    Do_more(c);
	    Do_send_clear(c);
	    c->phoenixReady = time(NULL) + 3600;
	    Do_energy(c, c->player.max_energy / 2.0 + 1.0, c->player.max_energy,
		    c->player.shield, 0, FALSE);
	    return TRUE;
	}
    }
    return FALSE;
}


/************************************************************************
/ combat abilities
*************************************************************************/

static int Do_is_dragon(const char *name)
{
    return strstr(name, "Dragon") || strstr(name, "drake") ||
	    strstr(name, "Wyrm") || strstr(name, "Smaug") ||
	    strstr(name, "Scatha") || strstr(name, "Tiamat") ||
	    strstr(name, "Jabberwock");
}

	/* the player is about to hit a monster: returns adjusted damage */
double Do_gear_attack(struct client_t *c, double inflict)
{
    double p;

    if (c->battle.opponent->special_type == SM_IT_COMBAT) {
	return inflict;
    }

    if ((p = Do_gear_ability(c, AB_CRUSHING)) > 0 && RND() < 0.07 * p) {
	Do_send_line(c, "A crushing blow!\n");
	inflict *= 2.0;
    }

    if (Do_gear_ability(c, AB_DRAGONBANE) > 0 &&
	    Do_is_dragon(c->battle.opponent->realName)) {
	inflict *= 2.0;
    }
	/* Wyrmslayer: the first blow of a fight */
    if (c->firstBlow) {
	c->firstBlow = FALSE;
	if (Do_gear_ability(c, AB_FIRSTBLOOD) > 0) {
	    Do_send_line(c, "First blood! Wyrmslayer bites deep.\n");
	    inflict *= 3.0;
	}
    }

    return floor(inflict);
}

	/* after the player's hit lands */
void Do_gear_after_attack(struct client_t *c, double inflict)
{
    double p, heal;

    if (c->battle.opponent->special_type == SM_IT_COMBAT) {
	return;
    }

	/* the Phoenix Amulet: every blow restores some energy */
    if (Do_gear_ability(c, AB_RADIANCE) > 0 && inflict > 0) {
	double top = c->player.max_energy + c->player.shield;
	if (c->player.energy < top) {
	    Do_energy(c, MIN(top, c->player.energy + ceil(top * 0.04)), c->player.max_energy,
		    c->player.shield, c->battle.force_field, FALSE);
	}
    }
	/* Mourneblade: finish a monster below a fifth of its energy */
    if (Do_gear_ability(c, AB_EXECUTE) > 0 && c->battle.opponent->energy > 0 &&
	    c->battle.opponent->energy < 0.2 * c->battle.opponent->max_energy) {
	char message[SZ_LINE];
	sprintf(message, "Mourneblade finishes %s.\n", c->battle.opponent->name);
	Do_send_line(c, message);
	c->battle.opponent->energy = 0;
    }
    if ((p = Do_gear_ability(c, AB_LEECH)) > 0 && inflict > 0) {
	heal = floor(inflict * 0.08 * p);
	if (heal >= 1 && c->player.energy < c->player.max_energy +
		c->player.shield) {
		/* silent: the energy bar shows it */
	    Do_energy(c, c->player.energy + heal, c->player.max_energy,
		    c->player.shield, c->battle.force_field, FALSE);
	}
    }
}

	/* a monster is about to land a normal hit: TRUE if evaded */
int Do_gear_defend(struct client_t *c)
{
    double p, chance;
    char message[SZ_LINE];

    if (c->battle.opponent->special_type == SM_IT_COMBAT) {
	return FALSE;
    }

	/* thieves eye artifacts (morons and idiots don't notice them) */
    if (Do_gear_holds_artifact(c) &&
	    c->battle.opponent->special_type != SM_MORON &&
	    c->battle.opponent->special_type != SM_IDIOT) {
	chance = 0.002;
	if (c->battle.opponent->special_type == SM_SMEAGOL)
	    chance = 0.15;
	else if (c->battle.opponent->special_type == SM_SARUMAN)
	    chance = 0.10;
	else if (c->championFight)
	    chance = 0.03;

	if (RND() < chance) {
	    Do_artifact_stolen(c);
	    return TRUE;
	}
    }

    if ((p = Do_gear_ability(c, AB_EVASION)) > 0 &&
	    RND() < MIN(0.40, 0.05 * p)) {
	sprintf(message, "You deftly evade %s's attack!\n",
		c->battle.opponent->name);
	Do_send_line(c, message);
	return TRUE;
    }

    return FALSE;
}

	/* the Dragonscale Aegis: a monster's blow does a quarter less */
double Do_gear_soak(struct client_t *c, double inflict)
{
    if (c->battle.opponent->special_type != SM_IT_COMBAT &&
	    Do_gear_ability(c, AB_DRAGONSCALE) > 0) {
	inflict = floor(inflict * 0.75);
    }
    return inflict;
}

	/* Mourneblade: a kill restores a fifth of your energy */
void Do_gear_on_kill(struct client_t *c)
{
    double top = c->player.max_energy + c->player.shield;

    if (Do_gear_ability(c, AB_SOULREAP) > 0 && c->player.energy < top) {
	Do_send_line(c, "Mourneblade drinks its soul, and your strength returns.\n");
	Do_energy(c, MIN(top, c->player.energy + ceil(top * 0.2)), c->player.max_energy,
		c->player.shield, c->battle.force_field, FALSE);
    }
}

	/* after a monster's normal hit */
void Do_gear_after_defend(struct client_t *c, double inflict)
{
    double p, back;
    char message[SZ_LINE];

    if (c->battle.opponent->special_type == SM_IT_COMBAT) {
	return;
    }

    if ((p = Do_gear_ability(c, AB_THORNS)) > 0 && inflict > 0) {
	back = floor(inflict * 0.10 * p);
	if (back >= 1) {
	    sprintf(message, "Your thorns cut %s for %.0f damage.\n",
		    c->battle.opponent->name, back);
	    Do_send_line(c, message);
	    c->battle.opponent->energy -= back;
	}
    }
}

	/* water monsters leave the Crown of the Deep's wearer alone */
int Do_gear_seafarer(struct client_t *c)
{
    char message[SZ_LINE];

    if (c->battle.opponent->type <= 13 &&
	    Do_gear_ability(c, AB_SEAFARER) > 0) {

	sprintf(message, "%s bows before the Crown of the Deep and sinks away.\n",
		c->battle.opponent->name);
	Do_send_line(c, message);
	Do_cancel_monster(c->battle.opponent);
	return TRUE;
    }

    return FALSE;
}

int Do_gear_holds_artifact(struct client_t *c)
{
    int i;

    for (i = 0; i < c->numGear; i++) {
	if (c->gear[i].artifact > 0) {
	    return TRUE;
	}
    }

    return FALSE;
}

	/* travelling heals a little with Mending */
void Do_gear_turn(struct client_t *c)
{
    double p, heal;

    if ((p = Do_gear_ability(c, AB_MENDING)) > 0 &&
	    c->player.energy < c->player.max_energy + c->player.shield) {

	heal = ceil((c->player.max_energy + c->player.shield) * 0.015 * p);
	Do_energy(c, c->player.energy + heal, c->player.max_energy,
		c->player.shield, c->battle.force_field, FALSE);
    }

	/* Wellspring: a share of your mana limit (Do_mana caps it) */
    if ((p = Do_gear_ability(c, AB_WELLSPRING)) > 0) {
	Do_mana(c, ceil((1000.0 + c->player.level *
		c->realm->charstats[c->player.type].max_mana) * 0.005 * p), FALSE);
    }
}

	/* brains and magic level with gear: the saved values never include it */
double Do_eff_brains(struct client_t *c)
{
    double p = Do_gear_ability(c, AB_INSIGHT);

    return c->player.brains + (p > 0 ? p * (2.0 + 0.10 * c->player.brains) : 0);
}

double Do_eff_magiclvl(struct client_t *c)
{
    double p = Do_gear_ability(c, AB_ARCANA);

    return c->player.magiclvl +
	    (p > 0 ? p * (2.0 + 0.08 * c->player.magiclvl) : 0);
}

	/* what a spell really costs with Focus gear */
double Do_gear_spell_cost(struct client_t *c, double cost)
{
    return ceil(cost * MAX(0.5, 1.0 - 0.1 * Do_gear_ability(c, AB_FOCUS)) *
	    (Do_gear_ability(c, AB_MANAFONT) > 0 ? 0.5 : 1.0) *	/* the Archmage's Circlet */
	    Do_world_spell_factor());
}

double Do_gear_gold_factor(struct client_t *c)
{
    return (1.0 + 0.15 * Do_gear_ability(c, AB_FORTUNE)) *
	    (Do_gear_ability(c, AB_MIDAS) > 0 ? 2.0 : 1.0);	/* the Ring of Hoarding */
}

double Do_gear_treasure_factor(struct client_t *c)
{
    return 1.0 + 0.2 * Do_gear_ability(c, AB_PROSPECTING);
}

double Do_gear_poison_factor(struct client_t *c)
{
    if (Do_gear_ability(c, AB_TIDEWARD) > 0) return 0.0;	/* the Crown of the Deep */
    return MAX(0.2, 1.0 - 0.2 * Do_gear_ability(c, AB_WARDING));
}


/************************************************************************
/ the gear menu (Info -> Gear) and the armory
*************************************************************************/

	/* "The King's Crown (regalia): +900 energy, ...; Crushing Blows, Warding" */
static int Do_regalia_line(struct client_t *c, char *buffer)
{
    struct gear_item_t r;
    char name[SZ_LINE], stats[SZ_LINE];

    if (!Do_regalia(c, &r, name)) {
	return FALSE;
    }
    Do_gear_stats(&r, stats);
    sprintf(buffer, "%s (regalia): %s; %s, %s\n", name, stats,
	    abilityNames[r.ability], abilityNames[r.ability2]);
    return TRUE;
}

static void Do_gear_list(struct client_t *c)
{
    char line[SZ_LINE * 2];
    int i, slot, n;

    Do_send_clear(c);
    Do_send_line(c, "Equipped:\n");

    for (slot = 0; slot < NUM_SLOTS; slot++) {
	i = Do_gear_equipped_in(c, slot);
	if (i < 0) {
	    sprintf(line, "  %s: (nothing)\n", slotNames[slot]);
	}
	else {
	    sprintf(line, "  ");
	    Do_gear_describe(&c->gear[i], line + 2);
	    strcat(line, "\n");
	}
	Do_send_line(c, line);
    }
    strcpy(line, "  ");
    if (Do_regalia_line(c, line + 2)) {
	Do_send_line(c, line);
    }

    Do_send_line(c, "\n");
    n = Do_gear_backpack_count(c);
    sprintf(line, "%s (%d of %d):\n", Do_bag_name(c->bagSize), n,
	    c->bagSize);
    Do_send_line(c, line);

    for (i = 0; i < c->numGear; i++) {
	if (!c->gear[i].equipped) {
	    sprintf(line, "  ");
	    Do_gear_describe(&c->gear[i], line + 2);
	    strcat(line, "\n");
	    Do_send_line(c, line);
	}
    }
}

	/* choose an item; which: 0 backpack, 1 equipped, 2 artifacts */
static int Do_gear_choose(struct client_t *c, int which, char *prompt)
{
    struct button_t buttons;
    int matches[MAX_GEAR], count = 0, page = 0, pages, n, i;
    long answer;
    char line[SZ_LINE];

    for (i = 0; i < c->numGear; i++) {
	struct gear_item_t *g = &c->gear[i];

	if ((which == 0 && !g->equipped) || (which == 1 && g->equipped) ||
		(which == 2 && g->artifact > 0)) {
	    matches[count++] = i;
	}
    }

    if (count == 0) {
	Do_send_line(c, "There is nothing to choose.\n");
	Do_more(c);
	return -1;
    }

	/* 7 to a page, or 6 and a "More" button when there are more */
    pages = count <= 7 ? 1 : (count + 5) / 6;

    for (;;) {
	int perPage = pages == 1 ? 7 : 6;
	int first = page * perPage;

	Do_clear_buttons(&buttons, 0);

	for (n = 0; n < perPage && first + n < count; n++) {
	    Do_gear_label(&c->gear[matches[first + n]], buttons.button[n]);
	}

	if (pages > 1) {
	    strcpy(buttons.button[6], "More...\n");
	    sprintf(line, "%s(Page %d of %d.)\n", prompt, page + 1, pages);
	    Do_send_line(c, line);
	}
	else {
	    Do_send_line(c, prompt);
	}
	strcpy(buttons.button[7], "Cancel\n");

	if (Do_buttons(c, &answer, &buttons) != S_NORM || answer == 7 ||
		answer < 0) {
	    return -1;
	}

	if (pages > 1 && answer == 6) {
	    page = (page + 1) % pages;
	    continue;
	}

	if (answer >= n) {
	    return -1;
	}

	return matches[first + answer];
    }
}

static void Do_gear_offer_artifact(struct client_t *c)
{
    struct button_t buttons;
    char message[SZ_LINE];
    long answer;
    int index, id;

    index = Do_gear_choose(c, 2, "Which artifact will you offer to the Valar?\n");
    if (index < 0) {
	return;
    }

    id = c->gear[index].artifact;
    Do_send_clear(c);
    sprintf(message, "The Valar will take %s back into the realm and grant you a boon:\n",
	    artifacts[id].name);
    Do_send_line(c, message);
    Do_send_line(c, "  Might: +10% maximum strength, forever\n");
    Do_send_line(c, "  Vigor: +10% maximum energy, forever\n");
    sprintf(message, "  Riches: %.0f gold and %.0f gems\n",
	    3000.0 * (c->player.level + 5), 5.0 + floor(c->player.level / 4));
    Do_send_line(c, message);

    Do_clear_buttons(&buttons, 0);
    strcpy(buttons.button[0], "Might\n");
    strcpy(buttons.button[1], "Vigor\n");
    strcpy(buttons.button[2], "Riches\n");
    strcpy(buttons.button[7], "Keep it\n");

    if (Do_buttons(c, &answer, &buttons) != S_NORM || answer < 0 ||
	    answer > 2) {
	Do_send_clear(c);
	return;
    }

    Do_gear_remove(c, index);
    Do_gear_apply(c, TRUE);

    if (answer == 0) {
	Do_strength(c, c->player.max_strength * 1.1, c->player.sword,
		c->battle.strengthSpell, FALSE);
	Do_send_line(c, "You feel mightier.\n");
    }
    else if (answer == 1) {
	Do_energy(c, c->player.energy, c->player.max_energy * 1.1,
		c->player.shield, c->battle.force_field, FALSE);
	Do_send_line(c, "You feel more vigorous.\n");
    }
    else {
	Do_gold(c, 3000.0 * (c->player.level + 5), FALSE);
	Do_gems(c, 5.0 + floor(c->player.level / 4), FALSE);
	Do_send_line(c, "Riches rain down upon you.\n");
    }

    Do_gear_save(c);
    Do_artifact_release(c, id, "offered an artifact to the Valar.");
    Do_more(c);
}

void Do_gear_menu(struct client_t *c)
{
    struct button_t buttons;
    long answer;
    int index;

    for (;;) {

	Do_gear_list(c);

	Do_clear_buttons(&buttons, 0);
	strcpy(buttons.button[0], "Equip\n");
	strcpy(buttons.button[1], "Unequip\n");
	strcpy(buttons.button[2], "Discard\n");
	if (Do_gear_holds_artifact(c)) {
	    strcpy(buttons.button[3], "Offer Artifact\n");
	}
	strcpy(buttons.button[7], "Done\n");
#ifdef GEAR_TEST
	strcpy(buttons.button[5], "Bags\n");	/* test builds only */
	strcpy(buttons.button[6], "Armory\n");
#endif

	if (Do_buttons(c, &answer, &buttons) != S_NORM || answer == 7) {
	    Do_send_clear(c);
	    return;
	}

	switch (answer) {
#ifdef GEAR_TEST
	case 5:
	    Do_gear_bag_shop(c);
	    break;
	case 6:
	    Do_gear_armory(c, -1);
	    break;
#endif

	case 0:
	    index = Do_gear_choose(c, 0, "Which item do you want to wear?\n");
	    if (index >= 0) {
		Do_gear_equip(c, index);
		Do_gear_apply(c, TRUE);
		Do_gear_save(c);
	    }
	    break;

	case 1:
	    if (Do_gear_backpack_count(c) >= c->bagSize) {
		Do_send_line(c, "Your backpack is full.\n");
		Do_more(c);
		break;
	    }
	    index = Do_gear_choose(c, 1, "Which item do you want to take off?\n");
	    if (index >= 0) {
		c->gear[index].equipped = FALSE;
		Do_gear_apply(c, TRUE);
		Do_gear_save(c);
	    }
	    break;

	case 2:
	    index = Do_gear_choose(c, 0, "Which item do you want to throw away?\n");
	    if (index >= 0) {
		if (c->gear[index].artifact > 0) {
		    int id = c->gear[index].artifact;
		    Do_gear_remove(c, index);
		    Do_artifact_release(c, id, "cast aside an artifact.");
		}
		else {
		    Do_gear_remove(c, index);
		}
		Do_gear_send(c);
		Do_gear_save(c);
	    }
	    break;

	case 3:
	    if (Do_gear_holds_artifact(c)) {
		Do_gear_offer_artifact(c);
	    }
	    break;
	}
    }
}

	/* value of an item in gold, matching the old shop prices */
/*
 * What the armory pays for an item: about half an average treasure find
 * of gold at the circle it came from (a find is about 30 + 36 x
 * (circle - 1)), more for better tiers.  Selling drops then adds roughly a
 * third to a player's gold, not a flood.  The armory sells for
 * GEAR_MARKUP times what it pays.
 */
#define GEAR_MARKUP	16.0

static double Do_gear_sell_price(struct gear_item_t *g)
{
    static const double tierMult[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 0.0 };
    static const double slotMult[] = { 1.0, 1.0, 0.6, 1.0, 0.6 };
    double price, circle = MAX(1, g->circle);

    price = (15.0 + 18.0 * (circle - 1.0)) *
	    tierMult[MAX(0, MIN(TIER_ARTIFACT, g->tier))] *
	    slotMult[MAX(0, MIN(NUM_SLOTS - 1, g->slot))];

    if (g->ability != AB_NONE) {
	price *= 1.3;
    }

    return floor(MAX(1.0, price));
}

static double Do_gear_buy_price(struct gear_item_t *g)
{
    return floor(Do_gear_sell_price(g) * GEAR_MARKUP);
}

	/* the armory at a trading post: slot < 0 shows every slot */
void Do_gear_armory(struct client_t *c, int slot)
{
    struct button_t buttons;
    struct gear_item_t stock[6];
    char line[SZ_LINE * 2];
    double price[6];
    long answer;
    int n, i, index;
    unsigned int seed, state;

	/* the same stock all day for this circle */
    seed = (unsigned int) (time(NULL) / 86400) * 7919 +
	    (unsigned int) c->player.circle * 104729 + (slot + 2) * 31;

    for (;;) {

	Do_send_clear(c);
	Do_send_line(c, "The armorer shows you their wares:\n");

	state = seed;
	for (n = 0; n < 6; n++) {
	    int s = slot >= 0 ? slot : rand_r(&state) % NUM_SLOTS;
	    int t = (n < 2) ? TIER_COMMON : (n < 5 ? TIER_FINE : TIER_MASTERWORK);

		/* deterministic stats for the day's stock */
	    memset(&stock[n], 0, sizeof(stock[n]));
	    stock[n].slot = s;
	    stock[n].tier = t;
	    stock[n].ability = (t >= TIER_FINE && rand_r(&state) % 3 == 0) ||
		    t == TIER_MASTERWORK ?
		    Do_pick_ability(s, (rand_r(&state) % 1000) / 1000.0,
		    (rand_r(&state) % 1000) / 1000.0) : AB_NONE;
	    {
		double q = Do_tier_quality(t) * (0.9 + (rand_r(&state) % 20) / 100.0);
		double cc = MAX(1.0, c->player.circle);
		stock[n].base = MIN(7, (int) (log(cc) / log(2.2)) + rand_r(&state) % 2);
		Do_gear_set_stats(&stock[n], cc, q);
	    }
	    price[n] = floor(Do_gear_buy_price(&stock[n]) * Do_world_price_factor(c));

	    Do_gear_describe(&stock[n], line);
	    sprintf(line + strlen(line), " - %.0f gold\n", price[n]);
	    Do_send_line(c, line);
	}

	sprintf(line, "You have %.0f gold.\n", c->player.gold);
	Do_send_line(c, line);

	Do_clear_buttons(&buttons, 0);
	for (i = 0; i < 6; i++) {
	    Do_gear_label(&stock[i], buttons.button[i]);
	}
	strcpy(buttons.button[6], "Sell Gear\n");
	strcpy(buttons.button[7], "Leave\n");
	Do_autoplay_gear_values(c, "armory", stock, 6);

	if (Do_buttons(c, &answer, &buttons) != S_NORM || answer == 7) {
	    Do_send_clear(c);
	    return;
	}

	if (answer == 6) {
	    index = Do_gear_choose(c, 0, "What will you sell? (Only backpack items can be sold.)\n");
	    if (index >= 0 && c->gear[index].artifact == 0) {
		double paid = Do_gear_sell_price(&c->gear[index]);
		sprintf(line, "The armorer pays you %.0f gold.\n", paid);
		Do_send_line(c, line);
		Do_gold(c, paid, FALSE);
		Do_gear_remove(c, index);
		Do_gear_send(c);
		Do_gear_save(c);
		Do_more(c);
	    }
	    else if (index >= 0) {
		Do_send_line(c, "No armorer would dare buy an artifact.\n");
		Do_more(c);
	    }
	    continue;
	}

	if (answer < 0 || answer > 5) {
	    continue;
	}

	if (price[answer] > c->player.gold) {
	    Do_send_line(c, "You can't afford that.\n");
	    Do_more(c);
	    continue;
	}

	if (Do_gear_backpack_count(c) >= c->bagSize &&
		Do_gear_equipped_in(c, stock[answer].slot) >= 0) {
	    Do_send_line(c, "Your backpack is full.  Sell or discard something first.\n");
	    Do_more(c);
	    continue;
	}

	Do_gold(c, -price[answer], FALSE);
	Do_royal_tithe(c, price[answer]);
	sprintf(line, "%s, %s, purchased gear for %.0f\n", c->player.lcname,
		c->realm->charstats[c->player.type].class_name, price[answer]);
	Do_log(PURCHASE_LOG, line);

	c->gear[c->numGear] = stock[answer];
	c->gear[c->numGear].equipped = FALSE;
	++c->numGear;
	Do_gear_equip(c, c->numGear - 1);
	Do_gear_apply(c, TRUE);
	Do_gear_save(c);

	Do_send_line(c, "You put it on at once.\n");
	Do_more(c);
    }
}

	/* the bag maker at a trading post */
void Do_gear_bag_shop(struct client_t *c)
{
    struct button_t buttons;
    char line[SZ_LINE];
    long answer;
    int now, next;
    double bagPrice;

    for (;;) {

	Do_send_clear(c);
	now = Do_bag_index(c->bagSize);

	sprintf(line, "You carry a %s, which holds %d items.\n",
		bags[now].name, bags[now].size);
	Do_send_line(c, line);

	if (now >= NUM_BAGS - 1) {
	    Do_send_line(c, "The bag maker bows.  'There is no finer bag in all the realm.'\n");
	    Do_more(c);
	    Do_send_clear(c);
	    return;
	}

	next = now + 1;
	bagPrice = floor(bags[next].price * Do_world_price_factor(c));
	sprintf(line, "The bag maker offers a %s, holding %d items, for %.0f gold. It lets you carry %g times as much gold as a belt pouch (you now carry %g times).\n",
		bags[next].name, bags[next].size, bagPrice, bagGold[next], bagGold[now]);
	Do_send_line(c, line);
	sprintf(line, "Your items move into the new bag.  You have %.0f gold.\n",
		c->player.gold);
	Do_send_line(c, line);

	Do_clear_buttons(&buttons, 0);
	strcpy(buttons.button[0], "Upgrade\n");
	strcpy(buttons.button[7], "Leave\n");

	if (Do_buttons(c, &answer, &buttons) != S_NORM || answer != 0) {
	    Do_send_clear(c);
	    return;
	}

	if (bagPrice > c->player.gold) {
	    Do_send_line(c, "You can't afford it yet.\n");
	    Do_more(c);
	    continue;
	}

	Do_gold(c, -bagPrice, FALSE);
	Do_royal_tithe(c, bagPrice);
	c->bagSize = bags[next].size;
	Do_check_weight(c);	/* a bigger bag carries more gold */

	sprintf(line, "%s, %s, bought a %s for %.0f\n", c->player.lcname,
		c->realm->charstats[c->player.type].class_name,
		bags[next].name, bagPrice);
	Do_log(PURCHASE_LOG, line);

	Do_gear_send(c);
	Do_gear_save(c);

	sprintf(line, "You shoulder your new %s.\n", bags[next].name);
	Do_send_line(c, line);
	Do_more(c);
    }
}

/*
 * The browser client's gear panel: click an item in the bag to wear it,
 * or a worn item to put it in the bag.  The client sends
 * "<e|u> <index> <slot> <equipped>" so a stale click is ignored.
 */
void Do_gear_click(struct client_t *c, char *request)
{
    char action, name[SZ_LINE], line[SZ_LINE * 2];
    int index, slot, equipped;

    if (!c->gearLoaded || sscanf(request, " %c %d %d %d", &action, &index,
	    &slot, &equipped) != 4) {
	return;
    }

    if (c->inCombat) {
	Do_send_chat_self(c, "You can't change gear in the middle of a fight.\n");
	Do_gear_send(c);
	return;
    }

	/* the list must still match what the player clicked */
    if (index < 0 || index >= c->numGear || c->gear[index].slot != slot ||
	    c->gear[index].equipped != (equipped ? TRUE : FALSE)) {
	Do_gear_send(c);
	return;
    }

    Do_gear_name(&c->gear[index], name);

    if (action == 'e' && !c->gear[index].equipped) {
	Do_gear_equip(c, index);
	Do_gear_apply(c, TRUE);
	sprintf(line, "You put on %s.\n", name);
    }
    else if (action == 'u' && c->gear[index].equipped) {
	if (Do_gear_backpack_count(c) >= c->bagSize) {
	    Do_send_chat_self(c, "Your bag is full.\n");
	    Do_gear_send(c);
	    return;
	}
	c->gear[index].equipped = FALSE;
	Do_gear_apply(c, TRUE);
	sprintf(line, "You take off %s.\n", name);
    }
    else {
	Do_gear_send(c);
	return;
    }

    Do_gear_save(c);
    Do_send_chat_self(c, line);
}

/************************************************************************
/ wizard tools (Administrate > Realm Tools); these run in the target
/ player's own thread, delivered as events
*************************************************************************/

const char *Do_artifact_name(int id)
{
    return (id >= 1 && id <= NUM_ARTIFACTS) ? artifacts[id].name : "";
}

	/* who holds an artifact ("" if lost in the realm) */
void Do_artifact_holder(struct client_t *c, int id, char *holder)
{
    holder[0] = '\0';
    if (id < 1 || id > NUM_ARTIFACTS) {
	return;
    }

    Do_lock_mutex(&c->realm->gear_lock);
    strcpy(holder, c->realm->artifactHolder[id]);
    Do_unlock_mutex(&c->realm->gear_lock);
}

	/* return an artifact whose holder is offline to the realm */
void Do_artifact_release_offline(struct client_t *c, int id)
{
    char message[SZ_LINE];

    if (id < 1 || id > NUM_ARTIFACTS) {
	return;
    }

    Do_lock_mutex(&c->realm->gear_lock);
    c->realm->artifactHolder[id][0] = '\0';
    Do_save_artifacts_locked(c->realm);
    Do_unlock_mutex(&c->realm->gear_lock);

    sprintf(message, "The Wizards have returned %s to the realm, waiting to be found.\n",
	    artifacts[id].name);
    Do_broadcast(c, message);
}

	/* claim one particular artifact, if it is lost in the realm */
static int Do_artifact_claim_id(struct client_t *c, int id,
	struct gear_item_t *item)
{
    if (id < 1 || id > NUM_ARTIFACTS) {
	return FALSE;
    }

    Do_lock_mutex(&c->realm->gear_lock);

    if (c->realm->artifactHolder[id][0] != '\0') {
	Do_unlock_mutex(&c->realm->gear_lock);
	return FALSE;
    }

    strcpy(c->realm->artifactHolder[id], c->player.lcname);
    c->realm->artifactSeen[id] = time(NULL);
    Do_save_artifacts_locked(c->realm);

    Do_unlock_mutex(&c->realm->gear_lock);

    memset(item, 0, sizeof(*item));
    item->slot = artifacts[id].slot;
    item->tier = TIER_ARTIFACT;
    item->artifact = id;
    item->ability = artifacts[id].ability;
    item->ability2 = artifacts[id].ability2;
    Do_gear_roll_stats(item, MAX(c->player.circle, artifacts[id].minCircle),
	    Do_tier_quality(TIER_ARTIFACT));

    return TRUE;
}

	/* artifact gift (id > 0) or removal (id < 0) */
void Do_gear_wizard_artifact(struct client_t *c, int id)
{
    struct gear_item_t item;
    char message[SZ_LINE];
    int i;

    if (!c->gearLoaded) {
	return;
    }

    if (id < 0) {
	for (i = 0; i < c->numGear; i++) {
	    if (c->gear[i].artifact == -id) {
		Do_gear_remove(c, i);
		Do_gear_apply(c, FALSE);
		Do_gear_save(c);
		sprintf(message, "The Wizards have taken back %s.\n",
			artifacts[-id].name);
		Do_send_line(c, message);
		break;
	    }
	}
	Do_artifact_release_offline(c, -id);
	return;
    }

    if (!Do_artifact_claim_id(c, id, &item)) {
	return;
    }

    sprintf(message, "The Wizards have granted %s to %s!\n",
	    artifacts[id].name, c->player.name);
    Do_broadcast(c, message);
    Do_gear_offer(c, &item);
}

	/* a gift of gear: slot -1 any, circle 0 means the player's own */
void Do_gear_wizard_gift(struct client_t *c, int slot, int tier, double circle)
{
    struct gear_item_t item;

    if (!c->gearLoaded) {
	return;
    }

    tier = MAX(TIER_COMMON, MIN(TIER_ENCHANTED, tier));
    if (circle < 1) {
	circle = MAX(1.0, c->player.circle);
    }

    Do_gear_make(&item, slot >= 0 && slot < NUM_SLOTS ? slot : -1, circle,
	    tier);

	/* exactly the tier asked for */
    item.tier = tier;
    item.ability = Do_gear_roll_ability(tier, item.slot);
    if (tier >= TIER_MASTERWORK && item.ability == AB_NONE) {
	item.ability = Do_pick_ability(item.slot, RND(), RND());
    }
    Do_gear_roll_stats(&item, circle, Do_tier_quality(tier));

    Do_send_line(c, "The Wizards have sent you a gift!\n");
    Do_more(c);
    Do_gear_offer(c, &item);
}

	/* set the bag size */
void Do_gear_wizard_bag(struct client_t *c, int size)
{
    char message[SZ_LINE];

    if (!c->gearLoaded) {
	return;
    }

	/* never smaller than what is already in it */
    c->bagSize = MAX(MAX(BACKPACK_SIZE, Do_gear_backpack_count(c)),
	    MIN(MAX_BAG_SIZE, size));
    Do_check_weight(c);
    Do_gear_send(c);
    Do_gear_save(c);

    sprintf(message, "The Wizards have given you a %s (%d slots).\n",
	    Do_bag_name(c->bagSize), c->bagSize);
    Do_send_line(c, message);
}

	/* text for /gear in chat */
void Do_gear_chat(struct client_t *c)
{
    char line[SZ_LINE * 2];
    int i;

    strcpy(line, "[royal] ");
    if (Do_regalia_line(c, line + 8)) {
	Do_send_chat_self(c, line);
    }

    if (!c->gearLoaded || c->numGear == 0) {
	Do_send_chat_self(c, "You have no gear.\n");
	return;
    }

    for (i = 0; i < c->numGear; i++) {
	sprintf(line, "%s ", c->gear[i].equipped ? "[worn]" : "[pack]");
	Do_gear_describe(&c->gear[i], line + strlen(line));
	strcat(line, "\n");
	Do_send_chat_self(c, line);
    }
}

	/* text for /artifacts in chat */
void Do_artifacts_chat(struct client_t *c)
{
    char line[SZ_LINE];
    int id;

    Do_lock_mutex(&c->realm->gear_lock);
    for (id = 1; id <= NUM_ARTIFACTS; id++) {
	sprintf(line, "%s: %s\n", artifacts[id].name,
		c->realm->artifactHolder[id][0] ? c->realm->artifactHolder[id]
		: "lost somewhere in the realm");
	Do_send_chat_self(c, line);
    }
    Do_unlock_mutex(&c->realm->gear_lock);
}


/************************************************************************
/
/ FUNCTION NAME: Do_autoplay_gear_values(c, kind, items, n)
/
/ FUNCTION: what each item would do for this character, for autoplay
/
/ DESCRIPTION:
/	Judged with the fight math (Do_fight_cost): for each item, the
/	character's stats with it worn in its slot instead of what's there
/	(armor and helms add to full energy, weapons to might as strength x
/	(1 + sqrt(sword) x 0.04), boots to quickness as sqrt(quicksilver)),
/	the share of full energy an average fight costs at the circle this
/	character hunts in now.  Lower is better.  Sent as: the kind of list
/	("gear", "offer" or "armory"), "huntCircle:costNow", the count, then
/	a line each, in the list's order: the cost with it, and the circle the
/	character could hunt in with it.  Abilities aren't counted.
/
/************************************************************************/

static double Do_gear_stats_cost(struct client_t *c, int hunt, double shield,
	double sword, double quicksilver, int *huntWith)
{
    double might, quick, top;

    might = c->player.max_strength * (1 + sqrt(MAX(0.0, sword)) * N_SWORDPOWER);
    quick = c->player.quickness - sqrt(floor(MAX(0.0, c->player.quicksilver))) +
	    sqrt(floor(MAX(0.0, quicksilver)));
    top = MAX(1.0, c->player.max_energy + shield);
	/* and how far out these stats could hunt */
    if (huntWith) *huntWith = Do_hunt_circle(c, might, quick, top);
    return Do_fight_cost(c, hunt, might, quick, top, NULL, NULL, NULL) / top;
}

void Do_autoplay_gear_values(struct client_t *c, const char *kind,
	struct gear_item_t *items, int n)
{
    char line[SZ_LINE];
    double now, might, top;
    int hunt, i, w;

    if (!c->autoplayOk) return;

    might = c->player.max_strength * (1 + sqrt(MAX(0.0, c->player.sword)) * N_SWORDPOWER);
    top = c->player.max_energy + c->player.shield;
    hunt = Do_hunt_circle(c, might, c->player.quickness, top);
    now = Do_gear_stats_cost(c, hunt, c->player.shield, c->player.sword, c->player.quicksilver, NULL);

    Do_send_int(c, GEARVALUE_PACKET);
    sprintf(line, "%s\n%d:%.5f\n", kind, hunt, now);
    Do_send_string(c, line);
    Do_send_int(c, n);
    for (i = 0; i < n; i++) {
	struct gear_item_t *g = &items[i];
	double shield = c->player.shield, sword = c->player.sword, qs = c->player.quicksilver;

	if (g->equipped && items == c->gear) {
	    sprintf(line, "%.5f:%d\n", now, hunt);
	    Do_send_string(c, line);
	    continue;
	}
	    /* take off what's in that slot, put this on */
	w = Do_gear_equipped_in(c, g->slot);
	if (w >= 0) {
	    shield -= c->gear[w].shield;
	    sword -= c->gear[w].sword;
	    qs -= c->gear[w].quicksilver;
	}
	shield += g->shield;
	sword += g->sword;
	qs += g->quicksilver;
	{
	    int huntWith;
	    double cost = Do_gear_stats_cost(c, hunt, shield, sword, qs, &huntWith);

	    sprintf(line, "%.5f:%d\n", cost, huntWith);
	    Do_send_string(c, line);
	}
    }
}

/* the next bag up, at this town's prices, for autoplay */
void Do_autoplay_bag(struct client_t *c, char *out)
{
    int now = Do_bag_index(c->bagSize), next = now + 1;

    if (next >= NUM_BAGS) {
	sprintf(out, "-:0:%g:%g", bagGold[now], bagGold[now]);
	return;
    }
    sprintf(out, "%s:%.0f:%g:%g", bags[next].name,
	    floor(bags[next].price * Do_world_price_factor(c)), bagGold[now], bagGold[next]);
}


/************************************************************************
/
/ FUNCTION NAME: Do_gear_artifact_grow(struct client_t *c)
/
/ FUNCTION: artifacts grow with their bearer
/
/ DESCRIPTION:
/	One of a kind should always feel like the best: an artifact's stats
/	follow its bearer's level, as if made at the circle that matches it
/	(1 + level / 4, about where characters of that level hunt).  It
/	keeps its own roll against the formula, at the artifact tier's
/	quality, which no other tier reaches.  Returns how many changed
/	(and in *grew how many grew).
/
/************************************************************************/

int Do_gear_artifact_grow(struct client_t *c, int *grew)
{
    double q = Do_tier_quality(TIER_ARTIFACT), target;
    struct gear_item_t was, now;
    int i, grown = 0, changed = 0;

    if (!c->gearLoaded) return 0;
	/* the circle that matches its bearer's level: about where characters
	   of that level hunt (where it was found doesn't count, so carrying
	   one in from far out gives no head start) */
    target = MIN(30000.0, 1.0 + floor(c->player.level / 4.0));

    for (i = 0; i < c->numGear; i++) {
	struct gear_item_t *g = &c->gear[i];

	if (g->artifact <= 0 || g->circle == (short) target) continue;

	    /* the formula at its old circle and its new one, at roll 1:
	       its own roll carries over */
	was = *g;
	Do_gear_set_stats(&was, MAX(1, g->circle), q);
	now = *g;
	Do_gear_set_stats(&now, target, q);

	if (was.shield >= 1 && g->shield >= 1) g->shield = MAX(1.0, floor(now.shield * g->shield / was.shield));
	if (was.sword >= 1 && g->sword >= 1) g->sword = MAX(1.0, floor(now.sword * g->sword / was.sword));
	if (was.quicksilver >= 1 && g->quicksilver >= 1)
	    g->quicksilver = MAX(1.0, floor(now.quicksilver * g->quicksilver / was.quicksilver));
	grown += (short) target > g->circle;
	g->circle = (short) target;
	++changed;
    }
    if (grew) *grew = grown;
    return changed;
}


/************************************************************************
/
/ FUNCTION NAME: Do_gear_banner(struct client_t *c)
/
/ FUNCTION: the Banner of the Seasons, for the war's greatest helper
/
/ DESCRIPTION:
/	The top contributor to a won War of the Seasons holds the Banner
/	until the next war ends: every one of that account's characters
/	carries it (new ones included), worn as a trinket, its bonus sized
/	to the character's level at the artifact tier's quality.  It can't
/	be found, stolen or offered; any other character loses it.  Checked
/	each turn.
/
/************************************************************************/

void Do_gear_banner(struct client_t *c)
{
    char holder[SZ_NAME], line[SZ_LINE];
    struct gear_item_t item;
    double circle;
    int i, have = -1;

    if (!c->gearLoaded || !c->lcaccount[0]) return;
    Do_world_banner_holder(holder);

    for (i = 0; i < c->numGear; i++) if (c->gear[i].ability == AB_CONQUEROR) have = i;

    if (have >= 0 && strcmp(holder, c->lcaccount) != 0) {
	Do_gear_remove(c, have);
	Do_gear_apply(c, FALSE);
	Do_gear_save(c);
	Do_send_line(c, "The Banner of the Seasons leaves you, to fly over a new war.\n");
	return;
    }
    if (strcmp(holder, c->lcaccount) != 0) return;

    circle = MIN(30000.0, 1.0 + floor(c->player.level / 4.0));
    if (have >= 0) {
	if (c->gear[have].circle != (short) circle) {	/* grows with the bearer */
	    Do_gear_set_stats(&c->gear[have], circle, Do_tier_quality(TIER_ARTIFACT));
	    Do_gear_apply(c, FALSE);
	    Do_gear_save(c);
	}
	return;
    }
    if (c->numGear >= MAX_GEAR) return;

    memset(&item, 0, sizeof item);
    item.slot = SLOT_TRINKET;
    item.tier = TIER_ARTIFACT;
    item.base = 7;
    item.ability = AB_CONQUEROR;
    item.ability2 = AB_RALLY;
    Do_gear_set_stats(&item, circle, Do_tier_quality(TIER_ARTIFACT));
    c->gear[c->numGear++] = item;
    Do_gear_equip(c, c->numGear - 1);
    Do_gear_apply(c, TRUE);
    Do_gear_save(c);
    sprintf(line, "You carry the Banner of the Seasons, won as the greatest helper in the War of the Seasons!\n");
    Do_send_line(c, line);
}
