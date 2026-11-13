/*
 * royal.c - what it is worth to rule: Steward, King/Queen and Valar.
 *
 *   regalia   a unique item worn while holding the title (on top of
 *             normal gear), growing with the holder's level
 *   treasury  a share of shop spending flows into the royal coffers and
 *             the ruler is paid a stipend from them as they reign
 *   bonuses   more experience, gold and better gear from fighting
 *   decrees   the Steward feeds the poor, the King calls a world event,
 *             the Valar blesses the whole realm
 */

#include "include.h"

#ifdef GEAR_TEST
#define STIPEND_SECONDS		20	/* test builds: quickly */
#else
#define STIPEND_SECONDS		600	/* ruler is paid every 10 minutes */
#endif
#define FEED_COOLDOWN		(30 * 60)
#define CALL_COOLDOWN		(3 * 60 * 60)
#define CALL_MINUTES		15
#define BLESS_COOLDOWN		(2 * 60 * 60)
#define BLESS_SECONDS		(10 * 60)
#define BLESS_XP		1.25

/************************************************************************
/ who rules
*************************************************************************/

	/* SC_STEWARD, SC_KING or SC_VALAR if this player holds that title
	   right now, otherwise SC_NONE */
int Do_reign_title(struct client_t *c)
{
    if (c->realm == NULL || c->game == NULL) {
	return SC_NONE;
    }
    if (c->player.special_type == SC_VALAR && c->realm->valar == c->game) {
	return SC_VALAR;
    }
    if (c->realm->king == c->game) {
	if (c->player.special_type == SC_KING) return SC_KING;
	if (c->player.special_type == SC_STEWARD) return SC_STEWARD;
    }
    return SC_NONE;
}

static const char *Do_reign_word(struct client_t *c, int title)
{
    switch (title) {
    case SC_STEWARD: return "Steward";
    case SC_KING:    return c->player.gender == FEMALE ? "Queen" : "King";
    case SC_VALAR:   return "Valar";
    }
    return "";
}

/************************************************************************
/ reign bonuses: stronger, richer fighting (monsters still come)
*************************************************************************/

static int Do_valar_blessing_on(struct client_t *c)
{
    return c->realm->valarBlessingEnds > time(NULL);
}

double Do_reign_xp_factor(struct client_t *c)
{
    double f = 1.0;

    switch (Do_reign_title(c)) {
    case SC_STEWARD: f = 1.25; break;
    case SC_KING:    f = 1.5; break;
    case SC_VALAR:   f = 1.5; break;
    }
    if (Do_valar_blessing_on(c)) {
	f *= BLESS_XP;
    }
    return f;
}

double Do_reign_gold_factor(struct client_t *c)
{
    switch (Do_reign_title(c)) {
    case SC_STEWARD: return 1.25;
    case SC_KING:    return 1.5;
    case SC_VALAR:   return 1.25;
    }
    return 1.0;
}

double Do_reign_gear_factor(struct client_t *c)
{
    switch (Do_reign_title(c)) {
    case SC_STEWARD: return 1.5;
    case SC_KING:    return 2.0;
    case SC_VALAR:   return 2.0;
    }
    return 1.0;
}

	/* lowest tier a ruler's monster drops can be */
int Do_reign_min_tier(struct client_t *c)
{
    switch (Do_reign_title(c)) {
    case SC_STEWARD: return TIER_FINE;
    case SC_KING:    return TIER_FINE;
    case SC_VALAR:   return TIER_MASTERWORK;
    }
    return TIER_COMMON;
}

/************************************************************************
/ regalia
*************************************************************************/

	/* fills in the ruler's regalia; FALSE if they hold no title.
	   Regalia grow with the holder's level (not where they stand):
	   they add a share of the energy and strength the character's
	   level gives them. */
