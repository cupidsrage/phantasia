/*
 * Phantasia Reimagined: autoplay (for the accounts the server allows).
 *
 * Plays a character the way a careful player would, pressing only the
 * buttons and answering only the questions a player could:
 *
 *   - rests before hunting, and hunts at a circle that suits the character,
 *     moving out as fights get easy and back in when they hurt;
 *   - fights with Melee, heals with Mending Light or Drain Life, blasts
 *     tough monsters with Fireball or Frost Lance, and flees (Evade, then
 *     Luckout) when the next hits could kill it, or from champions;
 *   - wears better gear as it finds it, and swaps in better pieces from
 *     the bag;
 *   - walks to the nearest trading post when it has gold to spend or is
 *     getting weighed down, then: sells spare gems and gear, buys the
 *     next bag, armory upgrades, amulets, mana for its spells and, at
 *     the far posts, a blessing; gives surplus gold to the realm project;
 *   - pays medics to cure poison or blindness, reads scrolls, picks up
 *     treasure, keeps clear of sin (no pillaging), and chases its daily
 *     quest and the Call to Arms region.
 *
 * It hands control back when the character dies, when something new to it
 * comes up, or as soon as the player presses a button.
 *
 * client.js calls window.PhantasiaAutoplay(host) with the few things it
 * needs (press a button, answer a dialog, read the gear) and feeds it what
 * the server sends.
 */
