// Knopf "Log kopieren" für den ESPHome-Webserver (eingebunden über web_server: js_include).
// Liest den Log-Stream (/events, Ereignis "log") mit, hält die letzten Zeilen ohne Farbcodes
// und kopiert sie per Knopfdruck in die Zwischenablage. Funktioniert auch ohne HTTPS.
(function () {
  const MAX_LINES = 2000;
  const lines = [];
  const ansi = /\x1b\[[0-9;]*m/g;
  const pad = (n) => String(n).padStart(2, "0");
  const stamp = () => {
    const d = new Date();
    return pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" + pad(d.getSeconds());
  };

  let source = null;
  function connect() {
    source = new EventSource("/events");
    source.addEventListener("log", (e) => {
      lines.push(stamp() + " " + String(e.data).replace(ansi, ""));
      if (lines.length > MAX_LINES) lines.splice(0, lines.length - MAX_LINES);
      counter.textContent = lines.length;
    });
    source.onerror = () => {
      source.close();
      setTimeout(connect, 5000);
    };
  }

  function copy(text) {
    // Ohne HTTPS gibt es navigator.clipboard nicht, daher zuerst der klassische Weg
    const area = document.createElement("textarea");
    area.value = text;
    area.setAttribute("readonly", "");
    area.style.position = "fixed";
    area.style.opacity = "0";
    document.body.appendChild(area);
    area.select();
    let ok = false;
    try {
      ok = document.execCommand("copy");
    } catch (err) {
      ok = false;
    }
    document.body.removeChild(area);
    if (!ok && navigator.clipboard) {
      navigator.clipboard.writeText(text).then(() => flash("kopiert"), () => flash("Fehler"));
      return;
    }
    flash(ok ? "kopiert" : "Fehler");
  }

  function button(label, onClick) {
    const b = document.createElement("button");
    b.textContent = label;
    b.style.cssText =
      "margin-left:6px;padding:6px 10px;border:1px solid #888;border-radius:6px;" +
      "background:#2b6cb0;color:#fff;font:13px sans-serif;cursor:pointer";
    b.onclick = onClick;
    return b;
  }

  const bar = document.createElement("div");
  bar.style.cssText =
    "position:fixed;right:12px;bottom:12px;z-index:9999;display:flex;align-items:center;" +
    "font:12px sans-serif;background:rgba(0,0,0,.55);color:#fff;padding:6px 8px;border-radius:8px";
  const info = document.createElement("span");
  info.textContent = "Log-Zeilen: ";
  const counter = document.createElement("span");
  counter.textContent = "0";
  const status = document.createElement("span");
  status.style.marginLeft = "6px";
  function flash(text) {
    status.textContent = text;
    setTimeout(() => (status.textContent = ""), 2000);
  }
  bar.append(info, counter,
    button("Log kopieren", () => copy(lines.join("\n"))),
    button("Log leeren", () => { lines.length = 0; counter.textContent = "0"; }),
    status);

  const start = () => { document.body.appendChild(bar); connect(); };
  if (document.body) start(); else document.addEventListener("DOMContentLoaded", start);
})();