int Do_regalia(struct client_t *c, struct gear_item_t *item, char *name)
{
    int title = Do_reign_title(c);
    double energy = MAX(10.0, c->player.max_energy);
    double strength = MAX(10.0, c->player.max_strength);

    memset(item, 0, sizeof *item);
    item->equipped = TRUE;
    item->tier = TIER_ARTIFACT;

    switch (title) {
    case SC_STEWARD:
	strcpy(name, "The Steward's Seal");
	item->slot = SLOT_TRINKET;
	item->ability = AB_FORTUNE;
	item->ability2 = AB_MENDING;
	item->shield = floor(0.15 * energy);
	item->sword = floor(0.08 * strength);
	item->quicksilver = 2;
	return TRUE;

    case SC_KING:
	strcpy(name, c->player.gender == FEMALE ?
		"The Queen's Crown" : "The King's Crown");
	item->slot = SLOT_HELM;
	item->ability = AB_CRUSHING;
	item->ability2 = AB_WARDING;
	item->shield = floor(0.30 * energy);
	item->sword = floor(0.15 * strength);
	item->quicksilver = 3;
	return TRUE;

    case SC_VALAR:
	strcpy(name, "The Halo of the Valar");
	item->slot = SLOT_HELM;
	item->ability = AB_ARCANA;
	item->ability2 = AB_WELLSPRING;
	item->shield = floor(0.25 * energy);
	item->sword = floor(0.15 * strength);
	item->quicksilver = 4;
	return TRUE;
    }
    return FALSE;
}

	/* strength of an ability the regalia gives (artifact strength) */
double Do_regalia_ability(struct client_t *c, int ability)
{
    struct gear_item_t item;
    char name[SZ_LINE];

    if (!Do_regalia(c, &item, name)) {
	return 0;
    }
    return (item.ability == ability || item.ability2 == ability) ? 3.0 : 0;
}

/************************************************************************
/ the treasury
*************************************************************************/

	/* a share of what is spent at shops goes to the royal coffers:
	   the Steward's (capped at 40K) first, the rest to the King's */
void Do_royal_tithe(struct client_t *c, double spent)
{
    double gold = floor(spent * 0.10), room;

	/* spending also helps the local town prosper */
    Do_world_spend(c, spent);

    if (gold < 1) {
	return;
    }
    Do_lock_mutex(&c->realm->kings_gold_lock);
    room = MAX(0.0, 40000.0 - c->realm->steward_gold);
    if (room > 0) {
	double part = MIN(room, MIN(gold, 1000.0));
	c->realm->steward_gold += part;
	gold -= part;
    }
    if (gold > 0) {
	c->realm->kings_gold = MIN(2000000.0, c->realm->kings_gold + gold);
    }
    Do_unlock_mutex(&c->realm->kings_gold_lock);
}

	/* put on or take off the regalia when the title changes */
int Do_royal_refresh(struct client_t *c)
{
    int title = Do_reign_title(c);

    if (title == c->reignTitle) {
	    /* regalia grow as the ruler levels up */
	if (title != SC_NONE && c->player.level != c->reignLevel) {
	    c->reignLevel = c->player.level;
	    if (c->gearLoaded) Do_gear_apply(c, FALSE);
	}
	return FALSE;
    }
    c->reignTitle = title;
    c->reignLevel = c->player.level;
    c->reignPaid = time(NULL);
    if (c->gearLoaded) {
	Do_gear_apply(c, FALSE);
    }
    return TRUE;
}

	/* every few minutes of a reign, the treasurer pays the ruler */
void Do_royal_turn(struct client_t *c)
{
    char line[SZ_LINE];
    int title = Do_reign_title(c);
    time_t now = time(NULL);
    double pay = 0;

    if (Do_royal_refresh(c)) {		/* a new reign, or none */
	return;
    }
    if (title != SC_STEWARD && title != SC_KING) {
	return;
    }
    if (now - c->reignPaid < STIPEND_SECONDS) {
	return;
    }
    c->reignPaid = now;

    Do_lock_mutex(&c->realm->kings_gold_lock);
    if (title == SC_STEWARD) {
	pay = MIN(2000.0, floor(c->realm->steward_gold * 0.10));
	c->realm->steward_gold -= pay;
    }
    else {
	pay = MIN(20000.0, floor(c->realm->kings_gold * 0.02));
	c->realm->kings_gold -= pay;
    }
    Do_unlock_mutex(&c->realm->kings_gold_lock);

    if (pay >= 1) {
	c->player.gold += pay;
	Do_gold(c, 0, TRUE);
	Do_check_weight(c);
	sprintf(line, "The royal treasurer brings you %.0f gold from the coffers.\n",
		pay);
	Do_send_chat_self(c, line);
    }
}

