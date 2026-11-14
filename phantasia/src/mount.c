/*
 * mount.c - mounts: travel, carrying, and (for the dragon) a fight
 *
 * A character can ride one mount.  Mounts lengthen every step, some carry
 * gold, and a few have a gift of their own; only the Dragon Hatchling
 * fights.  They are a full simulation: they eat (a daily cost paid at the
 * Stables), tire as they travel (resting restores them), can be injured by
 * a monster's blow (rest or a Stables heals them) and can be killed by a
 * very heavy one.  A mount is lost when its rider dies.
 *
 * Where they come from: the common ones (Pony, Pack Mule, Riding Horse,
 * Warhorse) are sold at the Stables of a trading post; a Marsh Strider or
 * Warg is tamed from a weakened monster; a Great Eagle comes from Gwaihir or
 * a great deed; a Nightmare from a Nazgul or the Dark Lord; and the single
 * Dragon Hatchling from slaying a dragon champion or nemesis.
 */

#include "include.h"

struct mount_def {
    const char *name;
    double step;	/* steps go this many times as far */
    double carry;	/* gold carried before slowing, times this */
    double price;	/* at the Stables (0: not sold) */
    int minRank;	/* the smallest post that sells it */
    double feed;	/* gold a day */
    double stamina;	/* steps before it tires */
    double injury;	/* chance of injury, times this */
    const char *gift;	/* what's special about it */
};

static const struct mount_def mounts[NUM_MOUNTS] = {
    { "", 1, 1, 0, 0, 0, 0, 0, "" },
    { "Pony", 1.5, 1, 500, 1, 10, 80, 1.0, "steps half again as far" },
    { "Pack Mule", 1.25, 1.5, 800, 1, 12, 100, 1.0, "carries half again as much gold" },
    { "Riding Horse", 2, 1, 3000, 2, 25, 100, 1.0, "steps twice as far, but no faster in circles 20 to 35" },
    { "Warhorse", 2, 1.25, 15000, 4, 60, 150, 0.5, "steps twice as far anywhere, carries more gold, and is hard to injure" },
    { "Marsh Strider", 2, 1, 0, 0, 20, 120, 1.0, "steps twice as far, at full speed even in circles 20 to 35" },
    { "Great Eagle", 2, 1, 0, 0, 40, 120, 1.0, "steps twice as far, and monsters find you far less often on a Move To walk" },
    { "Warg", 2, 1, 0, 0, 30, 110, 1.0, "steps twice as far, and wargs and wolves leave you alone (but shops charge more)" },
    { "Nightmare", 2.5, 1, 0, 0, 50, 140, 0.8, "steps two and a half times as far, and the night brings you no more monsters (it slowly darkens your soul)" },
    { "Dragon Hatchling", 3, 1, 0, 0, 100, 200, 0.5, "steps three times as far, and breathes fire on your foes" }
};

#define FEED_DAYS	3	/* bought at a time at the Stables */
#define REST_STAMINA	25	/* restored by a rest */

const char *Do_mount_name(int type)
{
    return (type > 0 && type < NUM_MOUNTS) ? mounts[type].name : "";
}

static int Do_mount_hungry(struct client_t *c)
{
    return c->mountType && time(NULL) > c->mountFedUntil;
}

	/* can it carry you right now? (not exhausted, not injured) */
int Do_mount_usable(struct client_t *c)
{
    return c->mountType > 0 && c->mountType < NUM_MOUNTS &&
	    c->mountStamina >= 1 && c->mountInjured <= 0;
}

	/* how much farther a step goes (half the gift when it's hungry) */
double Do_mount_step_factor(struct client_t *c)
{
    double f;

    if (!Do_mount_usable(c)) return 1.0;
    f = mounts[c->mountType].step;
	/* a Riding Horse can't hurry through the Marshes and Gorgoroth */
    if (c->mountType == MOUNT_HORSE && c->player.circle > 19 && c->player.circle < 36) f = 1.0;
    if (Do_mount_hungry(c)) f = 1.0 + (f - 1.0) / 2;
    return f;
}

	/* the Marsh Strider ignores the slow going of circles 20 to 35 */
int Do_mount_marsh_speed(struct client_t *c)
{
    return Do_mount_usable(c) && c->mountType == MOUNT_STRIDER;
}

double Do_mount_carry_factor(struct client_t *c)
{
    if (!c->mountType || c->mountType >= NUM_MOUNTS) return 1.0;
    return mounts[c->mountType].carry;
}

	/* the Great Eagle: far fewer ambushes on a Move To walk */