window.PhantasiaAutoplay = function (host) {
  "use strict";

  var SPEEDS = { slow: 1500, normal: 700, fast: 250 };
  var COMPASS_DIR = [[-1, 1], [0, 1], [1, 1], [-1, 0], null, [1, 0], [-1, -1], [0, -1], [1, -1]];
  var BAGS = [
    ["Belt Pouch", 0, 1], ["Satchel", 400, 1.5], ["Leather Rucksack", 1500, 2],
    ["Traveler's Pack", 4000, 3], ["Adventurer's Pack", 9000, 4], ["Reinforced Pack", 18000, 6],
    ["Bag of Holding", 40000, 8], ["Bag of the Endless Road", 80000, 12]
  ];
  /* the eight regions, by compass angle (east = 0, counter-clockwise) */
  var REGIONS = ["eastern marches", "dawnwood", "northern reach", "frostfells",
    "western wastes", "sunken fens", "southern expanse", "ember coast"];
  var DEATH = /You died|You were killed by|beyond repair|disemboweled|You're dead|fatal blow|found you easy prey|dies of old age|died in glorious battle|You were turned into|failure -- you've died/;
  /* questions a careful player says yes to (everything else: no) */
  var YES = [/frisk the body/i, /pick (them|it) up\?/i, /inspect it\?/i, /continue waiting/i,
    /Will you read it\?/i, /still wish to move there/i];

  var st = {
    allowed: false, on: false, speed: "normal", timer: null,
    x: 0, y: 0, level: 0, e: 0, emax: 1, mana: 0, gold: 0, gems: 0, spd: 0, spdmax: 0,
    lines: [], hits: [], resting: false,
    inFight: false, fightStartE: 0, evaded: false, lucked: false,
    target: 1, easy: 0, heading: Math.floor(Math.random() * 4),
    fights: 0, startLevel: null, kills: 0,
    plan: [], visit: null, trip: null, lastShopFight: -999, badPosts: {},
    spells: {}, spellsAt: -1, noMana: {}, moveScale: 1,
    quest: null, callRegion: -1, fallen: {}, worldAt: 0, blessed: false, amulets: 0, unknown: 0
  };
  try { st.speed = localStorage.getItem("phantasia-autoplay-speed") || "normal"; } catch (e) {}
  if (!SPEEDS[st.speed]) st.speed = "normal";

  /* ------------------------------------------------------------ helpers */

  function el(id) { return document.getElementById(id); }
  function say(text, kind) {
    var s = el("autoplay-status");
    if (!s) return;
    s.textContent = text;
    s.title = text;
    s.className = "autoplay-status" + (kind ? " " + kind : "");
  }
  function dist(x1, y1, x2, y2) { return Math.hypot(x1 - x2, y1 - y2); }
  function circleAt(x, y) { return Math.floor(Math.hypot(x, y) / 125) + 1; }
  function circle() { return circleAt(st.x, st.y); }
  /* how far out a character of this level belongs (fights decide within it) */
  function cap() { return Math.max(1, 1 + Math.floor(st.level / 2.5)); }
  function recent(n) { return st.lines.slice(-n).join("\n"); }
  function frac() { return st.e / (st.emax || 1); }
  function reserve() { return Math.floor(60 + st.level * 25); }   /* for medics */
  function maxHit() { return Math.max(st.emax * 0.1, Math.max.apply(null, st.hits.concat([0]))); }

  /* how far one step can go (the server's rule), a little under */
  function maxMove() {
    var c = circle(), m;
    if (c > 19 && c < 36) m = Math.min(Math.ceil(st.level / 50) + 1.5, 10);
    else m = Math.min(Math.floor(st.level * 1.5 + 1.5), 100);
    return Math.max(1, Math.floor(m * st.moveScale));
  }

  /* gear */
  function worth(stats, abilities) {
    if (!stats) return 0;
    var v = 0, m;
    if ((m = /\+(\d+) energy/.exec(stats))) v += +m[1];
    if ((m = /\+(\d+) damage/.exec(stats))) v += 2 * m[1];
    if ((m = /\+(\d+) quicksilver/.exec(stats))) v += 3 * m[1];
    if (abilities) v *= 1.15;
    return v;
  }
  function itemWorth(it) { return it ? worth(it.stats, it.ab1 || it.ab2) : 0; }
  function gear() { return host.gear(); }
  function worn(slot) { return gear().items.filter(function (g) { return g.equipped && g.slot === slot; })[0]; }
  function bagIndex() {
    var name = gear().bag || "";
    for (var i = BAGS.length - 1; i >= 0; i--) if (name.indexOf(BAGS[i][0]) >= 0) return i;
    return 0;
  }
  function nextBag() { var i = bagIndex(); return i + 1 < BAGS.length ? BAGS[i + 1] : null; }
  /* gold the character carries before it slows (the lightest class's tote) */
  function capacity() { return (1000 + Math.max(0, st.level) * 125) * BAGS[bagIndex()][2]; }
  function load() { return st.gold + st.gems / 2; }
  /* backpack pieces worth selling: not artifacts, not better than what's worn */
  function spareGear() {
    return gear().items.filter(function (g) {
      return !g.equipped && g.tier < 5 && itemWorth(g) <= itemWorth(worn(g.slot));
    });
  }

  /* trading posts: the diagonals at 100n^2, the main roads at the same distance */
  function nearestPost() {
    var best = null, now = Date.now();
    for (var n = 1; n <= 40; n++) {
      var d = 100 * n * n, a = Math.round(d * Math.SQRT2);
      [[d, d], [-d, d], [d, -d], [-d, -d], [a, 0], [-a, 0], [0, a], [0, -a]].forEach(function (p) {
        if ((st.badPosts[p[0] + "," + p[1]] || 0) > now) return;
        /* a fallen town's posts lie in ruins */
        var r = ((Math.floor((Math.atan2(p[1], p[0]) + Math.PI / 8) / (Math.PI / 4)) % 8) + 8) % 8;
        if (st.fallen[r]) return;
        var k = dist(st.x, st.y, p[0], p[1]);
        if (!best || k < best.d) best = { x: p[0], y: p[1], d: k };
      });
      if (best && best.d < d) break;
    }
    return best;
  }

  /* the compass step that best heads (dx, dy) */
  function stepToward(dx, dy) {
    var best = -1, bestDot = -Infinity, len = Math.hypot(dx, dy) || 1;
    COMPASS_DIR.forEach(function (d, k) {
      if (!d) return;
      var dot = (d[0] * dx + d[1] * dy) / (len * Math.hypot(d[0], d[1]));
      if (dot > bestDot) { bestDot = dot; best = k; }
    });
    return best;
  }
  /* outward, leaning toward the Call to Arms region (richer rewards) */
  function outward() {
    var r = Math.hypot(st.x, st.y), dx, dy;
    if (r < 1) { dx = [0, 1, 0, -1][st.heading]; dy = [1, 0, -1, 0][st.heading]; }
    else { dx = st.x / r; dy = st.y / r; }
    if (st.callRegion >= 0) {
      var a = st.callRegion * Math.PI / 4;
      dx += 0.8 * Math.cos(a); dy += 0.8 * Math.sin(a);
    }
    return stepToward(dx, dy);
  }

  /* ------------------------------------------------------- pressing */

  var lastLabels = [], lastCompass = false;
  function has(s) { return lastLabels.indexOf(s); }
  function hasAny(list) { for (var i = 0; i < list.length; i++) if (has(list[i]) >= 0) return has(list[i]); return -1; }
  /* the last few decisions, for anyone curious (window.PhantasiaAutoplayTrace) */
  var trace = window.PhantasiaAutoplayTrace = [];
  function press(i) {
    if (i < 0) return;
    trace.push(lastLabels.filter(Boolean).join("|") + " -> " + lastLabels[i]);
    if (trace.length > 60) trace.shift();
    host.press(i);
  }
  function pressCompass(k) { if (lastCompass) host.compass(k); }

  /* ------------------------------------------------------- the plan */

  /* A plan is a list of steps run across menus: a label to press, or a
     function that picks a button (or returns -1 to drop the step).  A
     menu that doesn't fit the next step drops the plan. */
  function runPlan() {
    while (st.plan.length) {
      var step = st.plan[0];
      if (typeof step === "function") {
        st.plan.shift();                    /* a step may queue more steps */
        var i = step();
        if (i >= 0) { press(i); return true; }
        continue;
      }
      if (has(step) >= 0) { st.plan.shift(); press(has(step)); return true; }
      /* "More" between steps */
      if (has("More") >= 0 && lastLabels.filter(Boolean).length === 1) { press(has("More")); return true; }
      st.plan = [];
    }
    return false;
  }

  /* ------------------------------------------------------- shopping */

  /* which towns have fallen (the realm's public status, as on Realm Today) */
  function checkWorld() {
    if (Date.now() - st.worldAt < 5 * 60 * 1000 || !window.fetch) return;
    st.worldAt = Date.now();
    fetch("/api/world").then(function (r) { return r.json(); }).then(function (w) {
      var fallen = {};
      (w.regions || []).forEach(function (reg) {
        var name = (reg.name || "").toLowerCase();
        REGIONS.forEach(function (rn, i) { if (name.indexOf(rn) >= 0 && /fallen/i.test(reg.townState || "")) fallen[i] = true; });
      });
      st.fallen = fallen;
    }).catch(function () {});
  }

  function wantsToShop() {
    if (st.fights - st.lastShopFight < 15 && load() < 0.8 * capacity()) return false;
    var spend = st.gold + Math.min(st.gems, st.level * st.level + 1) * 1000 - reserve();
    var nb = nextBag();
    if (load() > 0.75 * capacity()) return true;
    if (nb && spend >= nb[1]) return true;
    if (spareGear().length >= 2) return true;
    return spend >= 300 + st.level * 40;
  }

  function startTrip() {
    var p = nearestPost();
    if (!p) return false;
    /* only when the walk is reasonable */
    if (p.d / maxMove() > 45 && load() < 0.9 * capacity()) return false;
    st.trip = p;
    return true;
  }

  /* the tasks for a visit, in order */
  function planVisit() {
    var tasks = [], nb = nextBag();
    if (st.gems > 0 && nb && st.gold < nb[1] + reserve()) tasks.push("gems");
    if (nb) tasks.push("bag");
    tasks.push("armory");
    if (st.level >= 3 && st.amulets < 3) tasks.push("amulets");
    if (Object.keys(st.spells).length) tasks.push("mana");
    if (!st.blessed) tasks.push("blessing");
    tasks.push("donate");
    st.visit = { tasks: tasks, items: [] };
    st.lastShopFight = st.fights;
  }

  function nextVisitTask() {
    var v = st.visit, nb;
    while (v.tasks.length) {
      var t = v.tasks.shift();
      v.task = t;
      switch (t) {
        case "gems":
          if (st.gems > 0) { say("Selling gems.", "on"); st.plan = ["Sell Gems"]; return true; }
          break;
        case "bag":
          nb = nextBag();
          if (nb && st.gold - reserve() >= nb[1]) {
            say("Buying a " + nb[0] + ".", "on");
            st.plan = ["Bags", "Upgrade", function () { return has("Upgrade") >= 0 ? has("Leave") : -1; }];
            return true;
          }
          break;
        case "armory":
          if (spareGear().length || st.gold - reserve() >= 100) {
            say("Visiting the armory.", "on");
            st.plan = ["Armory", armoryStep];
            return true;
          }
          break;
        case "amulets":
          if (st.gold - reserve() >= 600) { say("Buying amulets against cursed treasure.", "on"); st.plan = ["Purchase", orCancel("Amulet")]; return true; }
          break;
        case "mana":
          if (st.mana < manaWanted() && st.gold - reserve() >= 50) { say("Buying mana for spells.", "on"); st.plan = ["Purchase", orCancel("Mana")]; return true; }
          break;
        case "blessing":
          if (st.gold - reserve() >= 15000) { st.plan = ["Purchase", orCancel("Blessing")]; return true; }
          break;
        case "donate":
          if (load() > 0.6 * capacity()) { say("Giving spare gold to the realm project.", "on"); st.plan = ["Realm Project"]; return true; }
          break;
      }
    }
    return false;
  }

  /* press label if this shop sells it, else back out */
  function orCancel(label) { return function () { return has(label) >= 0 ? has(label) : hasAny(["Cancel"]); }; }

  function manaWanted() { return Math.min(400 + st.level * 20, 2000); }

  /* the armory: sell spare pieces, then buy the best upgrade we can afford */
  function armoryStep() {
    if (has("Sell Gear") < 0) return -1;
    var spare = spareGear();
    if (spare.length) {
      var backpack = gear().items.filter(function (g) { return !g.equipped; });
      var k = backpack.indexOf(spare[0]);
      /* the list: 7 to a page, or 6 and "More..." */
      var pickSpare = function () {
        var perPage = backpack.length <= 7 ? 7 : 6;
        if (k >= perPage) {
          if (has("More...") < 0) return hasAny(["Cancel"]);
          k -= perPage;
          st.plan.unshift(pickSpare);
          return has("More...");
        }
        return lastLabels[k] ? k : hasAny(["Cancel"]);
      };
      st.plan.unshift(pickSpare, armoryStep);
      say("Selling a spare " + spare[0].slot.toLowerCase() + ".", "on");
      return has("Sell Gear");
    }
    /* the wares: six lines "<item> (Slot) <stats> - N gold" */
    var wares = st.lines.filter(function (l) { return / - \d+ gold$/.test(l); }).slice(-6), best = -1, gain = 0;
    var budget = st.gold - reserve();
    wares.forEach(function (line, i) {
      var m = /^(.*) \((Weapon|Armor|Helm|Boots|Trinket)\) (.*) - (\d+) gold$/.exec(line);
      if (!m) return;
      var price = +m[4], g = worth(m[3], / - /.test(m[3])) - itemWorth(worn(m[2]));
      if (price <= budget && g > Math.max(2, 0.08 * itemWorth(worn(m[2]))) && g > gain) { gain = g; best = i; }
    });
    if (best >= 0 && lastLabels[best]) {
      say("Buying an upgrade at the armory.", "on");
      st.plan.unshift(armoryStep);
      return best;
    }
    return has("Leave");
  }

  /* ------------------------------------------------------- spells */

  function learnSpells() {
    ["Magic Bolt", "Frost Lance", "Mending Light", "Drain Life", "Fireball", "Force Field", "Haste", "Increase Might"].forEach(function (s) {
      if (has(s) >= 0) st.spells[s] = true;
    });
  }
  var SPELL_COST = { "Frost Lance": 30, "Mending Light": 50, "Drain Life": 65, "Fireball": 140 };
  /* healing may spend down to the cost; attacks keep enough back to heal */
  function canCast(s, attack) {
    if (!st.spells[s]) return false;
    if (st.noMana[s] && st.mana < st.noMana[s]) return false;
    return st.mana >= SPELL_COST[s] * (attack ? 3 : 1.5);
  }
  function castPlan(spell) {
    st.lastSpell = spell;
    st.plan = ["Spell", "More Spells", spell];
  }

  /* ------------------------------------------------------- deciding */

  function fightOver() {
    st.inFight = false;
    st.fights++;
    var lost = (st.fightStartE - st.e) / st.emax, left = frac();
    /* a fight we had to run from, or that hurt, says: closer in */
    if (st.fightDanger || lost > 0.45 || left < 0.3) {
      st.target = Math.max(1, Math.min(st.target, circle()) - 1);
      st.easy = 0;
    } else if (st.fightKilled && lost < 0.15 && left > 0.75) {
      if (++st.easy >= 4) { st.target = Math.min(cap(), st.target + 1); st.easy = 0; }
    } else st.easy = 0;
  }

  function mainMenu() {
    if (st.inFight) fightOver();
    checkWorld();
    var e = frac(), c = circle();

    /* wear anything in the bag that beats what's on */
    var swap = gear().items.filter(function (g) {
      return !g.equipped && itemWorth(g) > itemWorth(worn(g.slot)) + 0.5;
    })[0];
    if (swap && !st.swapped) {
      st.swapped = true;
      host.wear(swap);
      say("Putting on a better " + swap.slot.toLowerCase() + " from the bag.", "on");
      setTimeout(function () { st.swapped = false; }, 1500);
    }

    /* rest until nearly full: a second monster can follow the first */
    if (e < 0.85 || st.e < 3 * maxHit()) st.resting = true;
    if (st.resting && e >= 0.98) st.resting = false;
    if (st.resting && has("Rest") >= 0) { say("Resting: energy " + Math.round(e * 100) + "%.", "on"); return press(has("Rest")); }

    /* standing at a trading post */
    if (has("Enter Post") >= 0) {
      if (st.trip || wantsToShop()) {
        st.trip = null;
        planVisit();
        say("Entering the trading post.", "on");
        st.atPost = { x: st.x, y: st.y };
        return press(has("Enter Post"));
      }
      st.heading = (st.heading + 1) % 4;
      return pressCompass([1, 5, 7, 3][st.heading]);
    }

    /* off to shop */
    if (!st.trip && wantsToShop()) startTrip();
    if (st.trip) {
      var d = dist(st.x, st.y, st.trip.x, st.trip.y);
      if (d < 0.5) {   /* here, but no trade: the town must have fallen */
        st.badPosts[st.trip.x + "," + st.trip.y] = Date.now() + 30 * 60 * 1000;
        st.trip = null;
        return press(has("Hunt") >= 0 ? has("Hunt") : has("Rest"));
      }
      say("Walking to the trading post at (" + st.trip.x + ", " + st.trip.y + "), " + Math.ceil(d) + " away.", "on");
      var step = Math.min(d, maxMove());
      var tx = st.x + (st.trip.x - st.x) * step / d, ty = st.y + (st.trip.y - st.y) * step / d;
      if (d <= maxMove()) { tx = st.trip.x; ty = st.trip.y; }
      else {
        tx = Math.round(tx); ty = Math.round(ty);
        while (dist(st.x, st.y, tx, ty) > maxMove() && (tx !== Math.round(st.x) || ty !== Math.round(st.y))) {
          tx += Math.sign(st.x - tx) * (Math.abs(st.x - tx) >= 1 ? 1 : 0);
          ty += Math.sign(st.y - ty) * (Math.abs(st.y - ty) >= 1 ? 1 : 0);
        }
      }
      st.moveTo = { x: tx, y: ty };
      return press(has("Move To"));
    }

    /* the daily quest: a journey to a circle, if it's safe */
    var want = st.target;
    if (st.quest && st.quest.reach && !st.quest.done && st.quest.reach <= cap()) want = Math.max(want, st.quest.reach);

    if (lastCompass && c < want) { say("Heading out to circle " + want + " (now " + c + ").", "on"); return pressCompass(outward()); }
    if (lastCompass && c > want) { say("Pulling back toward circle " + want + " (now " + c + ").", "on"); return pressCompass(stepToward(-st.x, -st.y)); }
    if (has("Hunt") >= 0) {
      say("Hunting in circle " + c + ". Fights " + st.fights + ", levels gained " +
        Math.max(0, Math.floor(st.level - (st.startLevel || 0))) + ", gold " + Math.floor(st.gold) + ".", "on");
      return press(has("Hunt"));
    }
    if (lastCompass) return pressCompass(outward());
    return stop("Autoplay stopped: it can't hunt here.");
  }

  /* A fight, decided with the game's own odds (the server sends them for
     autoplay accounts, from the same formulas the fight uses):
       q      the chance the monster acts before us each tick, so it gets
              q/(1-q) hits per action of ours, each (strength+1)/2 on average;
       melee  our average melee damage, so n = energy/melee actions to win;
       luck   the luckout chance (brains against the monster's brains): a
              win at once, or a wasted action.  Luckout beats melee when
              luck x n > 1;
       evade  the chance of getting away (quickness and brains);
       spells each one's mana, average damage and healing.
     Every option is costed as the energy we expect to lose before the
     monster is dead; we take the cheapest, unless we can't afford the
     fight at all, when the best way out wins. */
  function fight() {
    var e = frac(), F = st.odds;
    if (!st.inFight) { st.inFight = true; st.fightStartE = st.e; st.fightDanger = false; st.fightKilled = false; }
    st.odds = null;

    /* learn this level's spells once, in an easy moment */
    if (!F && st.spellsAt !== Math.floor(st.level) && e > 0.9 && st.mana >= 30 && has("Spell") >= 0 &&
        (!F || F.q < 0.5)) {
      st.spellsAt = Math.floor(st.level);
      st.learning = true;
      say("Checking which spells I know.", "on");
      return press(has("Spell"));
    }
    if (!F) { say("Fighting.", "on"); return press(has("Melee")); }

    var q = Math.min(F.q, 0.95), per = q / (1 - q);
    var D = per * (F.str + 1) / 2;                  /* expected damage per action of ours */
    var worst = F.str * Math.max(1, Math.ceil(per)); /* a bad round */
    var n = Math.max(1, Math.ceil(F.energy / Math.max(1, F.melee)));
    var missing = Math.max(0, st.emax - st.e);
    var opts = [{ label: "Melee", cost: n * D, acts: n, why: "Melee: " + n + " blow" + (n > 1 ? "s" : "") + " to win" }];

    if (F.luck > 0 && has("Luckout") >= 0) {
      opts.push({ label: "Luckout", cost: D + (1 - F.luck) * n * D, acts: 1 + (1 - F.luck) * n,
        why: "Luckout: " + Math.round(F.luck * 100) + "% (brains " + F.brains + " to beat)" });
    }
    F.spells.forEach(function (sp) {
      if (st.mana < sp.mana || !st.spells[sp.name] && !F.spellsFromServer) return;
      /* mana is cheap at the posts, but keep enough back to heal */
      var manaCost = st.mana >= 3 * sp.mana ? 0.05 * sp.mana * (D > 0 ? 1 : 0) : 0.5 * sp.mana;
      if (sp.name === "Mending Light") {
        if (missing < 1) return;
        var h = Math.min(sp.heal, missing);
        opts.push({ label: sp.name, spell: true, cost: D + n * D - h + manaCost, acts: n + 1, heal: h,
          why: "Mending Light: +" + Math.round(h) + " energy" });
        return;
      }
      var left = Math.max(0, F.energy - sp.dmg), ns = 1 + Math.ceil(left / Math.max(1, F.melee));
      if (left <= 0) ns = 1;
      var healBack = Math.min(sp.heal, missing);
      opts.push({ label: sp.name, spell: true, cost: ns * D - healBack + manaCost, acts: ns,
        why: sp.name + ": about " + Math.round(sp.dmg) + " damage" });
    });

    opts.sort(function (a, b) { return a.cost - b.cost || a.acts - b.acts; });
    var pick = opts[0];

    /* can we afford it?  Before each action of ours the monster acts k
       times in a row with chance q^k; k hits of up to its strength drop
       us.  Over the actions this fight needs (and with a flock, the friend
       who comes next) that's our chance of dying: over 2% is too much */
    var flock = /friend appears/.test(recent(4));
    var k = Math.max(1, Math.ceil(st.e / Math.max(1, F.str)));
    var acts = pick.acts + (flock ? 2 : 0);
    var pDie = 1 - Math.pow(1 - Math.pow(q, k), acts);
    var risky = pDie > 0.02 || st.e - pick.cost < worst;
    if (risky) {
      st.fightDanger = true;
      var luck = opts.filter(function (o) { return o.label === "Luckout"; })[0];
      var heal = opts.filter(function (o) { return o.label === "Mending Light"; })[0];
      var evade = has("Evade") >= 0 ? F.evade : 0;
      var hopeless = st.e < pick.cost;   /* we expect to lose this fight */
      if (heal && heal.heal > 1.5 * D && st.e > worst && !hopeless) pick = heal;
      else if (luck && F.luck >= evade && (F.luck >= 0.2 || hopeless)) pick = luck;
      else if (evade >= 0.2 || (hopeless && evade > 0)) pick = { label: "Evade", why: "Getting away: " + Math.round(evade * 100) + "%" };
      else if (luck && hopeless) pick = luck;
    }
    say((risky ? "In danger (" + Math.round(pDie * 100) + "% risk). " : "") + pick.why + ". Energy " + Math.round(e * 100) + "%.", risky ? "warn" : "on");
    if (pick.spell) { castPlan(pick.label); return runPlan(); }
    return press(has(pick.label));
  }

  function act() {
    if (!st.on || host.pending() !== "buttons") return;
    var L = lastLabels, n = L.filter(Boolean).length, text = recent(6);

    if (runPlan()) return;

    /* the spell menus: note what we know, then cast or back out */
    var page1 = has("Magic Bolt") >= 0 || has("All or Nothing") >= 0;
    var page2 = has("Mending Light") >= 0 || has("Frost Lance") >= 0 || has("Drain Life") >= 0;
    if (page1 || page2) {
      learnSpells();
      if (st.learning && page1 && has("More Spells") >= 0 && !st.seenPage2) { st.seenPage2 = true; return press(has("More Spells")); }
      st.learning = false; st.seenPage2 = false;
      if (page2) {
        if (canCast("Frost Lance")) { st.lastSpell = "Frost Lance"; return press(has("Frost Lance")); }
        return press(has("Back"));
      }
      if (has("Cancel") >= 0) return press(has("Cancel"));
      if (has("Magic Bolt") >= 0) { st.boltMana = Math.max(1, Math.floor(st.mana * 0.1)); return press(has("Magic Bolt")); }
      return press(has("All or Nothing"));
    }

    /* main menu */
    if (has("Info") >= 0 && has("Move To") >= 0) { st.visit && (st.visit = null); return mainMenu(); }

    /* a fight */
    if (has("Melee") >= 0) return fight();

    /* inside a trading post */
    if (has("Purchase") >= 0 && has("Bags") >= 0) {
      if (!st.visit) planVisit();
      if (nextVisitTask()) return runPlan();
      st.visit = null;
      say("Done shopping.", "on");
      return press(has("Leave"));
    }

    /* a found piece of gear */
    if ((has("Leave") >= 0 || has("Refuse") >= 0) && (has("Equip") >= 0 || has("Stow") >= 0 || has("Swap") >= 0)) {
      var found = after("You found:"), wear = /You are wearing:/.test(recent(8)) ? after("You are wearing:") : "";
      var artifact = has("Refuse") >= 0;
      var better = !wear || worth(found, / - /.test(found)) > worth(wear, / - /.test(wear)) || artifact;
      if (better && has("Equip") >= 0) { say("Wearing an upgrade.", "on"); return press(has("Equip")); }
      if (better && has("Swap") >= 0) { say("Swapping in an upgrade.", "on"); return press(has("Swap")); }
      /* keep spare pieces to sell while there's room */
      if (has("Stow") >= 0 && (artifact || gear().items.filter(function (g) { return !g.equipped; }).length < gear().capacity - 1)) {
        say(artifact ? "Keeping an artifact." : "Stowing it to sell later.", "on");
        return press(has("Stow"));
      }
      say("Leaving gear that isn't better.", "on");
      return press(has("Leave"));
    }

    /* Wayland Smith improves a piece of equipment */
    if (/Wayland Smith offers/.test(text)) {
      var pick = hasAny(["Weapon", "Armor", "Boots"]);
      if (pick >= 0) return press(pick);
    }

    /* treasure: gold and gems */
    if (has("Yes") >= 0 && has("No to All") >= 0) return press(has("Yes"));

    /* a yes or no question */
    if (has("Yes") >= 0 && has("No") >= 0 && n === 2) {
      if (/continue playing/i.test(text)) return stop("Autoplay stopped: your character's turn is over.", "warn");
      var q = st.lines.slice(-3).join(" ");
      var bm = /blessing requires a (\d+) gp/.exec(q);
      if (bm) return press(st.gold - reserve() >= +bm[1] ? has("Yes") : has("No"));
      return press(YES.some(function (r) { return r.test(q); }) ? has("Yes") : has("No"));
    }

    /* "More" to carry on */
    if (has("More") >= 0 && n === 1) return press(has("More"));

    /* a menu we don't need: back out of it */
    var out = hasAny(["Leave", "Back", "Cancel", "Done"]);
    if (out >= 0 && ++st.unknown < 4) return press(out);

    stop("Autoplay paused: this choice needs you (" + L.filter(Boolean).join(", ") + ").", "warn");
  }

  function after(label) {
    for (var i = st.lines.length - 2; i >= 0; i--) if (st.lines[i] === label) return st.lines[i + 1];
    return "";
  }

  /* typed answers: amounts to buy, offers to medics, where to move */
  function answerDialog(kind, prompt) {
    var m, v;
    if (kind === "coords") {
      if (st.moveTo) { var t = st.moveTo; st.moveTo = null; host.answerCoords(t.x, t.y); return true; }
      return false;
    }
    if (kind !== "string") return false;

    if (/How much will you offer to be cured/.test(prompt)) {
      v = Math.max(Math.ceil(st.level), Math.floor(st.gold * 0.6));
      if (v > st.gold) v = Math.floor(st.gold);
      say("Paying a medic to cure poison.", "on");
      host.answerString(String(v)); return true;
    }
    if (/heal your blindness/.test(prompt)) {
      v = Math.floor(st.gold * 0.6);
      if (v < st.level * 10) v = 0;
      host.answerString(String(v)); return true;
    }
    if ((m = /Mana is one per (\d+(?:\.\d+)?) gold piece.*\((\d+) max\)/.exec(prompt))) {
      v = Math.max(0, Math.min(+m[2], manaWanted() - st.mana, Math.floor((st.gold - reserve()) / +m[1])));
      host.answerString(String(Math.floor(v))); return true;
    }
    if ((m = /An amulet costs (\d+) gp.*\((\d+) max\)/.exec(prompt))) {
      v = Math.max(0, Math.min(+m[2], 3 - st.amulets, Math.floor((st.gold - reserve()) / +m[1])));
      st.amulets += v;
      host.answerString(String(v)); return true;
    }
    if (/A book costs/.test(prompt)) { host.answerString("0"); return true; }
    if ((m = /A gem is worth (\d+) gp.*\((\d+) max\)/.exec(prompt))) {
      var nb = nextBag(), need = nb ? Math.ceil((nb[1] + reserve() - st.gold) / +m[1]) : 0;
      v = Math.max(0, Math.min(+m[2], Math.floor(st.level * st.level + 1), need));
      host.answerString(String(v)); return true;
    }
    if (/How much gold will you give/.test(prompt)) {
      v = Math.max(0, Math.floor(load() - 0.4 * capacity()));
      v = Math.min(v, Math.floor(st.gold - reserve()));
      host.answerString(String(Math.max(0, v))); return true;
    }
    if (/How much mana for bolt/.test(prompt)) {
      host.answerString(String(st.boltMana || Math.max(1, Math.floor(st.mana * 0.2)))); return true;
    }
    return false;
  }

  /* ------------------------------------------------------- control */

  function draw() {
    var b = el("autoplay");
    if (!b) return;
    b.hidden = !st.allowed;
    el("autoplay-speed").hidden = !st.allowed || !st.on;
    el("autoplay-status").hidden = !st.allowed;
    b.classList.toggle("on", st.on);
    b.textContent = st.on ? "Stop autoplay" : "Autoplay";
    el("autoplay-speed").value = st.speed;
  }
  function start() {
    if (!st.allowed) return;
    st.on = true;
    st.target = Math.min(circle(), cap());
    st.easy = 0; st.plan = []; st.unknown = 0; st.trip = null;
    if (st.startLevel === null) st.startLevel = st.level;
    draw();
    say("Autoplay is on.", "on");
    if (!st.quest) host.chat("/quest");
    if (host.pending() === "buttons") schedule();
  }
  function stop(why, kind) {
    st.on = false; st.plan = []; st.trip = null; st.visit = null;
    cancelPress();
    draw();
    if (why) say(why, kind || "");
  }
  function cancelPress() { if (st.timer) { clearTimeout(st.timer); st.timer = null; } }
  function schedule() {
    cancelPress();
    st.timer = setTimeout(function () { st.timer = null; act(); }, SPEEDS[st.speed]);
  }

  return {
    allow: function (yes) { st.allowed = yes; if (!yes) stop(); draw(); if (yes) say("Autoplay is ready."); },
    at: function (x, y) { st.x = x; st.y = y; },
    level: function (lv) { st.level = lv; },
    bar: function (which, v, max) {
      if (which === "energy") { st.e = v; st.emax = max || 1; }
      if (which === "speed") { st.spd = v; st.spdmax = max || v; }
    },
    stat: function (which, v) {
      if (which === "mana") st.mana = v;
      else if (which === "gold") st.gold = v;
      else if (which === "gems") st.gems = v;
      else if (which === "blessing") st.blessed = !!v;
    },
    world: function (callRegion) {
      var name = (callRegion || "").toLowerCase();
      st.callRegion = -1;
      REGIONS.forEach(function (r, i) { if (name && name.indexOf(r) >= 0) st.callRegion = i; });
    },
    line: function (text) {
      if (text === "" || text == null) return;
      st.lines.push(text);
      if (st.lines.length > 60) st.lines.shift();
      var hm = /hit you for (\d+)/.exec(text);
      if (hm) { st.hits.push(+hm[1]); if (st.hits.length > 12) st.hits.shift(); }
      if (/(attacked by|attacks|find and attack)/.test(text) && /Size:/.test(text)) { st.evaded = false; st.lucked = false; }
      if (/You killed it|You made it!/.test(text)) {
        st.kills++; st.unknown = 0; st.fightKilled = true;
        /* each kill counts (a flock can go on for a long time) */
        if (st.inFight) { fightOver(); st.inFight = true; st.fightStartE = st.e; st.fightDanger = false; st.fightKilled = false; }
      }
      if (/That's too far to move in 1 step/.test(text)) st.moveScale = Math.max(0.3, st.moveScale * 0.8);
      if (/You do not have enough mana/.test(text) && st.lastSpell) st.noMana[st.lastSpell] = st.mana * 1.3 + 10;
      if (/protects you|amulet/i.test(text) && /cursed|protect/i.test(text)) st.amulets = Math.max(0, st.amulets - 1);
      if (/in ruins|no trade/i.test(text) && st.atPost) {
        st.badPosts[st.atPost.x + "," + st.atPost.y] = Date.now() + 30 * 60 * 1000;
        st.visit = null;
      }
      if (st.on && DEATH.test(text)) stop("Autoplay stopped: your character died.", "warn");
    },
    chat: function (text) {
      var m = /Today's quest: (.*)/.exec(text || "");
      if (!m) return;
      var r = /Journey to circle (\d+)/.exec(m[1]);
      st.quest = { text: m[1], reach: r ? +r[1] : 0, done: /complete!/.test(m[1]) };
    },
    odds: function (F) { st.odds = F; F.spells.forEach(function (sp) { st.spells[sp.name] = true; }); F.spellsFromServer = true; },
    buttons: function (labels, compass) {
      lastLabels = labels; lastCompass = compass;
      if (st.on) schedule();
    },
    dialog: function (kind, prompt) {
      if (!st.on) return false;
      if (answerDialog(kind, prompt)) return true;
      stop("Autoplay paused: a question only you can answer.", "warn");
      return false;
    },
    userTook: function () { if (st.on) stop("Autoplay stopped: you took over."); },
    cancelPress: cancelPress,
    toggle: function () { if (st.on) stop("Autoplay is off."); else start(); },
    setSpeed: function (s) {
      if (!SPEEDS[s]) return;
      st.speed = s;
      try { localStorage.setItem("phantasia-autoplay-speed", s); } catch (e) {}
    },
    get pressing() { return host.isPressing(); }
  };
};