/************************************************************************
/ decrees
*************************************************************************/

static int Do_count_players(struct client_t *c)
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

	/* true (and a message) if a decree is still recharging */
static int Do_decree_waiting(struct client_t *c, time_t ready)
{
    char line[SZ_LINE];
    long left = (long) (ready - time(NULL));

    if (left <= 0 || c->wizard >= 3) {
	return FALSE;
    }
    sprintf(line, "You may do that again in %ld minute%s.\n",
	    (left + 59) / 60, (left + 59) / 60 == 1 ? "" : "s");
    Do_send_line(c, line);
    Do_more(c);
    Do_send_clear(c);
    return TRUE;
}

	/* Steward: open the granary - everyone is healed and cured, and
	   half the Steward's coffers are shared out among them */
void Do_royal_feed(struct client_t *c)
{
    struct event_t *event_ptr;
    char line[SZ_LINE];
    double share = 0;
    int players;

    if (Do_decree_waiting(c, c->realm->feedReady)) {
	return;
    }

    players = MAX(1, Do_count_players(c) - 1);
    Do_lock_mutex(&c->realm->kings_gold_lock);
    share = MIN(2000.0, floor(c->realm->steward_gold * 0.5 / players));
    c->realm->steward_gold -= share * players;
    Do_unlock_mutex(&c->realm->kings_gold_lock);
    c->realm->feedReady = time(NULL) + FEED_COOLDOWN;

    sprintf(line, "Steward %s opens the royal granary to all the realm!\n",
	    c->modifiedName);
    Do_broadcast(c, line);

    event_ptr = (struct event_t *) Do_create_event();
    event_ptr->type = ROYAL_GIFT_EVENT;
    event_ptr->arg1 = 1;
    event_ptr->arg2 = share;
    event_ptr->from = c->game;
    Do_broadcast_event(c, event_ptr);

    Do_send_line(c, "The granary doors swing open.  The realm will remember your kindness.\n");
    Do_more(c);
    Do_send_clear(c);
}

	/* King/Queen: call a world event */
void Do_royal_call_event(struct client_t *c)
{
    struct button_t buttons;
    char line[SZ_LINE];
    long answer;

    if (Do_decree_waiting(c, c->realm->royalCallReady)) {
	return;
    }
    if (Do_current_world_event(c) != WE_NONE) {
	Do_send_line(c, "A world event is already under way.\n");
	Do_more(c);
	Do_send_clear(c);
	return;
    }

    Do_send_line(c, "What shall your decree bring upon the realm?\n");
    Do_clear_buttons(&buttons, 0);
    strcpy(buttons.button[0], "Gold Rush\n");
    strcpy(buttons.button[1], "Champion Hunt\n");
    strcpy(buttons.button[2], "Blood Moon\n");
    strcpy(buttons.button[7], "Cancel\n");
    if (Do_buttons(c, &answer, &buttons) != S_NORM || answer < 0 ||
	    answer > 2) {
	Do_send_clear(c);
	return;
    }
    Do_send_clear(c);

    c->realm->royalCallReady = time(NULL) + CALL_COOLDOWN;
    sprintf(line, "By royal decree of %s %s...\n",
	    Do_reign_word(c, SC_KING), c->modifiedName);
    Do_broadcast(c, line);
    Do_world_event_start(c, answer == 0 ? WE_GOLD_RUSH :
	    answer == 1 ? WE_CHAMPION_HUNT : WE_BLOOD_MOON, CALL_MINUTES);
}

	/* Valar: everyone in the realm gains more experience for a while */
