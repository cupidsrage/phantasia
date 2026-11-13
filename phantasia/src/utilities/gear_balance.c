/*
 * gear_balance.c - prints gear vs. monster balance by circle.
 *
 * Build from phantasia/src after a normal 'make':
 *   objcopy --redefine-sym main=server_main main.o main_nomain.o
 *   gcc -D_REENTRANT -D_GNU_SOURCE -fcommon -w -O2 -I. \
 *       -o gear_balance utilities/gear_balance.c \
 *       $(ls *.o | grep -v '^gear.o$\|^main.o$') -pthread -lm
 *   ./gear_balance
 *
 * Columns: gold per kill from selling gear vs. from treasure, the price
 * of Fine armor and kills of treasure gold to afford it, the average
 * monster hit, Fine armor+helm energy and how many hits that absorbs,
 * and the Fine weapon's damage multiplier.
 */
#include "gear.c"

extern char randomStateBuffer[];
extern struct random_data randData;

/* which monster a random encounter picks at a circle (copied from Do_monster) */
static int pick_type(double circle) {
    int t;
    if (circle >= 36) t = (int) ROLL(14.0, 86.0);
    else if (circle == 27 || circle == 28) { t = (int) ROLL(50.0, 50.0); if (t < 52) t = 15; }
    else if (circle < 31 && circle > 24) { t = (int) (ROLL(50.0, 25.0) + ROLL(0.0, 26.0)); if (t < 52) t = 15; }
    else if (circle > 19) t = (int) ROLL(0.0, 17.0);
    else if (circle > 15) t = (int) (ROLL(0.0, 50.0) + ROLL(14.0, 37.0));
    else if (circle > 9) t = (int) (ROLL(0.0, (-8.0 + circle * 4)) + ROLL(14.0, 26.0));
    else if (circle > 7) t = (int) ROLL(14.0, (18.0 + circle * 4));
    else if (circle > 4) t = (int) ROLL(14.0, 46.0);
    else if (circle == 4) t = (int) ROLL(14.0, 38.0);
    else t = (int) ROLL(14.0, (17.0 + circle * 4));
    if (t >= 100) t = 15;
    return t;
}

int main(void) {
    static struct realm_t realm;
    struct gear_item_t it;
    double circles[] = { 1, 3, 5, 10, 15, 20, 25, 30, 40, 50, 75, 100 };
    int ci, i, N = 20000;

    initstate_r(12345, randomStateBuffer, STATELEN, &randData);
    Do_load_monster_file(&realm);

    printf("%-6s | %9s %9s | %9s %9s | %8s %10s %7s | %7s\n", "circle",
	"sell/kill", "gold/kill", "Fine armr", "kills", "mon hit", "armr+helm", "hits", "weapon");
    for (ci = 0; ci < (int) (sizeof(circles) / sizeof(circles[0])); ci++) {
	double C = circles[ci], sell = 0, csell = 0, gold = 0, hit = 0, ah = 0, buy = 0, sw = 0;
	for (i = 0; i < N; i++) {
	    Do_gear_make(&it, -1, C, TIER_COMMON);            sell += Do_gear_sell_price(&it);
	    Do_gear_make(&it, -1, C + 2, TIER_MASTERWORK);    csell += Do_gear_sell_price(&it);
	    memset(&it, 0, sizeof(it)); it.slot = SLOT_ARMOR; it.tier = TIER_FINE;
	    Do_gear_roll_stats(&it, C, Do_tier_quality(TIER_FINE)); ah += it.shield; buy += Do_gear_buy_price(&it);
	    memset(&it, 0, sizeof(it)); it.slot = SLOT_HELM; it.tier = TIER_FINE;
	    Do_gear_roll_stats(&it, C, Do_tier_quality(TIER_FINE)); ah += it.shield;
	    memset(&it, 0, sizeof(it)); it.slot = SLOT_WEAPON; it.tier = TIER_FINE;
	    Do_gear_roll_stats(&it, C, Do_tier_quality(TIER_FINE)); sw += it.sword;
	    { int t = pick_type(C); struct monster_t *m = &realm.monster[t]; double tt = m->treasure_type;
	      hit += (1.0 + C / 2.0) * m->strength / 2.0;
	      if (tt > 0) gold += (1.0 - (0.2 + pow(0.4, C / 3.0))) * 0.35 * (tt > 7 ? 0 : 1) * (tt * 10.0 + tt * tt * 4.0 * (C - 1.0)); }
	}
	sell /= N; csell /= N; gold /= N; hit /= N; ah /= N; buy /= N; sw /= N;
	/* per kill: 7% of kills drop gear, 3% of encounters are champions */
	printf("%-6.0f | %9.1f %9.1f | %9.0f %9.0f | %8.0f %10.0f %7.1f | x%6.2f\n", C,
	    0.07 * sell + 0.03 * csell, gold, buy, buy / MAX(1.0, gold), hit, ah, ah / hit,
	    1 + sqrt(sw) * N_SWORDPOWER);
    }
    return 0;
}
