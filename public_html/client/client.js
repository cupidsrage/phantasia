/*
 * Phantasia Reimagined browser client.
 *
 * Talks to the game through the website's WebSocket bridge (/ws), which
 * forwards the original Phantasia client/server protocol unchanged: the
 * server sends newline-terminated fields, the client answers with
 * "<type>\0<value>\0" packets.
 */
(function () {
  "use strict";

  /* ---- server -> client packet types ---- */
  var P = {
    HANDSHAKE: 2, CLOSE: 3, PING: 4, ADD_PLAYER: 5, REMOVE_PLAYER: 6,
    SHUTDOWN: 7, ERROR: 8, CLEAR: 10, WRITE_LINE: 11,
    BUTTONS: 20, FULL_BUTTONS: 21, STRING_DIALOG: 22, COORD_DIALOG: 23,
    PLAYER_DIALOG: 24, PASSWORD_DIALOG: 25, SCOREBOARD_DIALOG: 26,
    DIALOG: 27, CHAT: 30, ACTIVATE_CHAT: 31, DEACTIVATE_CHAT: 32,
    PLAYER_INFO: 33, CONNECTION_DETAIL: 34,
    NAME: 40, LOCATION: 41, ENERGY: 42, STRENGTH: 43, SPEED: 44,
    SHIELD: 45, SWORD: 46, QUICKSILVER: 47, MANA: 48, LEVEL: 49,
    GOLD: 50, GEMS: 51, CLOAK: 52, BLESSING: 53, CROWN: 54,
    PALANTIR: 55, RING: 56, VIRGIN: 57, GEAR: 60, WORLD_EVENT: 61, WIZARD: 62, SELECT: 63, WORLD: 64
  };

  /* ---- client -> server ---- */
  var C = { RESPONSE: "1\0", CANCEL: "2\0", PING: "3\0", CHAT: "4\0",
            EXAMINE: "5\0", SCOREBOARD: "7\0", GEAR: "8\0" };

  var COMPASS = ["NW", "N", "NE", "W", "Rest", "E", "SW", "S", "SE"];
  var MAX_LOG = 600, MAX_CHAT = 300;

  var $ = function (id) { return document.getElementById(id); };

  /* ------------------------------------------------------------ state */

  var ws = null;
  var lines = [];            // received, not yet consumed fields
  var partial = "";          // incomplete trailing line
  var waiter = null;         // resolver waiting for the next field
  var decoder = new TextDecoder("windows-1252");
  var pending = null;        // what the server is waiting for
  var players = [];          // [{name, desc}]
  var name = "";
  var maxima = { energy: 1, strength: 1, speed: 1 };
  var extraMax = 0;
  var closedCleanly = false;

  /* ------------------------------------------------------- networking */

  function connect() {
    var proto = location.protocol === "https:" ? "wss://" : "ws://";
    setConn("connecting", "Connecting");
    lines = []; partial = ""; closedCleanly = false;
    ws = new WebSocket(proto + location.host + "/ws");
    ws.binaryType = "arraybuffer";

    ws.onopen = function () { setConn("online", "Connected"); };

    ws.onmessage = function (ev) {
      var text = typeof ev.data === "string" ? ev.data
        : decoder.decode(new Uint8Array(ev.data), { stream: true });
      var parts = (partial + text).split("\n");
      partial = parts.pop();
      for (var i = 0; i < parts.length; i++) lines.push(parts[i]);
      wake();
    };

    ws.onclose = function () {
      setConn("offline", "Disconnected");
      disableAll();
      wake();
      if (!closedCleanly) {
        showModal("Disconnected",
          "<p>The connection to the realm was lost.</p>",
          [{ label: "Reconnect", primary: true, action: function () { location.reload(); } }]);
      }
    };

    run().catch(function (e) {
      if (e !== CLOSED) console.error(e);
    });
  }

  var CLOSED = {};

  function wake() {
    if (waiter) { var w = waiter; waiter = null; w(); }
  }

  /* next field from the server (async) */
  function next() {
    return new Promise(function (resolve, reject) {
      (function attempt() {
        if (lines.length) return resolve(lines.shift());
        if (!ws || ws.readyState > 1) return reject(CLOSED);
        waiter = attempt;
      })();
    });
  }

  async function nextInt() { return parseInt(await next(), 10) || 0; }
  async function nextBool() { return (await next()) === "Yes"; }

  function send(text) {
    if (ws && ws.readyState === 1) ws.send(text);
  }

  function respond(value) { send(C.RESPONSE + value + "\0"); }

  /* ------------------------------------------------------- main loop */

  async function run() {
    for (;;) {
      var type = await nextInt();

      switch (type) {
        case P.HANDSHAKE:          // normally handled by the bridge
          respond("1004"); respond("0"); respond("0");
          respond(String(Math.floor(Date.now() / 1000)));
          break;
        case P.CLOSE:
          closedCleanly = true;
          addLog("— You have left the realm. —", "sep-line");
          setConn("offline", "Logged off");
          showModal("Farewell",
            "<p>You have left the realm.</p>",
            [{ label: "Play again", primary: true, action: function () { location.reload(); } },
             { label: "Website", action: function () { location.href = "../index.html"; } }]);
          ws.close();
          return;
        case P.PING: handlePing(); break;
        case P.ADD_PLAYER: {
          var pname = await next(), desc = await next();
          players.push({ name: pname, desc: desc });
          renderPlayers();
          break;
        }
        case P.REMOVE_PLAYER: {
          /* the server updates a player by adding the new entry and
             then removing the old one, so drop only the first match */
          var gone = await next();
          for (var pi = 0; pi < players.length; pi++) {
            if (players[pi].name === gone) { players.splice(pi, 1); break; }
          }
          renderPlayers();
          break;
        }
        case P.SHUTDOWN:
          showInfo("Server maintenance",
            "The server is being brought down for maintenance. Your game is being saved and you will be logged off. Please try again in a few minutes.");
          break;
        case P.ERROR: {
          var msg = await next();
          closedCleanly = true;
          showModal("The realm closed the connection", "<p>" + esc(msg) + "</p>",
            [{ label: "Try again", primary: true, action: function () { location.reload(); } }]);
          ws.close();
          return;
        }
        case P.CLEAR: clearLog(); break;
        case P.WRITE_LINE: addLog(await next()); break;
        case P.BUTTONS: await readButtons(false); break;
        case P.FULL_BUTTONS: await readButtons(true); break;
        case P.STRING_DIALOG: askString(await next(), false); break;
        case P.PASSWORD_DIALOG: askString(await next(), true); break;
        case P.COORD_DIALOG: askCoords(await next()); break;
        case P.PLAYER_DIALOG: askPlayer(await next()); break;
        case P.SCOREBOARD_DIALOG: await showScoreboard(); break;
        case P.DIALOG: showInfo("Phantasia Reimagined", await next()); break;
        case P.CHAT: addChat(await next()); break;
        case P.ACTIVATE_CHAT: setChat(true); break;
        case P.DEACTIVATE_CHAT: setChat(false); break;
        case P.PLAYER_INFO: await showExamine(); break;
        case P.CONNECTION_DETAIL: await showDetail(); break;
        case P.GEAR: await readGear(); break;
        case P.WORLD_EVENT: await readWorldEvent(); break;
        case P.WIZARD: { var kind = await nextInt(); ceremony(kind, await next()); break; }
        case P.SELECT: await askSelect(); break;
        case P.WORLD: await readWorld(); break;
        default:
          if (type >= P.NAME && type <= P.VIRGIN) await updateStatus(type);
          else console.warn("Unknown packet", type);
      }
    }
  }

  /* ------------------------------------------------------- status */

  async function updateStatus(type) {
    switch (type) {
      case P.NAME:
        name = await next();
        setLayoutOwner(name);
        if (!name) { $("title-head").textContent = "Welcome to Phantasia Reimagined!"; $("title-tail").textContent = ""; }
        document.title = name ? name + " — Phantasia Reimagined" : "Phantasia Reimagined";
        break;
      case P.LOCATION: {
        var x = await next(), y = await next(), place = await next();
        $("title-tail").textContent = "( " + x + " , " + y + " )";
        $("title-head").textContent = name ? name + " is in " + place : "Character starts at: " + place;
        break;
      }
      case P.ENERGY: {
        var e = await next(), emax = await nextInt(), extra = await nextInt();
        setBar("energy", e, emax);
        var ex = $("bar-energy").querySelector(".fill.extra");
        if (!extra) { extraMax = 0; ex.style.width = "0%"; }
        else { if (extra > extraMax) extraMax = extra; ex.style.width = (100 * extra / extraMax) + "%"; }
        break;
      }
      case P.STRENGTH: { var s = await next(); setBar("strength", s, await nextInt()); break; }
      case P.SPEED: { var q = await next(); setBar("speed", q, await nextInt()); break; }
      case P.SHIELD: $("val-shield").textContent = await next(); break;
      case P.SWORD: $("val-sword").textContent = await next(); break;
      case P.QUICKSILVER: $("val-quicksilver").textContent = await next(); break;
      case P.MANA: $("val-mana").textContent = await next(); break;
      case P.LEVEL: $("val-level").textContent = await next(); break;
      case P.GOLD: $("val-gold").textContent = await next(); break;
      case P.GEMS: $("val-gems").textContent = await next(); break;
      case P.CLOAK: setItem("cloak", await nextBool()); break;
      case P.BLESSING: setItem("blessing", await nextBool()); break;
      case P.CROWN: setItem("crown", await nextBool()); break;
      case P.PALANTIR: setItem("palantir", await nextBool()); break;
      case P.RING: setItem("ring", await nextBool()); break;
      case P.VIRGIN: setItem("virgin", await nextBool()); break;
    }
  }

  function setBar(which, value, max) {
    $("val-" + which).textContent = value + (max ? " / " + max : "");
    var v = parseFloat(value) || 0;
    var pct = max > 0 ? Math.max(0, Math.min(100, 100 * v / max)) : 0;
    $("bar-" + which).querySelector(".fill").style.width = pct + "%";
  }

  function setItem(which, on) {
    $("item-" + which).classList.toggle("on", on);
    $("item-" + which).title = $("item-" + which).title.split(":")[0] + (on ? ": yes" : ": no");
  }

  /* ------------------------------------------------------- story log */

  /* colour each line of the game log by what it is about, so a glance
     tells you what is happening */
  var LOG_KINDS = [
    ["announce", /^\*\*\*/],
    ["hurt", /hit you for|you were killed|killed you|you are poisoned|you've been hit|plague|you lose |drains you|curses you|smites you|your .* (breaks|is stolen)|you die|you have died/i],
    ["foe", /attacks|you are attacked|find and attack|challenge|bars your way|stalks |appears and|now attacks|friend appears|grows bold|you encounter|sighted/i],
    ["deal", /^you hit |magic bolt|fireball|frost lance|tendrils|lance of ice|you killed it|squish|is banished|transforms|crushing blow/i],
    ["heal", /restor|heals? |healed|cure|you drain|warm light|mending|feast|dresses your wounds/i],
    ["reward", /gold|gems?\b|you found|experience|reward|deed earned|you are paid|bounty|treasure|you receive|level up|you are now level/i],
    ["world", /you enter |shadow|liberat|trading post|the town|realm project|siege|throne|season|night falls/i]
  ];
  function logKind(text) {
    for (var i = 0; i < LOG_KINDS.length; i++) if (LOG_KINDS[i][1].test(text)) return LOG_KINDS[i][0];
    return "";
  }

  /* a line naming a piece of gear ("Fine Dagger of Thorns (Weapon) ...")
     shows the item in its tier's colour, like the gear panel does */
  var ITEM_LINE = /^(.*?)([A-Z][^():]*?) \((Weapon|Armor|Helm|Boots|Trinket|Regalia)\)(.*)$/;
  var ARTIFACT_NAMES = /^(Mourneblade|The Phoenix Amulet|The Crown of the Deep|Gwaihir's Feather|The Dragonscale Aegis|The Ring of Hoarding|Wyrmslayer|The Mantle of Shadows|The Helm of Ages|The Wind-lord's Boots|The Archmage's Circlet|The Steward's Seal|The King's Crown|The Queen's Crown|The Halo of the Valar)$/;
  var recentItems = [];        /* items just listed, to colour the buttons that follow */
  function itemTier(itemName) {
    if (ARTIFACT_NAMES.test(itemName)) return 5;
    var m = /^(Worn|Fine|Masterwork|Enchanted) /.exec(itemName);
    return m ? ["Worn", "", "Fine", "Masterwork", "Enchanted"].indexOf(m[1]) : 1;
  }

  function addLog(text, cls) {
    var log = $("log");
    var stick = log.scrollTop + log.clientHeight >= log.scrollHeight - 30;
    var p = document.createElement("p");
    var im = text ? ITEM_LINE.exec(text) : null;
    if (text === "") p.className = "sep";
    else if (im) {
      /* "<lead>Item Name (Slot) <stats>": the name in its tier colour */
      var tier = itemTier(im[2].trim());
      p.className = "k-item";
      if (im[1]) p.appendChild(document.createTextNode(im[1]));
      var nm = document.createElement("span");
      nm.className = "gitem t" + tier;
      nm.textContent = im[2];
      p.appendChild(nm);
      p.appendChild(document.createTextNode(" (" + im[3] + ")" + im[4]));
      recentItems.push({ name: im[2].trim().toLowerCase(), tier: tier });
      if (recentItems.length > 24) recentItems.shift();
    }
    else {
      p.textContent = text;
      var kind = logKind(text);
      if (kind) p.classList.add("k-" + kind);
    }
    if (cls) p.classList.add(cls);
    log.appendChild(p);
    while (log.childNodes.length > MAX_LOG) log.removeChild(log.firstChild);
    if (stick) log.scrollTop = log.scrollHeight;
  }

  function clearLog() {
    // keep recent history visible but faded, like turning a page
    var log = $("log");
    Array.prototype.forEach.call(log.childNodes, function (p) { p.classList.add("old"); });
    if (log.lastChild) {
      var sep = document.createElement("p");
      sep.className = "sep";
      log.appendChild(sep);
    }
    while (log.childNodes.length > 80 && log.firstChild.classList.contains("old"))
      log.removeChild(log.firstChild);
    log.scrollTop = log.scrollHeight;
  }

  /* ------------------------------------------------------- buttons */

  var actionButtons = [], compassButtons = [];

  function buildControls() {
    var actions = $("actions");
    for (var i = 0; i < 8; i++) {
      var b = document.createElement("button");
      b.className = "action empty";
      b.disabled = true;
      b.innerHTML = "<kbd>" + (i + 1) + "</kbd><span></span>";
      b.addEventListener("click", clickAction.bind(null, i));
      actions.appendChild(b);
      actionButtons.push(b);
    }
    $("arrange").addEventListener("click", function () { setArranging(!arranging); });
    $("arrange-reset").addEventListener("click", function () {
      layoutPrefs = {}; saveLayout(); placeButtons(); arrangePick = -1; drawButtons();
    });
    var compass = $("compass");
    COMPASS.forEach(function (label, i) {
      var b = document.createElement("button");
      b.textContent = label;
      b.disabled = true;
      if (label === "Rest") b.className = "rest";
      b.addEventListener("click", clickCompass.bind(null, i));
      compass.appendChild(b);
      compassButtons.push(b);
    });
  }

  /* ---- the player's own button layout ----
     The server sends 8 labelled buttons and expects the number of the
     one pressed.  Players can put any button in any position: the layout
     is remembered by label (so "Melee" can always sit in the same place)
     and kept in this browser, one layout per character (a fighter and a
     mage play differently); the screens before a character is chosen
     have their own.  The server is still told the original number. */
  var LAYOUT_KEY = "phantasia-button-layout";
  var layoutOwner = "", layoutPrefs = {}, serverLabels = ["", "", "", "", "", "", "", ""];
  var slotServer = [0, 1, 2, 3, 4, 5, 6, 7], buttonsLive = false;
  var arranging = false, arrangePick = -1, compassLive = false;

  function layoutKey() { return layoutOwner ? LAYOUT_KEY + ":" + layoutOwner.toLowerCase() : LAYOUT_KEY; }

  function loadLayout() {
    try { layoutPrefs = JSON.parse(localStorage.getItem(layoutKey()) || "{}") || {}; } catch (e) { layoutPrefs = {}; }
  }

  function saveLayout() {
    try {
      if (Object.keys(layoutPrefs).length) localStorage.setItem(layoutKey(), JSON.stringify(layoutPrefs));
      else localStorage.removeItem(layoutKey());
    } catch (e) {}
  }

  /* a character entered (or left) the realm: use their layout */
  function setLayoutOwner(who) {
    who = who || "";
    if (who === layoutOwner) return;
    layoutOwner = who;
    loadLayout();
    arrangePick = -1;
    placeButtons();
    drawButtons();
    updateArrangeHint();
  }

  function updateArrangeHint() {
    $("arrange-hint").textContent = (layoutOwner ? "Arranging " + layoutOwner + "'s buttons. " : "") +
      "Tap a button, then tap the place it should go.";
  }
  loadLayout();

  /* which server button goes in each of the 8 places */
  function placeButtons() {
    var placed = [null, null, null, null, null, null, null, null], waiting = [];
    serverLabels.forEach(function (label, i) {
      if (label === "") return;
      var want = layoutPrefs[label];
      if (typeof want === "number" && want >= 0 && want < 8 && placed[want] === null) placed[want] = i;
      else waiting.push(i);
    });
    waiting.forEach(function (i) {           /* the rest: own place if free */
      if (placed[i] === null) { placed[i] = i; return; }
      for (var d = 0; d < 8; d++) if (placed[d] === null) { placed[d] = i; return; }
    });
    slotServer = placed;
  }

  function drawButtons() {
    for (var d = 0; d < 8; d++) {
      var i = slotServer[d], label = i === null ? "" : serverLabels[i];
      var b = actionButtons[d];
      b.querySelector("span").textContent = label;
      var tier = i === null ? -1 : serverTiers[i];
      b.querySelector("span").style.color = tier >= 0 ? TIER_COLORS[tier] : "";
      b.classList.toggle("empty", label === "" && !arranging);
      b.classList.toggle("slot-empty", label === "");
      b.classList.toggle("picked", arranging && d === arrangePick);
      b.disabled = arranging ? false : (!buttonsLive || label === "");
    }
  }

  var serverTiers = [-1, -1, -1, -1, -1, -1, -1, -1];
  async function readButtons(withCompass) {
    for (var i = 0; i < 8; i++) serverLabels[i] = await next();
    /* a shop's buttons name the items it just listed: give each its colour */
    var pool = recentItems.slice();
    for (var j = 0; j < 8; j++) {
      serverTiers[j] = -1;
      var lab = serverLabels[j].toLowerCase();
      if (!lab) continue;
      for (var q = 0; q < pool.length; q++) {
        if (pool[q] && pool[q].name.indexOf(lab) >= 0) { serverTiers[j] = pool[q].tier; pool[q] = null; break; }
      }
    }
    recentItems = [];
    placeButtons();
    buttonsLive = true;
    drawButtons();
    compassLive = !!withCompass;
    compassButtons.forEach(function (b) { b.disabled = !withCompass || arranging; });
    pending = "buttons";
  }

  function disableButtons() {
    buttonsLive = false;
    compassLive = false;
    actionButtons.forEach(function (b) { b.disabled = !arranging; });
    compassButtons.forEach(function (b) { b.disabled = true; });
  }

  function clickAction(d) {
    if (arranging) { arrangeClick(d); return; }
    if (pending !== "buttons" || actionButtons[d].disabled || slotServer[d] === null) return;
    pending = null;
    disableButtons();
    respond(String(slotServer[d]));
  }

  /* arrange mode: tap a button, then the place it should go */
  function arrangeClick(d) {
    if (arrangePick < 0) {
      if (slotServer[d] === null) return;   /* pick a button first */
      arrangePick = d;
    }
    else {
      var a = slotServer[arrangePick];
      slotServer[arrangePick] = slotServer[d];
      slotServer[d] = a;
      arrangePick = -1;
      /* remember every button's place in this set, so it stays put */
      for (var k = 0; k < 8; k++) {
        if (slotServer[k] !== null && serverLabels[slotServer[k]] !== "") {
          layoutPrefs[serverLabels[slotServer[k]]] = k;
        }
      }
      saveLayout();
    }
    drawButtons();
  }

  function setArranging(on) {
    arranging = on;
    arrangePick = -1;
    document.getElementById("actions").classList.toggle("arranging", on);
    $("arrange").textContent = on ? "Done" : "Arrange buttons";
    $("arrange").classList.toggle("on", on);
    $("arrange-hint").hidden = !on;
    updateArrangeHint();
    $("arrange-reset").hidden = !on;
    if (!on) {
      placeButtons();
      compassButtons.forEach(function (b) { b.disabled = !(pending === "buttons" && compassLive); });
    }
    else compassButtons.forEach(function (b) { b.disabled = true; });
    drawButtons();
  }

  function clickCompass(i) {
    if (pending !== "buttons" || compassButtons[i].disabled) return;
    pending = null;
    disableButtons();
    respond(String(i + 8));
  }

  /* the server timed out waiting for us */
  function handlePing() {
    if (!pending) return;
    pending = null;
    send(C.PING);
    disableButtons();
    hideModal();
  }

  function disableAll() {
    pending = null;
    disableButtons();
    setChat(false);
  }

  /* ------------------------------------------------------- dialogs */

  function esc(s) {
    return String(s).replace(/[&<>"]/g, function (c) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c];
    });
  }

  var modalOnEscape = null;

  function showModal(title, html, buttons, opts) {
    opts = opts || {};
    $("modal-title").textContent = title;
    $("modal-body").innerHTML = html;
    var box = $("modal-buttons");
    box.innerHTML = "";
    modalOnEscape = null;
    (buttons || []).forEach(function (spec) {
      var b = document.createElement("button");
      b.type = "button";
      b.textContent = spec.label;
      if (spec.primary) b.className = "primary";
      b.addEventListener("click", spec.action);
      if (spec.cancel) modalOnEscape = spec.action;
      box.appendChild(b);
    });
    document.querySelector(".modal-card").classList.toggle("wide", !!opts.wide);
    $("modal").hidden = false;
    var focus = $("modal-body").querySelector("input, select") || box.querySelector(".primary");
    if (focus) setTimeout(function () { focus.focus(); }, 0);
  }

  function hideModal() { $("modal").hidden = true; modalOnEscape = null; }

  function showInfo(title, text) {
    showModal(title, "<p>" + esc(text) + "</p>",
      [{ label: "OK", primary: true, cancel: true, action: hideModal }]);
  }

  function askString(prompt, secret) {
    pending = "dialog";
    showModal("Phantasia Reimagined",
      "<p>" + esc(prompt) + "</p><form id='dlg-form'><input id='dlg-input' type='" +
      (secret ? "password" : "text") + "' maxlength='200' autocomplete='" +
      (secret ? "current-password" : "off") + "'></form>",
      [{ label: "Cancel", cancel: true, action: cancelDialog },
       { label: "OK", primary: true, action: submitString }]);
    $("dlg-form").addEventListener("submit", function (e) { e.preventDefault(); submitString(); });
  }

  function submitString() {
    if (pending !== "dialog") return;
    pending = null;
    var v = $("dlg-input").value;
    hideModal();
    respond(v);
  }

  function cancelDialog() {
    if (pending !== "dialog") return hideModal();
    pending = null;
    hideModal();
    send(C.CANCEL);
  }

  function askCoords(prompt) {
    pending = "dialog";
    showModal("Phantasia Reimagined",
      "<p>" + esc(prompt) + "</p><form id='dlg-form'>" +
      "<label for='dlg-x'>X coordinate</label><input id='dlg-x' type='text' inputmode='decimal'>" +
      "<label for='dlg-y'>Y coordinate</label><input id='dlg-y' type='text' inputmode='decimal'>" +
      "<button type='submit' hidden></button></form>",
      [{ label: "Cancel", cancel: true, action: cancelDialog },
       { label: "OK", primary: true, action: submitCoords }]);
    $("dlg-form").addEventListener("submit", function (e) { e.preventDefault(); submitCoords(); });
  }

  function submitCoords() {
    if (pending !== "dialog") return;
    pending = null;
    var x = $("dlg-x").value, y = $("dlg-y").value;
    hideModal();
    send(C.RESPONSE + x + "\0" + C.RESPONSE + y + "\0");
  }

  function askPlayer(prompt) {
    pending = "dialog";
    var opts = players.map(function (p) {
      return "<option>" + esc(p.name) + "</option>";
    }).join("");
    showModal("Phantasia Reimagined",
      "<p>" + esc(prompt) + "</p><select id='dlg-player'>" + opts + "</select>",
      [{ label: "Cancel", cancel: true, action: cancelDialog },
       { label: "OK", primary: true, action: function () {
         if (pending !== "dialog") return;
         pending = null;
         var v = $("dlg-player").value || "";
         hideModal();
         respond(v);
       } }]);
  }

  /* pick one from a list (wizard tools): answers with the index */
  async function askSelect() {
    var prompt = await next(), count = await nextInt(), opts = "";
    for (var i = 0; i < count; i++) {
      opts += "<option value='" + i + "'>" + esc(await next()) + "</option>";
    }
    pending = "dialog";
    showModal("Phantasia Reimagined",
      "<p>" + esc(prompt) + "</p><select id='dlg-select'>" + opts + "</select>",
      [{ label: "Cancel", cancel: true, action: cancelDialog },
       { label: "OK", primary: true, action: submitSelect }]);
    var sel = $("dlg-select");
    sel.selectedIndex = 0;
    sel.addEventListener("dblclick", submitSelect);
    sel.addEventListener("keydown", function (e) {
      if (e.key === "Enter") { e.preventDefault(); submitSelect(); }
    });
  }

  function submitSelect() {
    if (pending !== "dialog") return;
    pending = null;
    var v = $("dlg-select").value || "0";
    hideModal();
    respond(v);
  }

  async function showScoreboard() {
    var start = await nextInt(), count = await nextInt(), rows = [];
    for (var i = 0; i < count; i++) rows.push(await next());
    var buttons = [];
    if (start > 0) buttons.push({ label: "Previous", action: function () {
      hideModal(); send(C.SCOREBOARD + (start - 50) + "\0"); } });
    if (count !== 1) buttons.push({ label: "Next", action: function () {
      hideModal(); send(C.SCOREBOARD + (start + 50) + "\0"); } });
    buttons.push({ label: "Close", primary: true, cancel: true, action: hideModal });
    showModal("Scoreboard", "<pre>" + esc(rows.join("\n")) + "</pre>", buttons, { wide: true });
  }

  async function facts(spec) {
    // spec: array of [label, fieldCount, formatter] or "gap"
    var html = "<dl class='facts'>";
    for (var i = 0; i < spec.length; i++) {
      if (spec[i] === "gap") { html += "<div class='gap'></div>"; continue; }
      var vals = [];
      for (var n = 0; n < spec[i][1]; n++) vals.push(await next());
      var text = spec[i][2] ? spec[i][2](vals) : vals[0];
      html += "<dt>" + esc(spec[i][0]) + "</dt><dd>" + esc(text) + "</dd>";
    }
    return html + "</dl>";
  }

  function trio(extra) {
    return function (v) { return v[0] + " / (" + v[1] + " + " + v[2] + " " + extra + ")"; };
  }

  async function showExamine() {
    var title = await next();
    var html = await facts([
      ["Location", 1], "gap",
      ["Account", 1], ["Network", 1], ["Channel", 1], "gap",
      ["Level", 1], ["Experience", 1], ["Next level at", 1], "gap",
      ["Energy", 3, trio("shield")], ["Strength", 3, trio("sword")],
      ["Quickness", 3, trio("quicksilver")],
      ["Brains", 1], ["Magic level", 1], ["Mana", 1], ["Gender", 1],
      ["Poison", 1], ["Sin", 1], ["Lives", 1], "gap",
      ["Gold", 1], ["Gems", 1], ["Holy water", 1], ["Amulets", 1],
      ["Charms", 1], ["Staves/Crowns", 1], ["Virgin", 1], ["Blessing", 1],
      ["Palantir", 1], ["Ring", 1], "gap",
      ["Cloaked", 1], ["Blind", 1], ["Age", 1], ["Degenerated", 1],
      ["Played for", 1], ["Loaded on", 1], ["Created on", 1]
    ]);
    showModal(title, html, [{ label: "Close", primary: true, cancel: true, action: hideModal }]);
  }

  async function showDetail() {
    var title = await next();
    var html = "<dl class='facts'><dt>Modified name</dt><dd>" + esc(title) + "</dd></dl>" +
      await facts([
        ["Name", 1], ["Faithful", 1], ["Parent account", 1], ["Parent network", 1],
        ["Mute count", 1], "gap",
        ["Account", 1], ["E-mail", 1], ["Account network", 1], "gap",
        ["IP address", 1], ["Network", 1], ["Machine ID", 1], ["Connected on", 1]
      ]);
    showModal("Connection details: " + title, html,
      [{ label: "Close", primary: true, cancel: true, action: hideModal }]);
  }

  /* ------------------------------------------------------- gear */

  var SLOTS = ["Weapon", "Armor", "Helm", "Boots", "Trinket"];
  var REGALIA_SHAPES = {
    crown: '<path d="M44 14 L48 2 L54 10 L60 0 L66 10 L72 2 L76 14 Z"/>' +
      '<rect x="44" y="12" width="32" height="5" rx="1.5"/>' +
      '<circle cx="60" cy="14.5" r="1.8" fill="#d01820"/><circle cx="51" cy="14.5" r="1.4" fill="#1d4fd8"/>' +
      '<circle cx="69" cy="14.5" r="1.4" fill="#10a050"/>',
    halo: '<ellipse cx="60" cy="5" rx="17" ry="4.5" fill="none" stroke-width="3"/>',
    seal: '<path class="trim" d="M50 38 Q60 54 70 38"/><circle cx="60" cy="56" r="6.5"/>' +
      '<path d="M60 51.5 L61.5 55 L65 55 L62.2 57.2 L63.3 60.7 L60 58.6 L56.7 60.7 L57.8 57.2 L55 55 L58.5 55 Z" fill="#7a5212"/>'
  };
  var TIERS = ["Worn", "Common", "Fine", "Masterwork", "Enchanted", "Artifact"];

  var TIER_COLORS = ["#9c9182", "#d8cbb0", "#7fc97a", "#6aa6e8", "#c58cf0", "#f0cd7a"];

  /* small icons for the bag grid, one per slot */
  var ICONS = {
    Weapon: '<path d="M14 2 L16 2 L16 4 L7 13 L5 11 Z"/><path d="M3 10 L8 15 L7 16 L2 11 Z"/><path d="M4 13 L1 16 L2 17 L5 14 Z"/>',
    Armor: '<path d="M4 3 Q9 1 14 3 L16 6 L14 8 L14 16 Q9 17.5 4 16 L4 8 L2 6 Z"/>',
    Helm: '<path d="M3 12 Q3 3 9 3 Q15 3 15 12 L15 14 L11 14 L11 10 L7 10 L7 14 L3 14 Z"/>',
    Boots: '<path d="M5 2 L10 2 L10 11 L16 13 L16 16 L4 16 Z"/>',
    Trinket: '<path d="M5 2 Q9 9 13 2" fill="none" stroke-width="1.4"/><circle cx="9" cy="11" r="4.5"/>'
  };

  /* the weapon in the adventurer's hand, shaped by what it is */
  var WEAPONS = {
    axe: '<path class="trim" d="M92 92 L104 40"/><path d="M100 46 Q114 44 112 58 Q104 56 99 56 Z"/>',
    hammer: '<path class="trim" d="M92 92 L102 44"/><rect x="94" y="36" width="18" height="11" rx="2" transform="rotate(12 103 41)"/>',
    blade: '<path d="M90 82 L104 30 L107 31 L95 84 Z"/><path d="M84 80 L100 86 L99 89 L83 83 Z"/><path class="trim" d="M90 86 L88 94"/>'
  };

  function weaponShape(name) {
    if (/axe|glaive/i.test(name)) return WEAPONS.axe;
    if (/hammer|mace/i.test(name)) return WEAPONS.hammer;
    return WEAPONS.blade;
  }

  var gearItems = [];

  /* equip ("e") or take off ("u") straight from the gear panel */
  function gearClick(action, it) {
    send(C.GEAR + action + " " + it.index + " " + SLOTS.indexOf(it.slot) + " " +
      (it.equipped ? 1 : 0) + "\0");
  }

  /* clicking a piece on the adventurer takes it off */
  SLOTS.forEach(function (slot) {
    var layer = document.getElementById("d-" + slot);
    if (!layer) return;
    layer.addEventListener("click", function () {
      var it = gearItems.filter(function (x) { return x.equipped && x.slot === slot; })[0];
      if (it) gearClick("u", it);
    });
  });

  function itemTitle(it) {
    return it.name + " (" + TIERS[it.tier] + " " + it.slot.toLowerCase() + ")\n" + it.stats +
      (it.ab1 ? "\nAbility: " + it.ab1 + (it.ab2 ? ", " + it.ab2 : "") : "") +
      (it.equipped ? "\nWorn" : "\nIn your bag");
  }

  async function readGear() {
    var capacity = await nextInt(), bagName = await next();
    var n = await nextInt(), items = [], regalia = null, idx = 0;
    for (var i = 0; i < n; i++) {
      var g = { slot: await next(), name: await next(), stats: await next(),
        equipped: (await next()) === "1", tier: parseInt(await next(), 10) || 0,
        ab1: await next(), ab2: await next() };
      /* a ruler's regalia is not one of their items: it can't be clicked */
      if (g.slot === "Regalia") { regalia = g; continue; }
      g.index = idx++;
      items.push(g);
    }
    gearItems = items;

    /* the regalia: a crown, halo or seal drawn on the adventurer */
    var rl = $("d-Regalia");
    rl.classList.toggle("on", !!regalia);
    rl.innerHTML = "";
    if (regalia) {
      var kind = /Halo/.test(regalia.name) ? "halo" : /Seal/.test(regalia.name) ? "seal" : "crown";
      rl.setAttribute("class", "gear-layer on regal " + kind);
      rl.innerHTML = "<title></title>" + REGALIA_SHAPES[kind];
      rl.querySelector("title").textContent = itemTitle(regalia) + "\nWorn while you hold your title";
    }

    /* the adventurer */
    SLOTS.forEach(function (slot) {
      var it = items.filter(function (x) { return x.equipped && x.slot === slot; })[0];
      var layer = $("d-" + slot);
      layer.classList.toggle("on", !!it);
      layer.classList.toggle("art", !!it && it.tier === 5);
      if (it) {
        if (slot === "Weapon") layer.innerHTML = weaponShape(it.name);
        layer.setAttribute("fill", TIER_COLORS[it.tier]);
        layer.innerHTML = layer.innerHTML.replace(/<title>.*?<\/title>/, "");
        var t = document.createElementNS("http://www.w3.org/2000/svg", "title");
        t.textContent = itemTitle(it) + "\nClick to take it off";
        layer.insertBefore(t, layer.firstChild);
      }
    });

    /* what is worn, by slot */
    var ul = $("gear");
    ul.innerHTML = "";
    if (regalia) {
      var rli = document.createElement("li");
      rli.className = "regalia";
      rli.innerHTML = "<span class='slot'>Regalia</span><span class='gname t5'></span>";
      rli.querySelector(".gname").textContent = regalia.name;
      rli.title = itemTitle(regalia) + "\nWorn while you hold your title";
      ul.appendChild(rli);
    }
    SLOTS.forEach(function (slot) {
      var it = items.filter(function (x) { return x.equipped && x.slot === slot; })[0];
      var li = document.createElement("li");
      li.innerHTML = "<span class='slot'></span><span class='gname'></span>";
      li.querySelector(".slot").textContent = slot;
      var span = li.querySelector(".gname");
      if (it) {
        span.textContent = it.name;
        span.className = "gname t" + it.tier;
        li.title = itemTitle(it) + "\nClick to take it off";
        li.className = "worn";
        li.addEventListener("click", gearClick.bind(null, "u", it));
      } else {
        span.textContent = "\u2014";
        span.className = "gname t0";
      }
      ul.appendChild(li);
    });

    /* the bag: one square per slot */
    var packed = items.filter(function (x) { return !x.equipped; });
    $("bag-name").textContent = bagName;
    $("gear-pack").textContent = packed.length + " / " + capacity;
    var bag = $("bag");
    bag.innerHTML = "";
    for (var k = 0; k < capacity; k++) {
      var cell = document.createElement("div");
      cell.className = "cell";
      var it2 = packed[k];
      if (it2) {
        cell.className = "cell full c" + it2.tier;
        cell.title = itemTitle(it2) + "\nClick to wear it";
        cell.addEventListener("click", gearClick.bind(null, "e", it2));
        cell.innerHTML = '<svg viewBox="0 0 18 18" fill="' + TIER_COLORS[it2.tier] +
          '" stroke="' + TIER_COLORS[it2.tier] + '">' + (ICONS[it2.slot] || "") + "</svg>";
      }
      bag.appendChild(cell);
    }
  }

  /* ------------------------------------------------------- world events */

  var EVENTS = {
    1: { cls: "event-blood", icon: "🌕" },
    2: { cls: "event-gold", icon: "🪙" },
    3: { cls: "event-hunt", icon: "⚔️" },
    4: { cls: "event-siege", icon: "🏰" }
  };
  var eventEnds = 0, eventTimer = null, eventScene = null, eventShown = 0;
  var EVENT_INTRO = {
    1: "The sky bleeds red", 2: "Gold floods the realm", 3: "The hunt is on",
    4: "To arms! The throne is besieged"
  };

  /* the living world: region, town, day or night and season */
  async function readWorld() {
    var region = await next(), state = await next(), shadow = await nextInt();
    var town = await next(), townState = await next(), night = (await next()) === "1";
    var season = await next();
    var box = $("world-status");
    var SEASON_ICON = { "Bloom": "🌸", "Harvest": "🌾", "Hunter's Moon": "🍂", "Deep Frost": "❄️" };
    box.hidden = false;
    box.className = "world-status state-" + state.toLowerCase();
    box.innerHTML = "";
    function chip(text, title, cls) {
      var s = document.createElement("span");
      s.className = "chip" + (cls ? " " + cls : "");
      s.textContent = text;
      if (title) s.title = title;
      box.appendChild(s);
    }
    chip((night ? "🌙 Night" : "☀️ Day"), night ? "Monsters are stronger and roam more at night" : "");
    chip((SEASON_ICON[season] || "") + " " + season, "This week's season (/realm for its effect)");
    var where = region + " · " + state + (state === "Blighted" || state === "Contested" || state === "Liberated" ? " (" + shadow + "% Shadow)" : "");
    chip(where, state === "Blighted" ? "Monsters here are stronger and richer" :
      state === "Liberated" ? "Safer roads and cheaper shops" : "", "region");
    if (town) chip("🏘 " + town + " · " + townState, "The local town (its shops and inn)", "town");
  }

  function stopEventScene() {
    if (eventScene) { try { eventScene.dispose(); } catch (e) {} eventScene = null; }
    document.body.classList.remove("event-3d");
  }

  /* the event's own 3D world behind the game */
  function startEventScene(id) {
    load3d().then(function (m) {
      if (eventShown !== id || eventScene) return;
      try {
        eventScene = m.ambient(id, $("event-fx"), {   /* rests while a ceremony plays */
          paused: function () { return !!document.querySelector(".arrival.has3d"); }
        });
        document.body.classList.add("event-3d");
      } catch (e) { /* keep the CSS version */ }
    }, function () {});
  }

  /* the announcement, once per event per visit; waits for any ceremony
     already on screen (a wizard logging in) to finish */
  function eventIntro(id, name, effect, ends) {
    var key = "phantasia-intro-" + id + "-" + Math.round(ends / 60000);
    try { if (sessionStorage.getItem(key)) return; sessionStorage.setItem(key, "1"); } catch (e) {}
    var tries = 0;
    (function go() {
      if (eventShown !== id) return;
      if (document.querySelector(".arrival") && tries++ < 30) { setTimeout(go, 500); return; }
      showCeremony("event " + EVENTS[id].cls, '<div class="flash"></div>', EVENT_INTRO[id], name,
        effect.charAt(0).toUpperCase() + effect.slice(1), 200 + id);
    })();
  }

  async function readWorldEvent() {
    var id = await nextInt(), name = await next(), left = await nextInt(), effect = await next();
    showWorldEvent(id, name, left, effect);
  }

  function showWorldEvent(id, name, left, effect) {
    var body = document.body, fx = $("event-fx"), banner = $("event-banner");
    Object.keys(EVENTS).forEach(function (k) { body.classList.remove(EVENTS[k].cls); });
    body.classList.remove("event-on");
    clearInterval(eventTimer);

    var ev = EVENTS[id];
    if (!ev || left <= 0) {
      eventShown = 0;
      stopEventScene();
      fx.innerHTML = "";
      banner.hidden = true;
      return;
    }

    var same = eventShown === id;            /* a refresh of the same event */
    if (!same) { stopEventScene(); fx.innerHTML = ""; }
    eventShown = id;

    body.classList.add("event-on", ev.cls);
    $("event-icon").textContent = ev.icon;
    $("event-name").textContent = name + "!";
    $("event-effect").textContent = effect.charAt(0).toUpperCase() + effect.slice(1) + ".";
    banner.hidden = false;

    if (id === 2 && !same) { /* Gold Rush: drifting coins (until the 3D is up) */
      for (var i = 0; i < 22; i++) {
        var coin = document.createElement("span");
        coin.className = "coin";
        coin.style.left = (Math.random() * 100) + "%";
        coin.style.animationDuration = (7 + Math.random() * 8) + "s";
        coin.style.animationDelay = (-Math.random() * 15) + "s";
        var size = 9 + Math.random() * 9;
        coin.style.width = coin.style.height = size + "px";
        fx.appendChild(coin);
      }
    }

    eventEnds = Date.now() + left * 1000;
    if (!same) { startEventScene(id); eventIntro(id, name, effect, eventEnds); }
    var tick = function () {
      var s = Math.max(0, Math.round((eventEnds - Date.now()) / 1000));
      $("event-time").textContent = Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2) + " left";
      if (s <= 0) showWorldEvent(0, "", 0, "");
    };
    tick();
    eventTimer = setInterval(tick, 1000);
  }

  /* ------------------------------------------------------- ceremonies */

  /* gilded corner flourish (one corner; mirrored by CSS) */
  var CORNER = '<svg viewBox="0 0 74 74" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">' +
    '<path d="M4 70 V18 Q4 4 18 4 H70"/><path d="M12 62 V24 Q12 12 24 12 H62" stroke-width="1"/>' +
    '<path d="M18 4 Q30 16 18 28 Q8 16 18 4Z" fill="currentColor" stroke="none" opacity=".85"/>' +
    '<circle cx="4" cy="70" r="3" fill="currentColor"/><circle cx="70" cy="4" r="3" fill="currentColor"/>' +
    '<path d="M28 28 q10 -14 22 -6 M28 28 q-14 10 -6 22" stroke-width="1.3"/></svg>';

  function card(rank, name, what) {
    return '<div class="card">' +
      ['tl', 'tr', 'bl', 'br'].map(function (k) { return '<span class="corner ' + k + '">' + CORNER + '</span>'; }).join("") +
      '<div class="rank"><span class="orn">❧</span><span class="r"></span><span class="orn flip">❧</span></div>' +
      '<div class="who"></div>' +
      '<div class="rule"><span></span>✦<span></span></div>' +
      '<div class="what"></div></div>';
  }

  function fillCard(el, rank, name, what) {
    el.querySelector(".r").textContent = rank;
    var who = el.querySelector(".who");
    who.textContent = name;
    who.setAttribute("data-name", name);
    el.querySelector(".what").textContent = what;
  }

  /* a silk banner on a gilded rod */
  var PENNANT = '<svg class="pennant SIDE" viewBox="0 0 190 470">' +
    '<defs><linearGradient id="silkSIDE" x1="0" x2="1">' +
    '<stop offset="0" stop-color="#0c3a1f"/><stop offset=".22" stop-color="#1f6b3c"/><stop offset=".45" stop-color="#11482a"/>' +
    '<stop offset=".7" stop-color="#22743f"/><stop offset="1" stop-color="#0c3a1f"/></linearGradient>' +
    '<linearGradient id="goldSIDE" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color="#fff0b4"/>' +
    '<stop offset=".5" stop-color="#d9a640"/><stop offset="1" stop-color="#8a5c14"/></linearGradient></defs>' +
    '<rect x="0" y="6" width="190" height="12" rx="6" fill="url(#goldSIDE)"/>' +
    '<circle cx="6" cy="12" r="10" fill="url(#goldSIDE)"/><circle cx="184" cy="12" r="10" fill="url(#goldSIDE)"/>' +
    '<path d="M18 18 H172 V400 L95 450 L18 400 Z" fill="url(#silkSIDE)" stroke="url(#goldSIDE)" stroke-width="7"/>' +
    '<path d="M34 34 H156 V392 L95 432 L34 392 Z" fill="none" stroke="#d9a640" stroke-width="1.5" stroke-dasharray="2 6" opacity=".8"/>' +
    '<text x="95" y="250" text-anchor="middle" font-size="120" fill="url(#goldSIDE)" font-family="serif">⚜</text>' +
    '<path d="M95 400 v40" stroke="url(#goldSIDE)" stroke-width="3"/>' +
    '<path d="M86 438 h18 l-4 28 h-10z" fill="url(#goldSIDE)"/></svg>';

  /* a jewelled crown with a velvet cap */
  var CROWN = '<svg class="crown" viewBox="0 0 230 170">' +
    '<defs><linearGradient id="cg" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color="#fff3bf"/>' +
    '<stop offset=".45" stop-color="#e6b445"/><stop offset=".75" stop-color="#b07a1c"/><stop offset="1" stop-color="#f2cd6a"/></linearGradient>' +
    '<radialGradient id="ruby" cx=".35" cy=".35"><stop offset="0" stop-color="#ffd2cc"/><stop offset=".35" stop-color="#e5352c"/><stop offset="1" stop-color="#6a0d08"/></radialGradient>' +
    '<radialGradient id="sapph" cx=".35" cy=".35"><stop offset="0" stop-color="#d6e6ff"/><stop offset=".35" stop-color="#3b6fd0"/><stop offset="1" stop-color="#0d235a"/></radialGradient>' +
    '<radialGradient id="emer" cx=".35" cy=".35"><stop offset="0" stop-color="#d4ffd9"/><stop offset=".35" stop-color="#2fa457"/><stop offset="1" stop-color="#0b3f1d"/></radialGradient>' +
    '<linearGradient id="vel" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color="#a3141f"/><stop offset="1" stop-color="#4a050b"/></linearGradient></defs>' +
    '<path d="M42 110 Q115 18 188 110 Z" fill="url(#vel)"/>' +
    '<path d="M22 132 L30 52 L66 92 L92 34 L115 78 L138 34 L164 92 L200 52 L208 132 Z" fill="url(#cg)" stroke="#7a5210" stroke-width="3" stroke-linejoin="round"/>' +
    '<circle cx="30" cy="48" r="9" fill="url(#cg)" stroke="#7a5210" stroke-width="2"/><circle cx="200" cy="48" r="9" fill="url(#cg)" stroke="#7a5210" stroke-width="2"/>' +
    '<circle cx="92" cy="30" r="8" fill="url(#cg)" stroke="#7a5210" stroke-width="2"/><circle cx="138" cy="30" r="8" fill="url(#cg)" stroke="#7a5210" stroke-width="2"/>' +
    '<path d="M115 2 v24 M105 12 h20" stroke="url(#cg)" stroke-width="7" stroke-linecap="round"/>' +
    '<rect x="18" y="124" width="194" height="34" rx="6" fill="url(#cg)" stroke="#7a5210" stroke-width="3"/>' +
    '<ellipse cx="115" cy="141" rx="13" ry="11" fill="url(#ruby)"/>' +
    '<ellipse cx="68" cy="141" rx="10" ry="9" fill="url(#sapph)"/><ellipse cx="162" cy="141" rx="10" ry="9" fill="url(#sapph)"/>' +
    '<ellipse cx="34" cy="141" rx="7" ry="7" fill="url(#emer)"/><ellipse cx="196" cy="141" rx="7" ry="7" fill="url(#emer)"/>' +
    '<ellipse cx="115" cy="86" rx="9" ry="12" fill="url(#ruby)"/>' +
    '<path d="M24 130 H206" stroke="#fff6cf" stroke-width="2" opacity=".6"/></svg>';

  /* a ring of glowing runes */
  function runeRing(cls, glyphs) {
    var id = "rp-" + cls.replace(/\s+/g, "-");
    return '<svg class="' + cls + '" viewBox="0 0 400 400" fill="none" stroke="currentColor">' +
      '<defs><path id="' + id + '" d="M200 200 m-160 0 a160 160 0 1 1 320 0 a160 160 0 1 1 -320 0"/></defs>' +
      '<circle cx="200" cy="200" r="185" stroke-width="1.5"/><circle cx="200" cy="200" r="178" stroke-width="0.6"/>' +
      '<circle cx="200" cy="200" r="136" stroke-width="1"/>' +
      '<text font-size="22" fill="currentColor" stroke="none" letter-spacing="7"><textPath href="#' + id + '">' + glyphs + '</textPath></text>' +
      '<path d="M200 15 L360 292 L40 292 Z M200 385 L40 108 L360 108 Z" stroke-width="0.8" opacity=".6"/></svg>';
  }
  var RUNES = "ᚠᚢᚦᚨᚱᚲᚷᚹᚺᚾᛁᛃᛇᛈᛉᛊᛏᛒᛖᛗᛚᛜᛞᛟ";
  var RUNES2 = RUNES + RUNES;

  var BOLT = '<svg class="bolt" viewBox="0 0 140 600" preserveAspectRatio="none">' +
    '<path d="M78 0 L52 170 L84 176 L40 330 L76 336 L30 600 L110 300 L74 296 L112 150 L80 146 L100 0 Z" fill="#eef4ff"/></svg>';

  function sparks(el, count, delay, reach) {
    for (var i = 0; i < count; i++) {
      var s = document.createElement("span");
      s.className = "spark";
      var a = Math.random() * Math.PI * 2, r = 120 + Math.random() * reach;
      s.style.setProperty("--dx", Math.cos(a) * r + "px");
      s.style.setProperty("--dy", Math.sin(a) * r + "px");
      s.style.animationDelay = delay + Math.random() * 0.4 + "s";
      el.appendChild(s);
    }
  }

  function shake() {
    var app = $("app");
    app.classList.remove("shake"); void app.offsetWidth; app.classList.add("shake");
    setTimeout(function () { app.classList.remove("shake"); }, 1000);
  }

  /* the 3D scenes (ceremonies3d.js, three.js) load the first time a ceremony
     plays; until they are ready, or if the device can't do WebGL, the CSS
     scenery shows instead */
  var scenes3d = null, ready3d = null;
  function webglOk() {
    try {
      var cv = document.createElement("canvas");
      return !!(window.WebGLRenderingContext &&
        (cv.getContext("webgl2") || cv.getContext("webgl")));
    } catch (e) { return false; }
  }
  function load3d() {
    if (!scenes3d) {
      var ok = !window.matchMedia("(prefers-reduced-motion: reduce)").matches &&
        HTMLScriptElement.supports && HTMLScriptElement.supports("importmap") && webglOk();
      scenes3d = ok ? import("./ceremonies3d.js") : Promise.reject(new Error("no 3D"));
      scenes3d.then(function (m) { ready3d = m; }, function () {});
    }
    return scenes3d;
  }
  /* fetch the 3D in the background once the page has loaded, so it is
     ready before the first ceremony (a wizard's plays right at login) */
  window.addEventListener("load", function () {
    if (window.requestIdleCallback) window.requestIdleCallback(function () { load3d(); }, { timeout: 2000 });
    else setTimeout(load3d, 300);
  });

  function closeCeremony(el) {
    if (el._scene) { try { el._scene.dispose(); } catch (e) {} el._scene = null; }
    if (el.parentNode) el.remove();
  }

  function play3d(el, m, kind) {
    try {
      el._scene = m.play(kind, el);
      el.classList.remove("loading3d");
      el.classList.add("has3d");
      return true;
    } catch (e) {               /* WebGL refused: flat version from now on */
      ready3d = null;
      scenes3d = Promise.reject(e);
      scenes3d.catch(function () {});
      el.classList.remove("loading3d");
      return false;
    }
  }

  function showCeremony(cls, scenery, rank, name, what, kind) {
    var old = document.querySelector(".arrival");
    if (old) closeCeremony(old);
    var el = document.createElement("div");
    el.className = "arrival " + cls;
    el.setAttribute("role", "status");
    el.innerHTML = scenery + card();
    fillCard(el, rank, name, what);
    var done = function () { closeCeremony(el); };
    el.addEventListener("click", done);
    document.body.appendChild(el);
    setTimeout(done, 6200);

    if (ready3d) { play3d(el, ready3d, kind); return el; }

    /* still downloading: show only the title card until the 3D is ready.
       The flat scenery appears only if 3D fails or takes too long, and
       then stays, so the two never swap mid-ceremony */
    el.classList.add("loading3d");
    var settled = false;
    var giveUp = setTimeout(function () {
      if (settled) return;
      settled = true;
      el.classList.remove("loading3d");
    }, 2500);
    load3d().then(function (m) {
      if (settled || !el.parentNode) return;
      settled = true; clearTimeout(giveUp);
      play3d(el, m, kind);
    }, function () {
      if (settled) return;
      settled = true; clearTimeout(giveUp);
      el.classList.remove("loading3d");
    });
    return el;
  }

  /* someone won a title: Steward 101, King 102, Queen 103, Valar 104 */
  function ceremony(kind, name) {
    var el;
    if (kind < 100) { wizardArrives(name, kind); return; }

    if (kind === 101) {
      showCeremony("steward", PENNANT.replace(/SIDE/g, "left") + PENNANT.replace(/SIDE/g, "right"),
        "Steward", name, "has become Steward of the realm", kind);
    }
    else if (kind === 102 || kind === 103) {
      el = showCeremony("king", '<div class="rays"></div>' + CROWN,
        kind === 102 ? "Long live the King" : "Long live the Queen", name, "has claimed the throne!", kind);
      for (var i = 0; i < 46; i++) {          /* gold leaf tumbling down */
        var leaf = document.createElement("span");
        leaf.className = "leaf";
        leaf.style.left = Math.random() * 100 + "%";
        leaf.style.animationDuration = 3 + Math.random() * 3 + "s";
        leaf.style.animationDelay = 0.8 + Math.random() * 2 + "s";
        el.appendChild(leaf);
      }
    }
    else if (kind === 104) {
      el = showCeremony("valar", '<div class="pillar"></div>' + runeRing("runes", RUNES2),
        "Behold the new Valar", name, "ascends to the Valar!", kind);
      for (var j = 0; j < 70; j++) {          /* the heavens: stars */
        var st = document.createElement("span");
        st.className = "star";
        st.style.left = Math.random() * 100 + "%";
        st.style.top = Math.random() * 100 + "%";
        st.style.animationDelay = -Math.random() * 1.6 + "s";
        el.appendChild(st);
      }
      shake();
    }
  }

  /* a wizard (3-4) or apprentice (2) enters the realm */
  function wizardArrives(name, level) {
    var big = level >= 3, el;
    if (big) {
      el = showCeremony("wizard", '<div class="flash"></div>' + runeRing("sigil", RUNES2) +
        runeRing("sigil inner", RUNES) + BOLT, "Wizard", name, "has entered the realm!", level);
      sparks(el, 40, 0.15, 400);
      if (level >= 4) shake();
    }
    else {
      el = showCeremony("apprentice", '<div class="smoke"></div>', "Apprentice", name,
        "appears in a puff of smoke", 2);
      sparks(el, 14, 0.3, 180);
    }
  }

  /* ------------------------------------------------------- players */

  function renderPlayers() {
    var ul = $("players");
    ul.innerHTML = "";
    players.forEach(function (p) {
      var li = document.createElement("li");
      li.title = "Examine " + p.name;
      li.innerHTML = "<b></b><span></span>";
      li.querySelector("b").textContent = p.name;
      li.querySelector("span").textContent = p.desc.replace(/-\s*$/, "").trim();
      li.addEventListener("click", function () { send(C.EXAMINE + p.name + "\0"); });
      ul.appendChild(li);
    });
    $("player-count").textContent = players.length ? "(" + players.length + ")" : "";
  }

  /* ------------------------------------------------------- chat */

  function setChat(on) {
    $("chat-input").disabled = !on;
    $("chat-send").disabled = !on;
    $("chat-input").placeholder = on ? "Say something…" : "Chat opens once you're in the realm";
  }

  /* chat: each speaker gets a colour of their own; announcements and
     replies to your commands look different from people talking */
  function nameHue(name) {
    var h = 0;
    for (var i = 0; i < name.length; i++) h = (h * 31 + name.charCodeAt(i)) % 360;
    return h;
  }
  /* a speaker must be someone in the realm (titles like "King Blue" too) */
  function isPlayerName(who) {
    var w = who.toLowerCase();
    if (name && w === name.toLowerCase()) return true;
    return players.some(function (pl) {
      var n = pl.name.toLowerCase();
      return w === n || w.slice(-n.length - 1) === " " + n;
    });
  }
  function addChat(text) {
    var log = $("chat-log");
    var p = document.createElement("p");
    var m = /^(\((\d+)\) - )?([^:]{1,48}?)( proclaims)?: (.*)$/.exec(text);
    var me = (name || "").toLowerCase();
    if (/^\*\*\*/.test(text)) {
      p.className = "c-announce";
      p.textContent = text.replace(/^\*+\s*|\s*\*+$/g, "");
    }
    else if (m && isPlayerName(m[3])) {
      /* someone talking: "Name: message" */
      var who = document.createElement("b");
      who.className = "who";
      who.textContent = m[3] + (m[4] || "");
      who.style.color = "hsl(" + nameHue(m[3]) + ", 70%, 72%)";
      if (m[2]) {
        var ch = document.createElement("span");
        ch.className = "chan";
        ch.textContent = "[" + m[2] + "] ";
        p.appendChild(ch);
        p.classList.add("c-other");
      }
      p.appendChild(who);
      p.appendChild(document.createTextNode(": " + m[5]));
      if (m[4]) p.classList.add("c-proclaim");
      if (me && m[3].toLowerCase().indexOf(me) < 0 && m[5].toLowerCase().indexOf(me) >= 0) p.classList.add("c-mention");
    }
    else {
      /* the realm's news and answers to your commands */
      p.textContent = text;
      p.className = /has slain|has claimed|nemesis|champion|has completed|liberat|has fallen|retaken|crowned|Hail |Long live|Behold the new Valar|has found (the |[A-Z])/.test(text) ? "c-news" : "c-system";
    }
    log.appendChild(p);
    while (log.childNodes.length > MAX_CHAT) log.removeChild(log.firstChild);
    log.scrollTop = log.scrollHeight;
  }

  /* ------------------------------------------------------- misc UI */

  function setConn(state, label) {
    var el = $("conn");
    el.className = "conn " + state;
    el.querySelector("span").textContent = label;
  }

  var ARROWS = { ArrowUp: 1, ArrowDown: 7, ArrowLeft: 3, ArrowRight: 5 };

  document.addEventListener("keydown", function (e) {
    if (!$("modal").hidden) {
      if (e.key === "Escape" && modalOnEscape) { e.preventDefault(); modalOnEscape(); }
      return;
    }
    var tag = (e.target.tagName || "").toLowerCase();
    if (tag === "input" || tag === "textarea" || tag === "select") return;
    if (e.ctrlKey || e.metaKey || e.altKey) return;

    if (arranging) {
      if (e.key === "Escape" || e.key === "Enter") { e.preventDefault(); setArranging(false); }
      return;
    }
    var m = /^F([1-8])$/.exec(e.key);
    if (m) { e.preventDefault(); clickAction(+m[1] - 1); return; }

    if (e.code && /^Numpad[1-9]$/.test(e.code)) {
      var n = +e.code.slice(-1);       // keypad layout: 7 8 9 / 4 5 6 / 1 2 3
      var map = { 7: 0, 8: 1, 9: 2, 4: 3, 5: 4, 6: 5, 1: 6, 2: 7, 3: 8 };
      e.preventDefault(); clickCompass(map[n]); return;
    }
    if (/^[1-8]$/.test(e.key)) { clickAction(+e.key - 1); return; }
    if (e.key in ARROWS) { e.preventDefault(); clickCompass(ARROWS[e.key]); return; }
    if (e.key === " ") {
      e.preventDefault();
      for (var i = 0; i < 8; i++) if (!actionButtons[i].disabled && slotServer[i] !== null) { clickAction(i); return; }
    }
  });

  $("chat-form").addEventListener("submit", function (e) {
    e.preventDefault();
    var input = $("chat-input");
    if (input.disabled || !input.value.trim()) return;
    send(C.CHAT + input.value + "\0");
    input.value = "";
  });

  $("modal").addEventListener("click", function (e) {
    if (e.target === $("modal") && modalOnEscape) modalOnEscape();
  });

  window.addEventListener("beforeunload", function (e) {
    if (ws && ws.readyState === 1 && name) { e.preventDefault(); e.returnValue = ""; }
  });

  buildControls();
  connect();
})();