void Do_valar_bless_realm(struct client_t *c)
{
    char line[SZ_LINE];

    if (Do_decree_waiting(c, c->realm->valarBlessReady)) {
	return;
    }
    c->realm->valarBlessReady = time(NULL) + BLESS_COOLDOWN;
    c->realm->valarBlessingEnds = time(NULL) + BLESS_SECONDS;

    sprintf(line, "The Valar %s blesses the realm!  Everyone gains %d%% more experience for %d minutes.\n",
	    c->modifiedName, (int) ((BLESS_XP - 1.0) * 100 + 0.5), BLESS_SECONDS / 60);
    Do_broadcast(c, line);
    Do_send_line(c, "Light pours from your hands across the land.\n");
    Do_more(c);
    Do_send_clear(c);
}

	/* a gift that reached this player (handled between actions) */
void Do_royal_gift(struct client_t *c, struct event_t *the_event)
{
    char line[SZ_LINE];

    if ((int) the_event->arg1 == 1) {
	Do_energy(c, c->player.max_energy + c->player.shield,
		c->player.max_energy, c->player.shield,
		c->battle.force_field, FALSE);
	if (c->player.poison > 0) {
	    Do_poison(c, -c->player.poison);
	}
	if (the_event->from != c->game && the_event->arg2 >= 1) {
	    Do_gold(c, the_event->arg2, TRUE);
	    sprintf(line, "You feast at the royal granary: fully healed and cured, and given %.0f gold.\n",
		    the_event->arg2);
	}
	else {
	    strcpy(line, "You feast at the royal granary and are fully healed and cured.\n");
	}
	Do_send_line(c, line);
	Do_more(c);
	Do_send_clear(c);
    }
	/* Do_handle_event frees the event */
}

/************************************************************************
/ /court: who rules, the coffers, and what decrees are ready
*************************************************************************/

void Do_court_chat(struct client_t *c)
{
    char line[SZ_LINE], ruler[SZ_NAME + 16] = "", valar[SZ_NAME] = "";
    double sg, kg;
    long left;
    time_t now = time(NULL);
    int kingFlag;

    Do_lock_mutex(&c->realm->realm_lock);
    if (c->realm->king != NULL && c->realm->king->description != NULL) {
	strncpy(ruler, c->realm->king->description->name, SZ_NAME - 1);
    }
    kingFlag = c->realm->king_flag;
    strncpy(valar, c->realm->valar_name, SZ_NAME - 1);
    Do_unlock_mutex(&c->realm->realm_lock);

    Do_lock_mutex(&c->realm->kings_gold_lock);
    sg = c->realm->steward_gold;
    kg = c->realm->kings_gold;
    Do_unlock_mutex(&c->realm->kings_gold_lock);

    sprintf(line, "%s: %s.  Valar: %s.\n", kingFlag ? "Monarch" : "Steward",
	    ruler[0] ? ruler : "none", valar[0] ? valar : "none");
    Do_send_chat_self(c, line);
    sprintf(line, "Coffers: Steward %.0f gold, King %.0f gold.\n", sg, kg);
    Do_send_chat_self(c, line);

    if ((left = (long) (c->realm->valarBlessingEnds - now)) > 0) {
	sprintf(line, "The Valar's blessing: +25%% experience for %ld more minute%s.\n",
		(left + 59) / 60, (left + 59) / 60 == 1 ? "" : "s");
	Do_send_chat_self(c, line);
    }

    switch (Do_reign_title(c)) {
    case SC_STEWARD:
	left = (long) (c->realm->feedReady - now);
	sprintf(line, "Feed the Poor: %s.\n", left > 0 ? "recharging" : "ready");
	Do_send_chat_self(c, line);
	break;
    case SC_KING:
	left = (long) (c->realm->royalCallReady - now);
	sprintf(line, "Call an Event: %s.\n", left > 0 ? "recharging" : "ready");
	Do_send_chat_self(c, line);
	break;
    case SC_VALAR:
	left = (long) (c->realm->valarBlessReady - now);
	sprintf(line, "Bless the Realm: %s.\n", left > 0 ? "recharging" : "ready");
	Do_send_chat_self(c, line);
	break;
    }
}
