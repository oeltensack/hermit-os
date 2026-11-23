// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// Script fuer Shelly Gen2/Gen3: fragt alle 10 s die Leistung eines alten Shelly 1PM ab
// und schaltet das eigene Relais, wenn eine Schwelle ueberschritten wird
// (z. B. Absaugung an, sobald die Saege laeuft).

let ALT = "http://192.168.0.51";
let SCHWELLE_W = 50;
let NACHLAUF_S = 30;
let aus_timer = null;
let an = false;

function poll() {
  Shelly.call("HTTP.GET", { url: ALT + "/rpc/Switch.GetStatus?id=0", timeout: 5 },
    function (res, err) {
      if (err !== 0 || res.code !== 200) return;
      let st = JSON.parse(res.body);
      if (st.apower > SCHWELLE_W) {
        if (aus_timer !== null) { Timer.clear(aus_timer); aus_timer = null; }
        Shelly.call("Switch.Set", { id: 0, on: true });
        an = true;
      } else if (an && aus_timer === null) {
        aus_timer = Timer.set(NACHLAUF_S * 1000, false, function () {
          aus_timer = null;
          an = false;
          Shelly.call("Switch.Set", { id: 0, on: false });
        });
      }
    });
}

Timer.set(10000, true, poll);
