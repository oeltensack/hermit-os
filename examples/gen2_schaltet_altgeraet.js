// Script fuer Shelly Gen2/Gen3:
// Das eigene Relais (switch:0) wird auf ein altes Shelly 1/1PM mit Shelly Gen1 OS gespiegelt.
// Anpassen: IP des Altgeraets.

let ALT = "http://192.168.0.50";

function altSet(on) {
  Shelly.call("HTTP.GET",
    { url: ALT + "/rpc/Switch.Set?id=0&on=" + (on ? "true" : "false"), timeout: 5 },
    function (res, err, msg) {
      if (err !== 0) print("Altgeraet nicht erreichbar:", msg);
    });
}

Shelly.addStatusHandler(function (ev) {
  if (ev.component === "switch:0" && typeof ev.delta.output !== "undefined") {
    altSet(ev.delta.output);
  }
});
