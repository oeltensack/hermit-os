// Script fuer Shelly Gen2/Gen3: empfaengt Ereignisse vom Altgeraet.
//
// Im Altgeraet unter "Aktionen" z. B. anlegen:
//   Ereignis input.toggle_on  -> http://<IP-Gen2>/script/<Script-ID>/alt?ev=${event}&out=${output}&p=${apower}
//   Ereignis input.toggle_off -> dieselbe URL
//   Ereignis switch.power_above (Schwelle 1000 W) -> dieselbe URL
// <Script-ID> steht in der Gen2-Weboberflaeche beim Script (meist 1).

function parseQuery(q) {
  let r = {};
  if (!q) return r;
  let parts = q.split("&");
  for (let i = 0; i < parts.length; i++) {
    let kv = parts[i].split("=");
    r[kv[0]] = kv.length > 1 ? kv[1] : "";
  }
  return r;
}

HTTPServer.registerEndpoint("alt", function (req, res) {
  let q = parseQuery(req.query);
  print("Altgeraet:", q.ev, "Relais:", q.out, "Leistung:", q.p);

  // Beispiel: Wandschalter am Altgeraet schaltet das Gen2-Relais
  if (q.ev === "input.toggle_on")  Shelly.call("Switch.Set", { id: 0, on: true });
  if (q.ev === "input.toggle_off") Shelly.call("Switch.Set", { id: 0, on: false });

  res.code = 200;
  res.body = "ok";
  res.send();
});
