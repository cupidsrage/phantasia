/*
 * Shows a strip at the top of a website page while a world event is on
 * (Blood Moon, Gold Rush, Champion Hunt), with a link into the game.
 */
(function () {
  var LOOKS = {
    1: { icon: "🌕", bg: "linear-gradient(90deg,#5a0d0b,#2a0606)", edge: "#c2392e", title: "#ff9a8a" },
    2: { icon: "🪙", bg: "linear-gradient(90deg,#5a430c,#261c05)", edge: "#e0b23a", title: "#ffe08a" },
    3: { icon: "⚔️", bg: "linear-gradient(90deg,#4a1240,#1e0b2e)", edge: "#a54bc2", title: "#f0b2ff" }
  };

  function show(ev) {
    var look = LOOKS[ev.event];
    if (!look || !(ev.secondsLeft > 0)) return;
    var bar = document.createElement("div");
    bar.style.cssText = "position:sticky;top:0;z-index:50;display:flex;flex-wrap:wrap;align-items:center;" +
      "justify-content:center;gap:10px;padding:10px 16px;font:16px Georgia,serif;color:#f3e6c8;" +
      "background:" + look.bg + ";border-bottom:2px solid " + look.edge + ";box-shadow:0 0 18px " + look.edge;
    var mins = Math.max(1, Math.round(ev.secondsLeft / 60));
    bar.innerHTML = '<span style="font-size:22px">' + look.icon + '</span>' +
      '<b style="color:' + look.title + ';letter-spacing:.04em"></b>' +
      '<span class="fx"></span>' +
      '<a href="client/" style="color:' + look.title + ';font-weight:bold">Play now &rsaquo;</a>';
    bar.querySelector("b").textContent = ev.name + "!";
    bar.querySelector(".fx").textContent = ev.effect.charAt(0).toUpperCase() + ev.effect.slice(1) +
      " — " + mins + " minute" + (mins === 1 ? "" : "s") + " left.";
    document.body.insertBefore(bar, document.body.firstChild);
  }

  if (window.fetch) {
    fetch("/api/event", { cache: "no-store" })
      .then(function (r) { return r.json(); })
      .then(show)
      .catch(function () {});
  }
})();
