/*
 * Phantasia IV browser client.
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
    PALANTIR: 55, RING: 56, VIRGIN: 57
  };

  /* ---- client -> server ---- */
  var C = { RESPONSE: "1\0", CANCEL: "2\0", PING: "3\0", CHAT: "4\0",
            EXAMINE: "5\0", SCOREBOARD: "7\0" };

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
          var gone = await next();
          players = players.filter(function (p) { return p.name !== gone; });
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
        case P.DIALOG: showInfo("Phantasia", await next()); break;
        case P.CHAT: addChat(await next()); break;
        case P.ACTIVATE_CHAT: setChat(true); break;
        case P.DEACTIVATE_CHAT: setChat(false); break;
        case P.PLAYER_INFO: await showExamine(); break;
        case P.CONNECTION_DETAIL: await showDetail(); break;
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
        if (!name) { $("title-head").textContent = "Welcome to Phantasia!"; $("title-tail").textContent = ""; }
        document.title = name ? name + " — Phantasia IV" : "Phantasia IV";
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

  function addLog(text, cls) {
    var log = $("log");
    var stick = log.scrollTop + log.clientHeight >= log.scrollHeight - 30;
    var p = document.createElement("p");
    if (text === "") p.className = "sep";
    else p.textContent = text;
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

  async function readButtons(withCompass) {
    for (var i = 0; i < 8; i++) {
      var label = await next();
      var b = actionButtons[i];
      b.querySelector("span").textContent = label;
      b.classList.toggle("empty", label === "");
      b.disabled = label === "";
    }
    compassButtons.forEach(function (b) { b.disabled = !withCompass; });
    pending = "buttons";
  }

  function disableButtons() {
    actionButtons.forEach(function (b) { b.disabled = true; });
    compassButtons.forEach(function (b) { b.disabled = true; });
  }

  function clickAction(i) {
    if (pending !== "buttons" || actionButtons[i].disabled) return;
    pending = null;
    disableButtons();
    respond(String(i));
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
    showModal("Phantasia",
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
    showModal("Phantasia",
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
    showModal("Phantasia",
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

  function addChat(text) {
    var log = $("chat-log");
    var p = document.createElement("p");
    p.textContent = text;
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
      for (var i = 0; i < 8; i++) if (!actionButtons[i].disabled) { clickAction(i); return; }
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