double Do_mount_encounter_factor(struct client_t *c)
{
    return (c->walking && Do_mount_usable(c) && c->mountType == MOUNT_EAGLE) ? 0.25 : 1.0;
}

	/* the Nightmare: the night brings no more monsters */
int Do_mount_night_ok(struct client_t *c)
{
    return Do_mount_usable(c) && c->mountType == MOUNT_NIGHTMARE;
}

	/* the Warg: shops charge more */
double Do_mount_price_factor(struct client_t *c)
{
    return c->mountType == MOUNT_WARG ? 1.1 : 1.0;
}

	/* each step tires it */
void Do_mount_on_step(struct client_t *c)
{
    char line[SZ_LINE];

    if (!c->mountType || c->mountStamina < 1) return;
    c->mountStamina -= 1;
    if (c->mountStamina < 1) {
	sprintf(line, "Your %s is exhausted. Rest to let it recover.\n", mounts[c->mountType].name);
	Do_send_line(c, line);
	Do_mount_send(c);
    }
}

	/* a rest restores it, and mends injuries slowly */
void Do_mount_on_rest(struct client_t *c)
{
    char line[SZ_LINE];

    if (!c->mountType) return;
    c->mountStamina = MIN(mounts[c->mountType].stamina, c->mountStamina + REST_STAMINA);
    if (c->mountInjured > 0 && --c->mountInjured == 0) {
	sprintf(line, "Your %s's injury has healed.\n", mounts[c->mountType].name);
	Do_send_line(c, line);
    }
    Do_mount_send(c);
}

	/* a bought mount, given up for a new one, fetches half its price */
static void Do_mount_trade_in(struct client_t *c)
{
    char line[SZ_LINE];
    double back;

    if (!c->mountType || mounts[c->mountType].price <= 0) return;
    back = floor(mounts[c->mountType].price * 0.5);
    Do_gold(c, back, FALSE);
    sprintf(line, "A passing trader buys your old %s for %.0f gold.\n", mounts[c->mountType].name, back);
    Do_send_line(c, line);
}

	/* a new mount (replacing any other) */
void Do_mount_grant(struct client_t *c, int type, const char *how)
{
    char line[SZ_LINE];

    if (type <= 0 || type >= NUM_MOUNTS) return;
    if (c->mountType == MOUNT_DRAGON && type != MOUNT_DRAGON) Do_world_set_dragon("");
    c->mountType = type;
    c->mountStamina = mounts[type].stamina;
    c->mountFedUntil = time(NULL) + FEED_DAYS * 86400;
    c->mountInjured = 0;
    if (how && *how) {
	snprintf(line, sizeof line, "%s You now ride a %s: it %s.\n", how, mounts[type].name, mounts[type].gift);
	Do_send_line(c, line);
    }
    Do_mount_send(c);
    Do_gear_save(c);
}

static void Do_mount_lose(struct client_t *c, const char *why)
{
    char line[SZ_LINE];

    if (!c->mountType) return;
    snprintf(line, sizeof line, "%s\n", why);
    Do_send_line(c, line);
    if (c->mountType == MOUNT_DRAGON) {
	Do_world_set_dragon("");
	snprintf(line, sizeof line, "The Dragon Hatchling of %s is gone. It is out there again, for a dragon-slayer to find.\n",
		c->modifiedName);
	Do_broadcast(c, line);
    }
    c->mountType = 0;
    Do_mount_send(c);
    Do_gear_save(c);
}

	/* the rider died: the mount is lost with them */
void Do_mount_death(struct client_t *c)
{
    char line[SZ_LINE];

    if (c->mountType == MOUNT_DRAGON) {
	Do_world_set_dragon("");
	snprintf(line, sizeof line, "The Dragon Hatchling of %s is riderless. It is out there again, for a dragon-slayer to find.\n",
		c->modifiedName);
	Do_broadcast(c, line);
    }
    c->mountType = 0;
}

	/* a monster's blow: it may injure the mount, or (rarely) kill it */
