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
#define ARTIFACT_TREASURE_CHANCE 0.02	/* chance treasure is an artifact */
#endif
#ifndef ARTIFACT_CHAMPION_CHANCE
#define ARTIFACT_CHAMPION_CHANCE 0.08	/* chance a champion has one */
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
    "Seafarer", "Dragonbane"
};

static const char *abilitySuffix[NUM_ABILITIES] = {
    "", "of Evasion", "of the Leech", "of Swiftness", "of Crushing",
    "of Thorns", "of Fortune", "of Warding", "of Mending",
    "of Prospecting", "", "", ""
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
    "saves you from death once, then flies away",
    "the creatures of the water will not fight you",
    "double damage against dragons and wyrms"
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
    { "Mourneblade", SLOT_WEAPON, AB_CRUSHING, AB_LEECH, 10 },
    { "The Phoenix Amulet", SLOT_TRINKET, AB_PHOENIX, AB_MENDING, 5 },
    { "The Crown of the Deep", SLOT_HELM, AB_SEAFARER, AB_WARDING, 20 },
    { "Gwaihir's Feather", SLOT_BOOTS, AB_SWIFTNESS, AB_EVASION, 8 },
    { "The Dragonscale Aegis", SLOT_ARMOR, AB_THORNS, AB_WARDING, 15 },
    { "The Ring of Hoarding", SLOT_TRINKET, AB_FORTUNE, AB_PROSPECTING, 3 },
    { "Wyrmslayer", SLOT_WEAPON, AB_DRAGONBANE, AB_SWIFTNESS, 25 },
    { "The Mantle of Shadows", SLOT_ARMOR, AB_EVASION, AB_THORNS, 12 },
    { "The Helm of Ages", SLOT_HELM, AB_MENDING, AB_WARDING, 6 },
    { "The Wind-lord's Boots", SLOT_BOOTS, AB_EVASION, AB_MENDING, 18 }
};


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
static void Do_gear_roll_stats(struct gear_item_t *item, double circle,
	double quality)
{
    double c = MAX(1.0, circle);
    double q = quality * (0.85 + RND() * 0.3);

    item->shield = item->sword = item->quicksilver = 0;

    switch (item->slot) {
    case SLOT_WEAPON:
	item->sword = MAX(1, floor(1.6 * c * q));
	break;
    case SLOT_ARMOR:
	item->shield = MAX(1, floor(40.0 * c * q));
	break;
    case SLOT_HELM:
	item->shield = MAX(1, floor(15.0 * c * q));
	break;
    case SLOT_BOOTS:
	item->quicksilver = MAX(1, floor(0.6 * MAX(1.0, c - 3.0) * q));
	break;
    case SLOT_TRINKET:
	item->shield = floor(6.0 * c * q);
	item->sword = floor(0.3 * c * q);
	break;
    }

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

static int Do_gear_roll_ability(int tier)
{
    if (tier == TIER_COMMON || tier == TIER_WORN) {
	return AB_NONE;
    }
    if (tier == TIER_FINE && RND() > 0.4) {
	return AB_NONE;
    }

    return 1 + (int) (RND() * NUM_COMMON_ABILITIES) % NUM_COMMON_ABILITIES;
}

	/* a random item; slot < 0 means any slot */
void Do_gear_make(struct gear_item_t *item, int slot, double circle,
	int minTier)
{
    memset(item, 0, sizeof(*item));

    item->slot = slot >= 0 ? slot : (int) (RND() * NUM_SLOTS) % NUM_SLOTS;
    item->tier = Do_gear_roll_tier(circle, minTier);
    item->ability = Do_gear_roll_ability(item->tier);
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
	fprintf(out_file, "%s %d %d %d %d %d %d %d %.0f %.0f %.0f\n",
		c->player.lcname, g->slot, g->equipped, g->tier, g->base,
		g->ability, g->ability2, g->artifact, g->shield, g->sword,
		g->quicksilver);
    }

	/* a placeholder so a character with no gear isn't converted again */
    if (c->numGear == 0) {
	fprintf(out_file, "%s -1 0 0 0 0 0 0 0 0 0\n", c->player.lcname);
    }

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

	/* load (or create) this character's gear when entering the realm */
void Do_gear_load(struct client_t *c)
{
    FILE *the_file;
    char line[SZ_LINE], name[SZ_NAME + 1];
    struct gear_item_t g;
    int slot, equipped, tier, base, ab, ab2, art, found = FALSE, id;
    double shield, sword, quick;

    c->numGear = 0;

    Do_lock_mutex(&c->realm->gear_lock);

    if ((the_file = fopen(GEAR_FILE, "r")) != NULL) {
	while (fgets(line, sizeof(line), the_file) != NULL) {

	    if (sscanf(line, "%32s %d %d %d %d %d %d %d %lf %lf %lf", name,
		    &slot, &equipped, &tier, &base, &ab, &ab2, &art, &shield,
		    &sword, &quick) != 11 ||
		    strcmp(name, c->player.lcname) != 0) {
		continue;
	    }

	    found = TRUE;

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
	    g.shield = shield; g.sword = sword; g.quicksilver = quick;
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
	    g.base = MIN(7, (int) (log(MAX(1.0, c->player.circle)) / log(2.2)));
	    g.sword = floor(c->player.sword);
	    c->gear[c->numGear++] = g;
	}
	if (c->player.shield >= 1) {
	    memset(&g, 0, sizeof(g));
	    g.slot = SLOT_ARMOR; g.equipped = TRUE; g.tier = TIER_WORN;
	    g.base = MIN(7, (int) (log(MAX(1.0, c->player.circle)) / log(2.2)));
	    g.shield = floor(c->player.shield);
	    c->gear[c->numGear++] = g;
	}
	if (c->player.quicksilver >= 1) {
	    memset(&g, 0, sizeof(g));
	    g.slot = SLOT_BOOTS; g.equipped = TRUE; g.tier = TIER_WORN;
	    g.base = MIN(7, (int) (log(MAX(1.0, c->player.circle)) / log(2.2)));
	    g.quicksilver = floor(c->player.quicksilver);
	    c->gear[c->numGear++] = g;
	}
    }

    c->gearLoaded = TRUE;
    Do_gear_save_locked(c);

    Do_unlock_mutex(&c->realm->gear_lock);
}


/************************************************************************
/ applying gear to the character
*************************************************************************/

	/* recompute shield/sword/quicksilver from equipped gear */
void Do_gear_apply(struct client_t *c, int gainEnergy)
{
    double shield = 0, sword = 0, quick = 0, energy;
    int i;

    for (i = 0; i < c->numGear; i++) {
	if (c->gear[i].equipped) {
	    shield += c->gear[i].shield;
	    sword += c->gear[i].sword;
	    quick += c->gear[i].quicksilver;
	}
    }

    energy = c->player.energy;
    if (gainEnergy && shield > c->player.shield) {
	energy += shield - c->player.shield;
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

    return total;
}

	/* gear list for the browser client (other clients don't know it) */
void Do_gear_send(struct client_t *c)
{
    char line[SZ_LINE * 4], name[SZ_LINE], stats[SZ_LINE];
    int i;

    if (!c->isBrowser || !c->gearLoaded) {
	return;
    }

    Do_send_int(c, GEAR_PACKET);
    sprintf(line, "%d\n", c->numGear);
    Do_send_string(c, line);

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

    room = Do_gear_backpack_count(c) < BACKPACK_SIZE;

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
void Do_gear_monster_drop(struct client_t *c)
{
    struct gear_item_t item;
    char message[SZ_LINE];

    if (!c->gearLoaded) {
	return;
    }

    if (c->championFight && RND() < ARTIFACT_CHAMPION_CHANCE &&
	    Do_artifact_claim(c, &item)) {

	sprintf(message, "%s has found %s!\n", c->player.name,
		artifacts[item.artifact].name);
	Do_broadcast(c, message);
	Do_gear_offer(c, &item);
	return;
    }

    if (c->championFight) {
	Do_gear_make(&item, -1, c->player.circle + 2, TIER_MASTERWORK);
	Do_send_line(c, "The champion's gear is yours for the taking.\n");
	Do_more(c);
	Do_gear_offer(c, &item);
	return;
    }

    if (RND() < GEAR_DROP_CHANCE) {
	Do_gear_make(&item, -1, c->player.circle, TIER_COMMON);
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

    Do_gear_make_valued(&item, slot, value, circle);
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
	item.ability = 1 + (int) (RND() * NUM_COMMON_ABILITIES) %
		NUM_COMMON_ABILITIES;
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

	if (!g->equipped || g->artifact > 0) {
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
    Do_gear_save(c);
    c->gearLoaded = FALSE;
}

	/* the Phoenix Amulet saves its holder once, then flies away */
int Do_gear_phoenix(struct client_t *c)
{
    int i, id;

    for (i = 0; i < c->numGear; i++) {
	if (c->gear[i].equipped && (c->gear[i].ability == AB_PHOENIX ||
		c->gear[i].ability2 == AB_PHOENIX)) {

	    id = c->gear[i].artifact;
	    Do_send_line(c, "The Phoenix Amulet blazes with fire and pulls you back from death!\n");
	    Do_more(c);
	    Do_send_clear(c);

	    Do_gear_remove(c, i);
	    Do_energy(c, c->player.max_energy / 2.0 + 1.0, c->player.max_energy,
		    c->player.shield, 0, FALSE);
	    Do_gear_apply(c, FALSE);
	    Do_gear_save(c);

	    if (id > 0) {
		Do_artifact_release(c, id, "was saved by the Phoenix Amulet!");
	    }
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

    return floor(inflict);
}

	/* after the player's hit lands */
void Do_gear_after_attack(struct client_t *c, double inflict)
{
    double p, heal;

    if (c->battle.opponent->special_type == SM_IT_COMBAT) {
	return;
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
}

double Do_gear_gold_factor(struct client_t *c)
{
    return 1.0 + 0.15 * Do_gear_ability(c, AB_FORTUNE);
}

double Do_gear_treasure_factor(struct client_t *c)
{
    return 1.0 + 0.2 * Do_gear_ability(c, AB_PROSPECTING);
}

double Do_gear_poison_factor(struct client_t *c)
{
    return MAX(0.2, 1.0 - 0.2 * Do_gear_ability(c, AB_WARDING));
}


/************************************************************************
/ the gear menu (Info -> Gear) and the armory
*************************************************************************/

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

    Do_send_line(c, "\n");
    n = Do_gear_backpack_count(c);
    sprintf(line, "Backpack (%d of %d):\n", n, BACKPACK_SIZE);
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
    int map[8], n = 0, i;
    long answer;

    Do_clear_buttons(&buttons, 0);

    for (i = 0; i < c->numGear && n < 7; i++) {
	struct gear_item_t *g = &c->gear[i];

	if ((which == 0 && !g->equipped) || (which == 1 && g->equipped) ||
		(which == 2 && g->artifact > 0)) {
	    Do_gear_label(g, buttons.button[n]);
	    map[n++] = i;
	}
    }

    if (n == 0) {
	Do_send_line(c, "There is nothing to choose.\n");
	Do_more(c);
	return -1;
    }

    strcpy(buttons.button[7], "Cancel\n");
    Do_send_line(c, prompt);

    if (Do_buttons(c, &answer, &buttons) != S_NORM || answer < 0 ||
	    answer >= n) {
	return -1;
    }

    return map[answer];
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
    Do_gear_apply(c, FALSE);

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
	strcpy(buttons.button[6], "Armory\n");	/* test builds only */
#endif

	if (Do_buttons(c, &answer, &buttons) != S_NORM || answer == 7) {
	    Do_send_clear(c);
	    return;
	}

	switch (answer) {
#ifdef GEAR_TEST
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
	    if (Do_gear_backpack_count(c) >= BACKPACK_SIZE) {
		Do_send_line(c, "Your backpack is full.\n");
		Do_more(c);
		break;
	    }
	    index = Do_gear_choose(c, 1, "Which item do you want to take off?\n");
	    if (index >= 0) {
		c->gear[index].equipped = FALSE;
		Do_gear_apply(c, FALSE);
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
static double Do_gear_value(struct gear_item_t *g)
{
    double value = g->shield * 5.0 + g->sword * 500.0 +
	    g->quicksilver * 2000.0;

    if (g->ability != AB_NONE) {
	value *= 1.5;
    }

    return floor(MAX(10.0, value));
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
		    1 + rand_r(&state) % NUM_COMMON_ABILITIES : AB_NONE;
	    {
		double q = Do_tier_quality(t) * (0.9 + (rand_r(&state) % 20) / 100.0);
		double cc = MAX(1.0, c->player.circle);
		stock[n].base = MIN(7, (int) (log(cc) / log(2.2)) + rand_r(&state) % 2);
		if (s == SLOT_WEAPON) stock[n].sword = MAX(1, floor(1.6 * cc * q));
		else if (s == SLOT_ARMOR) stock[n].shield = MAX(1, floor(40.0 * cc * q));
		else if (s == SLOT_HELM) stock[n].shield = MAX(1, floor(15.0 * cc * q));
		else if (s == SLOT_BOOTS) stock[n].quicksilver = MAX(1, floor(0.6 * MAX(1.0, cc - 3.0) * q));
		else { stock[n].shield = floor(6.0 * cc * q); stock[n].sword = floor(0.3 * cc * q); }
	    }
	    price[n] = Do_gear_value(&stock[n]);

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

	if (Do_buttons(c, &answer, &buttons) != S_NORM || answer == 7) {
	    Do_send_clear(c);
	    return;
	}

	if (answer == 6) {
	    index = Do_gear_choose(c, 0, "What will you sell? (Only backpack items can be sold.)\n");
	    if (index >= 0 && c->gear[index].artifact == 0) {
		double paid = floor(Do_gear_value(&c->gear[index]) * 0.4);
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

	if (Do_gear_backpack_count(c) >= BACKPACK_SIZE &&
		Do_gear_equipped_in(c, stock[answer].slot) >= 0) {
	    Do_send_line(c, "Your backpack is full.  Sell or discard something first.\n");
	    Do_more(c);
	    continue;
	}

	Do_gold(c, -price[answer], FALSE);
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

	/* text for /gear in chat */
void Do_gear_chat(struct client_t *c)
{
    char line[SZ_LINE * 2];
    int i;

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
