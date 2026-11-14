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
    sinceStep: 0, spin: 1, extra: {}, str: 0, strmax: 0, status: null,
    quest: null, callRegion: -1, fallen: {}, worldAt: 0, blessed: false, amulets: 0, unknown: 0
  };
  try { st.speed = localStorage.getItem("phantasia-autoplay-speed") || "normal"; } catch (e) {}
  if (!SPEEDS[st.speed]) st.speed = "normal";

  /* ------------------------------------------------------------ the record */

  /* Everything that happens while autoplay runs, for studying how it plays:
     each choice with the numbers behind it, and every line the game shows.
     Kept in this browser (the newest 20000 entries), saved with "Save log",
     and sent to the website when the character dies (which commits it to
     GitHub when the server is set up for it). */
  var REC_KEY = "phantasia-autoplay-record", REC_MAX = 20000;
  var rec = [];
  try { rec = JSON.parse(localStorage.getItem(REC_KEY) || "[]") || []; } catch (e) { rec = []; }
  var recDirty = false;
  function record(entry) {
    entry.t = Date.now();
    rec.push(entry);
    if (rec.length > REC_MAX) rec.splice(0, rec.length - REC_MAX);
    recDirty = true;
  }
  setInterval(function () {
    if (!recDirty) return;
    recDirty = false;
    try { localStorage.setItem(REC_KEY, JSON.stringify(rec)); }
    catch (e) { rec.splice(0, Math.floor(rec.length / 3)); }
  }, 5000);
  function snapshot() {
    return { lv: st.level, e: Math.round(st.e), emax: st.emax, x: st.x, y: st.y, c: circle(),
      gold: Math.floor(st.gold), gems: st.gems, mana: Math.floor(st.mana), target: st.target, cap: cap() };
  }
  /* the whole character: stats, status, gear, bag, spells, and (when
     asked) the per-circle costs it is choosing its hunting ground from */
  function charSnapshot(why, withCircles) {
    var g = gear(), entry = snapshot();
    entry.k = "char"; entry.why = why;
    entry.str = st.str; entry.strmax = st.strmax; entry.spd = st.spd; entry.spdmax = st.spdmax;
    entry.shield = st.extra.shield; entry.sword = st.extra.sword; entry.quicksilver = st.extra.quicksilver;
    entry.status = st.status || null;
    entry.items = {};
    Object.keys(st.extra).forEach(function (k) { if (k.indexOf("item:") === 0) entry.items[k.slice(5)] = st.extra[k]; });
    entry.bag = g.bag; entry.bagSize = g.capacity;
    entry.gear = g.items.map(function (it) {
      return { slot: it.slot, name: it.name, stats: it.stats, tier: it.tier, worn: it.equipped,
        ab: [it.ab1, it.ab2].filter(Boolean) };
    });
    entry.spells = Object.keys(st.spells);
    entry.quest = st.quest ? st.quest.text : "";
    if (withCircles && st.circles) entry.circles = st.circles;
    record(entry);
  }

  function summary() {
    return { character: host.name(), level: st.level, startLevel: st.startLevel, fights: st.fights,
      kills: st.kills, minutes: st.startedAt ? Math.round((Date.now() - st.startedAt) / 60000) : 0 };
  }
  function sessionLog() {
    for (var i = rec.length - 1; i >= 0; i--) if (rec[i].k === "start") return rec.slice(i);
    return rec.slice();
  }
  function sendRecord(reason) {
    if (!st.key || !window.fetch) return;
    record({ k: "send", reason: reason });
    fetch("/api/autoplay/report", { method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ key: st.key, character: host.name(), reason: reason, summary: summary(), log: sessionLog() }) })
      .then(function (r) { return r.json(); })
      .then(function (j) { say("Record " + (j && j.result ? j.result : "sent") + ".", "warn"); })
      .catch(function () {});
  }
  function saveRecord() {
    var blob = new Blob([sessionLog().map(function (e) { return JSON.stringify(e); }).join("\n") + "\n"], { type: "application/json" });
    var a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "autoplay-" + (host.name() || "record") + "-" + new Date().toISOString().slice(0, 16).replace(/[:T]/g, "-") + ".jsonl";
    document.body.appendChild(a); a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
  }

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
  /* How hard an average monster hits at each circle: the server's own
     table (Do_gear_threat).  A character belongs as far out as monsters
     hit for no more than a sixth of its energy; fights decide within that. */
  var THREAT = [[1, 7], [5, 31], [10, 70], [15, 200], [20, 400], [25, 1000], [30, 1450],
    [40, 2250], [50, 2900], [75, 4400], [100, 5800]];
  function threat(c) {
    for (var i = 1; i < THREAT.length; i++) {
      if (c <= THREAT[i][0]) {
        var a = THREAT[i - 1], b = THREAT[i];
        return a[1] + (b[1] - a[1]) * (c - a[0]) / (b[0] - a[0]);
      }
    }
    return THREAT[THREAT.length - 1][1];
  }
  /* The server works out, from the real encounter rules and this
     character's might, quickness and energy, what an average fight costs
     at each circle and how many monsters there could nearly kill it.  A
     character belongs as far out as every circle on the way costs no more
     than a fifth of full energy a fight, with under one monster in ten
     that dangerous. */
  function cap() {
    if (st.circles && st.circles.length) {
      var best = 1;
      for (var i = 0; i < st.circles.length; i++) {
        var k = st.circles[i];
        if (k.loss > 0.2 * st.emax || k.bad > 0.1) break;
        best = k.circle;
      }
      return best;
    }
    var c = 1;
    while (c < 100 && threat(c + 1) <= st.emax / 6) c++;
    return c;
  }
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
  /* A post's stock grows with its rank: rank 1 sells mana and armor,
     rank 2 adds books, rank 3 amulets, and so on (Do_trading_post). */
  function nearestPost(minRank) {
    var best = null, now = Date.now();
    for (var n = minRank || 1; n <= 40; n++) {
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
    var entry = snapshot();
    entry.k = "do"; entry.menu = lastLabels.filter(Boolean).join("|"); entry.pick = lastLabels[i];
    entry.why = (el("autoplay-status") || {}).textContent || "";
    if (st.lastOdds) { entry.odds = st.lastOdds; st.lastOdds = null; }
    record(entry);
    if (trace.length > 60) trace.shift();
    host.press(i);
  }
  function pressCompass(k) {
    if (!lastCompass) return;
    var entry = snapshot();
    entry.k = "move"; entry.dir = ["NW", "N", "NE", "W", "Rest", "E", "SW", "S", "SE"][k];
    entry.why = (el("autoplay-status") || {}).textContent || "";
    record(entry);
    host.compass(k);
  }

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

  function backpackFull() {
    return gear().items.filter(function (g) { return !g.equipped; }).length >= gear().capacity;
  }

  function wantsToShop() {
    /* a full bag of spares: go and sell them */
    if (backpackFull() && spareGear().length) return true;
    if (st.fights - st.lastShopFight < 15 && load() < 0.8 * capacity()) return false;
    var spend = st.gold + Math.min(st.gems, st.level * st.level + 1) * 1000 - reserve();
    var nb = nextBag();
    if (load() > 0.75 * capacity()) return true;
    if (nb && spend >= nb[1]) return true;
    if (spareGear().length >= 2) return true;
    return spend >= 300 + st.level * 40;
  }

  function startTrip() {
    /* books need a rank 2 post (about circle 5): go to one when we can
       safely hunt that far out */
    var bookRank = booksWanted(100) > 0 && cap() >= circleAt(400, 400) ? 2 : 1;
    var p = nearestPost(bookRank);
    if (!p) return false;
    /* only when the walk is reasonable */
    var steps = p.d / maxMove();
    if (steps > 45 && load() < 0.9 * capacity() && !(backpackFull() && steps <= 160)) return false;
    st.trip = p;
    return true;
  }

  /* the tasks for a visit, in order */
  function planVisit() {
    var tasks = [], nb = nextBag();
    if (st.gems > 0 && nb && st.gold < nb[1] + reserve()) tasks.push("gems");
    tasks.push("books");
    if (nb) tasks.push("bag");
    tasks.push("armory");
    if (st.level >= 3 && st.amulets < 3) tasks.push("amulets");
    if (Object.keys(st.spells).length) tasks.push("mana");
    if (!st.blessed) tasks.push("blessing");
    tasks.push("donate");
    st.visit = { tasks: tasks, items: [], spent: 0 };
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
        case "books":
          if (booksWanted(100) > 0) { say("Buying books for brains (luckout and evade).", "on"); st.plan = ["Purchase", orCancel("Book")]; return true; }
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

  /* Books (Do_book): each adds half the class's brains increase until
     brains reach level x the class's cap, then far less, so buy up to the
     cap.  Below level 20 a visit's purchases over level x 350 + 150 gold
     risk being taken for a thief (with chance 1 - that / spent): stay at
     or under it and the risk is nil. */
  function booksWanted(price) {
    var b = st.brainsInfo;
    if (!b || b.perBook <= 0 || b.brains >= b.cap) return 0;
    var need = Math.ceil((b.cap - b.brains) / b.perBook);
    var afford = Math.floor((st.gold - reserve()) / price);
    var safe = st.level < 20 ? Math.floor((st.level * 350 + 150 - ((st.visit && st.visit.spent) || 0)) / price) : Infinity;
    return Math.max(0, Math.min(need, afford, safe));
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
      if (++st.easy >= 3) { st.target = Math.min(cap(), st.target + 1); st.easy = 0; }
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
      /* Move To walks the whole way, a step a turn (stopping to rest
         below half energy, when we just ask again) */
      st.moveTo = { x: st.trip.x, y: st.trip.y };
      return press(has("Move To"));
    }

    /* the daily quest: a journey to a circle, if it's safe */
    var want = st.target;
    if (st.quest && st.quest.reach && !st.quest.done && st.quest.reach <= cap()) want = Math.max(want, st.quest.reach);

    if (lastCompass && c < want) { say("Heading out to circle " + want + " (now " + c + ").", "on"); return pressCompass(outward()); }
    if (lastCompass && c > want) { say("Pulling back toward circle " + want + " (now " + c + ").", "on"); return pressCompass(stepToward(-st.x, -st.y)); }
    /* wander along the circle now and then, as players do: new ground
       turns up treasure, villages and gurus as well as monsters */
    if (lastCompass && ++st.sinceStep >= 3) {
      st.sinceStep = 0;
      if (Math.random() < 0.15) st.spin = -st.spin;
      var r = Math.hypot(st.x, st.y) || 1;
      say("Exploring circle " + c + ".", "on");
      return pressCompass(stepToward(-st.y / r * st.spin, st.x / r * st.spin));
    }
    if (has("Hunt") >= 0) {
      var here = st.circles && st.circles[c - 1];
      say("Hunting in circle " + c + (here ? " (a fight costs about " + here.loss + " energy, for " + here.exp + " experience)" : "") +
        ". Fights " + st.fights + ", levels gained " +
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

    /* a flock that won't end: each kill brings another with this chance,
       so about 1/(1 - flock) of them in a row (a Moron's 100% never ends).
       Get away once a long chain isn't worth its time */
    var chain = F.flock >= 100 ? Infinity : 1 / Math.max(0.01, 1 - F.flock / 100);
    if (has("Evade") >= 0 && F.evade > 0 && (chain > 15 || (st.flockKills >= 6 && F.flock >= 50))) {
      say("A flock that won't end (" + F.name + ", " + F.flock + "% flock): getting away, " +
        Math.round(F.evade * 100) + "%.", "on");
      return press(has("Evade"));
    }

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
      /* Out of trouble by the best chance of living through it, worked out
         action by action: before each action of ours the monster lands k
         hits in a row with chance q^k, so with energy e and hits averaging
         h we fall before acting with chance q^ceil(e/h); each action costs
         about D energy on average. */
      var h = Math.max(1, (F.str + 1) / 2);
      var pFall = function (en) { return en <= 0 ? 1 : Math.pow(q, Math.ceil(en / h)); };
      var liveThrough = function (en, actions) {
        var alive = 1;
        for (var i = 0; i < Math.min(actions, 80); i++) { alive *= 1 - pFall(en); en -= D; }
        return alive;
      };
      var choices = [{ label: "Melee", live: liveThrough(st.e, n), why: "Fighting on" }];
      var ev = has("Evade") >= 0 ? F.evade : 0;
      if (ev > 0) {
        var got = 0, still = 1, en2 = st.e;
        for (var t = 0; t < 12; t++) { var f = pFall(en2); got += still * (1 - f) * ev; still *= (1 - f) * (1 - ev); en2 -= D; }
        choices.push({ label: "Evade", live: got, why: "Getting away (" + Math.round(ev * 100) + "% a try)" });
      }
      if (F.luck > 0 && has("Luckout") >= 0) {
        var first = 1 - pFall(st.e);
        choices.push({ label: "Luckout", live: first * (F.luck + (1 - F.luck) * liveThrough(st.e - D, n)),
          why: "Luckout (" + Math.round(F.luck * 100) + "%, brains " + F.brains + " to beat)" });
      }
      var heal = opts.filter(function (o) { return o.label === "Mending Light"; })[0];
      if (heal) {
        choices.push({ label: heal.label, spell: true, live: (1 - pFall(st.e)) * liveThrough(st.e + heal.heal - D, n),
          why: "Healing (+" + Math.round(heal.heal) + ")" });
      }
      opts.filter(function (o) { return o.spell && o.label !== "Mending Light"; }).forEach(function (o) {
        choices.push({ label: o.label, spell: true, live: liveThrough(st.e, o.acts), why: o.why });
      });
      choices.sort(function (a, b) { return b.live - a.live; });
      var best = choices[0];
      pick = { label: best.label, spell: best.spell, why: best.why + ": " + Math.round(best.live * 100) + "% to live" };
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
      if (st.visit) st.visit.spent += Math.floor(v) * +m[1];
      host.answerString(String(Math.floor(v))); return true;
    }
    if ((m = /An amulet costs (\d+) gp.*\((\d+) max\)/.exec(prompt))) {
      v = Math.max(0, Math.min(+m[2], 3 - st.amulets, Math.floor((st.gold - reserve()) / +m[1])));
      st.amulets += v;
      if (st.visit) st.visit.spent += v * +m[1];
      host.answerString(String(v)); return true;
    }
    if ((m = /A book costs (\d+) gp.*\((\d+) max\)/.exec(prompt))) {
      v = Math.min(+m[2], booksWanted(+m[1]));
      if (st.visit) st.visit.spent += v * +m[1];
      host.answerString(String(v)); return true;
    }
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
    if (el("autoplay-save")) el("autoplay-save").hidden = !st.allowed;
  }
  function start() {
    if (!st.allowed) return;
    st.on = true;
    st.target = Math.min(circle(), cap());
    st.easy = 0; st.plan = []; st.unknown = 0; st.trip = null;
    if (st.startLevel === null) st.startLevel = st.level;
    if (!st.startedAt) st.startedAt = Date.now();
    var s0 = snapshot(); s0.k = "start"; s0.character = host.name(); s0.speed = st.speed; record(s0);
    charSnapshot("start", true);
    draw();
    say("Autoplay is on.", "on");
    if (!st.quest) host.chat("/quest");
    if (host.pending() === "buttons") schedule();
  }
  function stop(why, kind) {
    if (st.on) record({ k: "stop", why: why || "" });
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
    allow: function (yes, key) { st.allowed = yes; st.key = key || ""; if (!yes) stop(); draw(); if (yes) say("Autoplay is ready."); },
    at: function (x, y) { st.x = x; st.y = y; },
    level: function (lv) {
      var was = st.level;
      st.level = lv;
      if (st.on && Math.floor(lv) !== Math.floor(was)) {
        record({ k: "levelup", from: was, to: lv });
        setTimeout(function () { charSnapshot("level", true); }, 1500);
      }
    },
    status: function (o) {
      st.status = o;
      st.brainsInfo = { brains: o.brains, cap: o.brainsCap, perBook: o.perBook };
      st.amulets = o.amulets;
    },
    gearChanged: function () {
      if (!st.on) return;
      clearTimeout(st.gearTimer);
      st.gearTimer = setTimeout(function () { charSnapshot("gear", false); }, 1200);
    },
    bar: function (which, v, max) {
      if (which === "energy") { st.e = v; st.emax = max || 1; }
      if (which === "speed") { st.spd = v; st.spdmax = max || v; }
      if (which === "strength") { st.str = v; st.strmax = max || v; }
    },
    stat: function (which, v) {
      if (which === "mana") st.mana = v;
      else if (which === "gold") st.gold = v;
      else if (which === "gems") st.gems = v;
      else if (which === "blessing") st.blessed = !!v;
      else st.extra[which] = v;
    },
    world: function (callRegion) {
      var name = (callRegion || "").toLowerCase();
      st.callRegion = -1;
      REGIONS.forEach(function (r, i) { if (name && name.indexOf(r) >= 0) st.callRegion = i; });
    },
    line: function (text) {
      if (text === "" || text == null) return;
      if (st.on) record({ k: "line", s: text });
      st.lines.push(text);
      if (st.lines.length > 60) st.lines.shift();
      var hm = /hit you for (\d+)/.exec(text);
      if (hm) { st.hits.push(+hm[1]); if (st.hits.length > 12) st.hits.shift(); }
      if (/(attacked by|attacks|find and attack)/.test(text) && /Size:/.test(text)) { st.evaded = false; st.lucked = false; }
      if (/friend appears/.test(text)) st.flockKills = (st.flockKills || 0) + 1;
      else if (/(attacked by|find and attack)/.test(text)) st.flockKills = 0;
      if (/You killed it|You made it!/.test(text)) {
        st.kills++; st.unknown = 0; st.fightKilled = true;
        /* each kill counts (a flock can go on for a long time) */
        if (st.inFight) { fightOver(); st.inFight = true; st.fightStartE = st.e; st.fightDanger = false; st.fightKilled = false; }
      }
      if (/That's too far to move in 1 step/.test(text)) st.moveScale = Math.max(0.3, st.moveScale * 0.8);
      if (/You do not have enough mana/.test(text) && st.lastSpell) st.noMana[st.lastSpell] = st.mana * 1.3 + 10;
      if (/in ruins|no trade/i.test(text) && st.atPost) {
        st.badPosts[st.atPost.x + "," + st.atPost.y] = Date.now() + 30 * 60 * 1000;
        st.visit = null;
      }
      if (st.on && DEATH.test(text)) {
        var d = snapshot(); d.k = "death"; d.s = text; record(d);
        charSnapshot("death", true);
        stop("Autoplay stopped: your character died.", "warn");
        sendRecord("death");
      }
    },
    chat: function (text) {
      if (st.on && text) record({ k: "chat", s: text });
      var m = /Today's quest: (.*)/.exec(text || "");
      if (!m) return;
      var r = /Journey to circle (\d+)/.exec(m[1]);
      st.quest = { text: m[1], reach: r ? +r[1] : 0, done: /complete!/.test(m[1]) };
    },
    brains: function (b) { st.brainsInfo = b; },
    circles: function (list) { st.circles = window.PhantasiaAutoplayCircles = list; if (st.target > cap()) st.target = cap(); },
    odds: function (F) {
      st.lastOdds = { m: F.name, me: F.energy, ms: F.str, q: F.q, melee: F.melee, luck: F.luck, ev: F.evade,
        br: F.brains, fl: F.flock, xp: F.exp, sp: F.special };
      st.odds = F; F.spells.forEach(function (sp) { st.spells[sp.name] = true; }); F.spellsFromServer = true; },
    buttons: function (labels, compass) {
      lastLabels = labels; lastCompass = compass;
      if (st.on) schedule();
    },
    dialog: function (kind, prompt) {
      if (!st.on) return false;
      record({ k: "ask", kind: kind, s: prompt });
      if (answerDialog(kind, prompt)) return true;
      stop("Autoplay paused: a question only you can answer.", "warn");
      return false;
    },
    userTook: function () { if (st.on) stop("Autoplay stopped: you took over."); },
    cancelPress: cancelPress,
    save: saveRecord,
    toggle: function () { if (st.on) stop("Autoplay is off."); else start(); },
    setSpeed: function (s) {
      if (!SPEEDS[s]) return;
      st.speed = s;
      try { localStorage.setItem("phantasia-autoplay-speed", s); } catch (e) {}
    },
    get pressing() { return host.isPressing(); }
  };
};