void Do_mount_monster_hit(struct client_t *c, double inflict)
{
    double top = c->player.max_energy + c->player.shield;
    char line[SZ_LINE];

    if (!c->mountType || inflict <= 0 || c->battle.opponent->special_type == SM_IT_COMBAT) return;
    if (inflict > 0.4 * top && RND() < 0.02 * mounts[c->mountType].injury) {
	snprintf(line, sizeof line, "The blow strikes down your %s! You fight on, on foot.", mounts[c->mountType].name);
	Do_mount_lose(c, line);
	return;
    }
    if (inflict > 0.1 * top && c->mountInjured <= 0 && RND() < 0.04 * mounts[c->mountType].injury) {
	c->mountInjured = 6;	/* rests to heal, or a Stables */
	sprintf(line, "Your %s is injured and can't carry you swiftly until it heals.\n", mounts[c->mountType].name);
	Do_send_line(c, line);
	Do_mount_send(c);
    }
}

	/* the Dragon Hatchling breathes fire after each of your blows */
void Do_mount_after_attack(struct client_t *c, double inflict)
{
    double share, fire;
    char line[SZ_LINE];

    if (c->mountType != MOUNT_DRAGON || !Do_mount_usable(c) || inflict <= 0 ||
	    c->battle.opponent->special_type == SM_IT_COMBAT || c->battle.opponent->energy <= 0) return;
	/* it grows with you: a fifth of your blow at first, up to three fifths */
    share = MIN(0.6, 0.2 + c->player.level / 1000.0);
    fire = floor(inflict * share);
    if (fire < 1) return;
    sprintf(line, "Your dragon breathes fire on %s for %.0f damage!\n", c->battle.opponent->name, fire);
    Do_send_line(c, line);
    c->battle.opponent->energy -= fire;
}

static int Do_is_wolf(const char *name)
{
    return strstr(name, "Warg") || strstr(name, "Wolf") || strstr(name, "wolf");
}

	/* the Warg: its kin leave you be (TRUE if the monster leaves) */
int Do_mount_kin_leave(struct client_t *c)
{
    char line[SZ_LINE];

    if (c->mountType != MOUNT_WARG || c->championFight || c->nemesisFight >= 0 ||
	    c->battle.opponent->special_type != SM_NONE || !Do_is_wolf(c->battle.opponent->realName)) return FALSE;
    sprintf(line, "%s sniffs at your warg and slinks away.\n", c->battle.opponent->name);
    Do_send_line(c, line);
    Do_cancel_monster(c->battle.opponent);
    return TRUE;
}

	/* each turn: hunger, the Nightmare's darkness, and the one dragon */
void Do_mount_turn(struct client_t *c)
{
    char line[SZ_LINE], holder[SZ_NAME];

    if (!c->mountType) return;
    if (c->mountType == MOUNT_DRAGON) {
	Do_world_dragon_holder(holder);
	if (strcmp(holder, c->player.lcname) != 0) {	/* claimed elsewhere */
	    c->mountType = 0;
	    Do_mount_send(c);
	    Do_gear_save(c);
	    return;
	}
    }
    if (Do_mount_hungry(c) && !c->mountHungryTold) {
	c->mountHungryTold = TRUE;
	sprintf(line, "Your %s is hungry and slows down. Feed it at the Stables of any trading post.\n",
		mounts[c->mountType].name);
	Do_send_line(c, line);
	Do_mount_send(c);
    }
    if (c->mountType == MOUNT_NIGHTMARE) Do_sin(c, 0.0005);
}

	/* a weakened Warg, or a water monster of the Dead Marshes, can be tamed */
int Do_mount_can_tame(struct client_t *c)
{
    struct opponent_t *o = c->battle.opponent;

    if (o->special_type != SM_NONE || c->championFight || c->nemesisFight >= 0 ||
	    o->energy <= 0 || o->energy > 0.25 * o->max_energy) return FALSE;
	/* not when you already ride something as swift */
    if (c->mountType && (mounts[c->mountType].step >= 2 || c->mountType == MOUNT_WARHORSE)) return FALSE;
    if (Do_is_wolf(o->realName) && strstr(o->realName, "Warg")) return MOUNT_WARG;
    if (o->type >= 0 && o->type <= 13 && c->player.circle >= 20 && c->player.circle <= 24) return MOUNT_STRIDER;
    return FALSE;
}

	/* "Tame": brains against the monster's, at half the odds of a luckout */
void Do_mount_tame(struct client_t *c)
{
    struct opponent_t *o = c->battle.opponent;
    int type = Do_mount_can_tame(c);
    double b = Do_eff_brains(c), m = MAX(1.0, o->brains), chance;
    char line[SZ_LINE];

    if (!type) return;
    chance = 0.5 * (b < m ? b / (2 * m) : 1 - m / (2 * b));
    if (RND() < chance) {
	snprintf(line, sizeof line, "You calm %s and it lets you climb on its back.", o->name);
	Do_mount_trade_in(c);
	Do_mount_grant(c, type, line);
	Do_cancel_monster(o);
    }
    else {
	sprintf(line, "%s won't be tamed!\n", o->name);
	Do_send_line(c, line);
    }
}

	/* rare mounts from great deeds and great foes, after a kill */
void Do_mount_after_kill(struct client_t *c)
{
    struct opponent_t *o = c->battle.opponent;
    char line[SZ_LINE], holder[SZ_NAME];

	/* the one Dragon Hatchling, from a dragon champion or nemesis */
    if ((c->championFight || c->nemesisFight >= 0) &&
	    (strstr(o->realName, "Dragon") || strstr(o->realName, "Wyrm") ||
	     strstr(o->realName, "drake") || strstr(o->realName, "Tiamat")) && RND() < 0.05) {
	Do_world_dragon_holder(holder);
	if (!holder[0]) {
	    Do_world_set_dragon(c->player.lcname);
	    Do_mount_trade_in(c);
	    Do_mount_grant(c, MOUNT_DRAGON, "Among the slain dragon's hoard, an egg cracks open!");
	    sprintf(line, "%s has hatched the Dragon Hatchling, the only one in the realm!\n", c->modifiedName);
	    Do_broadcast(c, line);
	    return;
	}
    }
	/* a Nightmare from a Nazgul or the Dark Lord */
    if ((o->special_type == SM_NAZGUL || o->special_type == SM_DARKLORD) && RND() < 0.1 &&
	    c->mountType != MOUNT_NIGHTMARE && c->mountType != MOUNT_DRAGON) {
	Do_mount_trade_in(c);
	Do_mount_grant(c, MOUNT_NIGHTMARE, "Its riderless steed turns its burning eyes on you, and bows.");
    }
}

	/* a Great Eagle, for Gwaihir's friends and great deeds */
void Do_mount_eagle_chance(struct client_t *c, double chance, const char *how)
{
    if (c->mountType == MOUNT_DRAGON || c->mountType == MOUNT_EAGLE ||
	    c->mountType == MOUNT_NIGHTMARE || RND() >= chance) return;
    Do_mount_trade_in(c);
    Do_mount_grant(c, MOUNT_EAGLE, how);
}

/************************************************************************
/ the Stables, at a trading post
*************************************************************************/

void Do_stables(struct client_t *c)
{
    struct button_t buttons;
    char line[SZ_LINE * 2];
    long answer;
    int rank, diagonal, i, n, sold[4];
    double pf, price, feed, heal;

    rank = Do_trading_post_rank(c->player.x, c->player.y, &diagonal);
    pf = Do_world_price_factor(c);

    for (;;) {
	Do_send_clear(c);
	if (c->mountType) {
	    sprintf(line, "You ride a %s (%s). Stamina %.0f of %.0f%s%s.\n", mounts[c->mountType].name,
		    mounts[c->mountType].gift, c->mountStamina, mounts[c->mountType].stamina,
		    Do_mount_hungry(c) ? ", hungry" : "", c->mountInjured > 0 ? ", injured" : "");
	    Do_send_line(c, line);
	}
	else Do_send_line(c, "You are on foot.\n");

	Do_clear_buttons(&buttons, 0);
	for (i = 1, n = 0; i < NUM_MOUNTS && n < 4; i++) {
	    if (mounts[i].price <= 0 || mounts[i].minRank > MAX(1, rank)) continue;
	    price = floor(mounts[i].price * pf);
	    sprintf(line, "%s: %.0f gold, eats %.0f a day; %s.\n", mounts[i].name, price,
		    mounts[i].feed, mounts[i].gift);
	    Do_send_line(c, line);
	    sold[n] = i;
	    snprintf(buttons.button[n], sizeof buttons.button[n], "%s\n", mounts[i].name);
	    n++;
	}
	feed = c->mountType ? floor(mounts[c->mountType].feed * FEED_DAYS * pf) : 0;
	heal = c->mountType ? floor(MAX(100.0, mounts[c->mountType].price * 0.1 + 50 * c->player.level) * pf) : 0;
	if (c->mountType) {
	    sprintf(line, "Feed it for %d days: %.0f gold.  Heal and rest it: %.0f gold.\n", FEED_DAYS, feed, heal);
	    Do_send_line(c, line);
	    strcpy(buttons.button[4], "Feed\n");
	    strcpy(buttons.button[5], "Heal\n");
	    strcpy(buttons.button[6], mounts[c->mountType].price > 0 ? "Sell Mount\n" : "Release\n");
	}
	sprintf(line, "You have %.0f gold.\n", c->player.gold);
	Do_send_line(c, line);
	strcpy(buttons.button[7], "Leave\n");

	if (Do_buttons(c, &answer, &buttons) != S_NORM || answer == 7) {
	    Do_send_clear(c);
	    return;
	}

	if (answer >= 0 && answer < n) {
	    i = sold[answer];
	    price = floor(mounts[i].price * pf);
	    if (c->mountType && mounts[c->mountType].price <= 0) {
		sprintf(line, "You'd have to release your %s first: it's not a mount you can buy back.\n",
			mounts[c->mountType].name);
		Do_send_line(c, line);
	    }
	    else if (price > c->player.gold) Do_send_line(c, "You can't afford it.\n");
	    else {
		double back = c->mountType ? floor(mounts[c->mountType].price * 0.5) : 0;
		Do_gold(c, back - price, FALSE);
		Do_royal_tithe(c, price);
		sprintf(line, "The stablehand saddles your new %s.", mounts[i].name);
		if (back > 0) sprintf(line + strlen(line), " (Your old mount fetched %.0f gold.)", back);
		Do_mount_grant(c, i, line);
	    }
	    Do_more(c);
	    continue;
	}
	if (answer == 4 && c->mountType) {
	    if (feed > c->player.gold) Do_send_line(c, "You can't afford its feed.\n");
	    else {
		Do_gold(c, -feed, FALSE);
		c->mountFedUntil = MAX(time(NULL), c->mountFedUntil) + FEED_DAYS * 86400;
		c->mountHungryTold = FALSE;
		sprintf(line, "Your %s eats its fill.\n", mounts[c->mountType].name);
		Do_send_line(c, line);
		Do_mount_send(c);
		Do_gear_save(c);
	    }
	    Do_more(c);
	    continue;
	}
	if (answer == 5 && c->mountType) {
	    if (heal > c->player.gold) Do_send_line(c, "You can't afford it.\n");
	    else {
		Do_gold(c, -heal, FALSE);
		c->mountInjured = 0;
		c->mountStamina = mounts[c->mountType].stamina;
		sprintf(line, "Your %s is healed and rested.\n", mounts[c->mountType].name);
		Do_send_line(c, line);
		Do_mount_send(c);
		Do_gear_save(c);
	    }
	    Do_more(c);
	    continue;
	}
	if (answer == 6 && c->mountType) {
	    double back = floor(mounts[c->mountType].price * 0.5);
	    if (back > 0) Do_gold(c, back, FALSE);
	    sprintf(line, back > 0 ? "You sell your %s for %.0f gold." : "You set your %s free.",
		    mounts[c->mountType].name, back);
	    Do_mount_lose(c, line);
	    Do_more(c);
	    continue;
	}
    }
}

/************************************************************************
/ telling the browser, /mount, and saving
*************************************************************************/

void Do_mount_send(struct client_t *c)
{
    char line[SZ_LINE];
    double days;

    if (!c->isBrowser) return;
    Do_send_int(c, MOUNT_PACKET);
    if (!c->mountType) {
	Do_send_string(c, "\n\n\n");
	return;
    }
	/* name, condition, and what it does */
    days = (c->mountFedUntil - time(NULL)) / 86400.0;
    snprintf(line, sizeof line, "%s\nstamina %.0f/%.0f, %s%s\nIt %s.\n", mounts[c->mountType].name,
	    c->mountStamina, mounts[c->mountType].stamina,
	    days > 0 ? "fed" : "hungry", c->mountInjured > 0 ? ", injured" : "", mounts[c->mountType].gift);
    Do_send_string(c, line);
}

void Do_mount_chat(struct client_t *c)
{
    char line[SZ_LINE];
    double days;

    if (!c->mountType) {
	Do_send_chat_self(c, "You are on foot. The Stables at any trading post sell mounts.\n");
	return;
    }
    days = (c->mountFedUntil - time(NULL)) / 86400.0;
    snprintf(line, sizeof line, "You ride a %s: it %s. Stamina %.0f of %.0f. %s%s\n", mounts[c->mountType].name,
	    mounts[c->mountType].gift, c->mountStamina, mounts[c->mountType].stamina,
	    days > 0 ? "" : "It is hungry: feed it at the Stables. ",
	    c->mountInjured > 0 ? "It is injured: rest, or heal it at the Stables." : "");
    Do_send_chat_self(c, line);
    if (days > 0) {
	sprintf(line, "It has food for %.1f more days.\n", days);
	Do_send_chat_self(c, line);
    }
}

	/* the gear file keeps it (slot -3): type, stamina, fed until, injured */
void Do_mount_save_line(struct client_t *c, FILE *f)
{
    if (c->mountType) {
	fprintf(f, "%s -3 %d %.0f %ld %d 0 0 0 0 0 0\n", c->player.lcname, c->mountType,
		c->mountStamina, (long) c->mountFedUntil, c->mountInjured);
    }
}

void Do_mount_load_line(struct client_t *c, const char *line)
{
    char name[SZ_NAME + 1];
    int slot, type, injured;
    double stamina;
    long fed;

    if (sscanf(line, "%32s %d %d %lf %ld %d", name, &slot, &type, &stamina, &fed, &injured) == 6 &&
	    slot == -3 && type > 0 && type < NUM_MOUNTS) {
	c->mountType = type;
	c->mountStamina = MIN(mounts[type].stamina, MAX(0.0, stamina));
	c->mountFedUntil = fed;
	c->mountInjured = injured;
    }
}

	/* autoplay: what it rides, and the next mount up for sale
	   (type:step:stamina:price:days fed:injured:next name:price:post rank) */
void Do_mount_autoplay(struct client_t *c, char *out)
{
    int i, next = 0;
    double have = c->mountType ? mounts[c->mountType].price : 0;

    if (!c->mountType || have > 0) {
	for (i = 1; i < NUM_MOUNTS; i++) {
	    if (mounts[i].price > have && (!next || mounts[i].price < mounts[next].price)) next = i;
	}
    }
    sprintf(out, "%d:%.2f:%.2f:%.0f:%.2f:%d:%s:%.0f:%d", c->mountType, Do_mount_step_factor(c),
	    c->mountType ? c->mountStamina / mounts[c->mountType].stamina : 0, have,
	    c->mountType ? (c->mountFedUntil - time(NULL)) / 86400.0 : 0, c->mountInjured,
	    next ? mounts[next].name : "-", next ? floor(mounts[next].price * Do_world_price_factor(c)) : 0,
	    next ? mounts[next].minRank : 0);
}

#ifdef GEAR_TEST
	/* test builds: /mounttest gold|ride N|hungry|tired|post|warg|strider|dragon */
void Do_mount_test(struct client_t *c, char *what)
{
    struct event_t *e;

    if (!strncmp(what, "gold", 4)) Do_gold(c, 1500, FALSE);
    else if (!strncmp(what, "ride", 4)) {
	if (atoi(what + 5) == MOUNT_DRAGON) Do_world_set_dragon(c->player.lcname);
	Do_mount_grant(c, atoi(what + 5), "Test:");
    }
    else if (!strncmp(what, "hungry", 6)) { c->mountFedUntil = time(NULL) - 1; c->mountHungryTold = FALSE; }
    else if (!strncmp(what, "tired", 5)) { c->mountStamina = 1; }
    else if (!strncmp(what, "tough", 5)) {
	Do_energy(c, 100000, 100000, c->player.shield, 0, FALSE);
    }
    else if (!strncmp(what, "injure", 6)) { c->mountInjured = 6; Do_mount_send(c); }
    else if (!strncmp(what, "post", 4) || !strncmp(what, "warg", 4) || !strncmp(what, "strider", 7)) {
	e = (struct event_t *) Do_create_event();
	e->type = MOVE_EVENT;
	e->arg3 = A_FORCED;
	if (what[0] == 'p') { e->arg1 = 100; e->arg2 = 100; }	/* the first trading post */
	else if (what[0] == 'w') { e->arg1 = 300; e->arg2 = 300; }	/* wargs' country */
	else { e->arg1 = 2700; e->arg2 = 0; }				/* the Dead Marshes (circle 22) */
	Do_file_event(c, e);
    }
    else if (!strncmp(what, "monster", 7)) {
	e = (struct event_t *) Do_create_event();
	e->type = MONSTER_EVENT;
	e->arg1 = MONSTER_SPECIFY;
	e->arg2 = 1;
	e->arg3 = atoi(what + 8);
	Do_file_event(c, e);
    }
    Do_mount_send(c);
}
#endif
