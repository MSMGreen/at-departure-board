#pragma once
#include <Arduino.h>

const char PORTAL_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Departure board setup</title>
<style>
 :root{color-scheme:light dark}
 body{font:15px system-ui,sans-serif;margin:0;padding:16px;max-width:760px}
 h1{font-size:20px;margin:0 0 16px}
 fieldset{border:1px solid #8886;border-radius:6px;margin:0 0 14px;padding:12px}
 legend{padding:0 6px;font-weight:600}
 label{display:block;font-size:13px;opacity:.8;margin:8px 0 2px}
 input[type=text]{width:100%;box-sizing:border-box;padding:6px;font:inherit;
   border:1px solid #8886;border-radius:4px;background:transparent;color:inherit}
 .row{display:flex;gap:10px;flex-wrap:wrap}
 .row>div{flex:1 1 150px}
 button{font:inherit;padding:6px 12px;border-radius:4px;border:1px solid #8886;
   background:transparent;color:inherit;cursor:pointer}
 button:disabled{opacity:.5;cursor:default}
 button.primary{background:#2563eb;border-color:#2563eb;color:#fff;padding:8px 20px}
 .note{font-size:12px;opacity:.7;margin:4px 0 0}
 .ok{color:#15803d}.bad{color:#b91c1c}
 #status{margin-left:12px;font-size:13px}
</style>
</head>
<body>
<h1>Departure board setup</h1>

<fieldset><legend>Board</legend>
 <label for="loc">Location shown on the panel</label>
 <input type="text" id="loc" maxlength="23">
 <label for="theme">Theme</label>
 <select id="theme"></select>
 <p class="note">The theme changes on the panel straight away. Everything else
  applies when you save, which restarts the board.</p>
</fieldset>

<fieldset><legend>Watches</legend>
 <div id="watches"></div>
 <button type="button" id="add">Add watch</button>
 <p class="note">Maximum four. Stop code is the number on the pole, letters and
  digits only. Leave the route blank to show every route at that stop. The
  toward stop is one further along in the direction you care about &mdash; it
  is not a compass direction.</p>
</fieldset>

<button type="button" class="primary" id="save">Save and restart</button>
<span id="status"></span>

<script>
const $ = s => document.querySelector(s);
let watches = [];
let checking = false;  // true while a /api/stop request is in flight
let nextWatchId = 1;  // stable per-row id, since array indexes shift on removal

// The board serves both the departure fetcher and this portal from one heap,
// and a stop check opens its own TLS session to Auckland Transport. Two of
// those at once measurably starves the fetcher (occasional 502s and a
// backoff on hardware). The fix lives here, not in the firmware: never let
// more than one check be in flight, so every Check button is disabled while
// any check runs and re-enabled once it settles.
function setChecking(v) {
  checking = v;
  document.querySelectorAll('[data-a=check]').forEach(b => b.disabled = v);
}

function render() {
  $('#watches').innerHTML = '';
  watches.forEach((w, i) => {
    const d = document.createElement('div');
    d.className = 'row';
    d.dataset.id = w.id;
    d.style.cssText = 'border-top:1px solid #8883;padding:10px 0;align-items:flex-end';
    d.innerHTML =
      '<div><label>Label</label><input type="text" maxlength="31" data-k="label"></div>' +
      '<div><label>Stop code</label><input type="text" maxlength="31" data-k="stop_code"></div>' +
      '<div><label>Route</label><input type="text" maxlength="31" data-k="route_short_name"></div>' +
      '<div><label>Toward stop</label><input type="text" maxlength="31" data-k="toward_stop_code"></div>' +
      '<div style="flex:0 0 auto"><label><input type="checkbox" data-k="enabled"> On</label></div>' +
      '<div style="flex:0 0 auto"><button type="button" data-a="check">Check</button> ' +
      '<button type="button" data-a="del">Remove</button></div>' +
      '<div style="flex:1 1 100%" class="note" data-r=""></div>';
    d.querySelectorAll('[data-k]').forEach(el => {
      const k = el.dataset.k;
      if (k === 'enabled') { el.checked = w.enabled; el.onchange = () => w.enabled = el.checked; }
      else { el.value = w[k] || ''; el.oninput = () => w[k] = el.value; }
    });
    d.querySelector('[data-a=del]').onclick = () => { watches.splice(i, 1); render(); };
    d.querySelector('[data-a=check]').onclick = () => check(w.id);
    $('#watches').appendChild(d);
  });
  $('#add').disabled = watches.length >= 4;
  // A fresh render() rebuilds every Check button, so re-apply the in-flight
  // disable in case a check is still running when watches re-render.
  if (checking) document.querySelectorAll('[data-a=check]').forEach(b => b.disabled = true);
}

// Looks the result <div> up by watch id at the moment there is something to
// show, instead of holding a reference captured at click time: add/remove
// re-renders #watches while a check is in flight (the button stays disabled
// meanwhile, but the row list itself can still change), which would detach
// a captured node and swallow the answer silently. If the row is gone by
// the time this runs, there is genuinely nothing to report.
function showCheckResult(id, text, cls) {
  const out = document.querySelector('[data-id="' + id + '"] [data-r]');
  if (!out) return;
  out.textContent = text;
  out.className = cls ? 'note ' + cls : 'note';
}

async function check(id) {
  if (checking) return;  // one /api/stop at a time, board-wide
  const w = watches.find(x => x.id === id);
  if (!w) return;
  setChecking(true);
  showCheckResult(id, 'checking…', '');
  try {
    const r = await fetch('/api/stop', {
      method: 'POST', body: JSON.stringify({stop_code: w.stop_code})
    });
    const j = await r.json();
    showCheckResult(id, r.ok ? j.stop_name : j.error, r.ok ? 'ok' : 'bad');
  } catch (e) { showCheckResult(id, 'the board did not answer', 'bad'); }
  setChecking(false);
}

async function load() {
  const cfg = await (await fetch('/api/config')).json();
  $('#loc').value = cfg.location;
  watches = cfg.watches.map(w => Object.assign({id: nextWatchId++}, w));
  render();
  const t = await (await fetch('/api/themes')).json();
  const sel = $('#theme');
  sel.innerHTML = '';
  // Built with createElement/textContent, not innerHTML string concatenation,
  // so a theme name can never be parsed as markup - the one sink in this page
  // that used to differ from every other data path here.
  t.themes.forEach((n, i) => {
    const opt = document.createElement('option');
    opt.value = i;
    opt.textContent = n;
    sel.appendChild(opt);
  });
  sel.value = t.selected;
}

$('#add').onclick = () => {
  if (watches.length >= 4) return;
  watches.push({id: nextWatchId++, label: '', stop_code: '', route_short_name: '',
                toward_stop_code: '', enabled: true});
  render();
};

$('#theme').onchange = async e => {
  await fetch('/api/theme', {
    method: 'POST', body: JSON.stringify({theme: Number(e.target.value)})
  });
};

$('#save').onclick = async () => {
  const s = $('#status');
  s.textContent = 'saving…';
  s.className = '';
  const body = JSON.stringify({
    v: 1, location: $('#loc').value, theme: Number($('#theme').value),
    // Drop the client-only "id" used to track rows across re-renders; the
    // board's schema only knows the four watch fields below.
    watches: watches.map(w => ({
      label: w.label, stop_code: w.stop_code,
      route_short_name: w.route_short_name, toward_stop_code: w.toward_stop_code,
      enabled: w.enabled
    }))
  });
  let r, j;
  try { r = await fetch('/api/config', {method: 'POST', body: body}); j = await r.json(); }
  catch (e) { s.textContent = 'the board did not answer'; s.className = 'bad'; return; }
  if (!r.ok) { s.textContent = j.error; s.className = 'bad'; return; }
  s.textContent = 'restarting…';
  const wait = setInterval(async () => {
    try {
      await fetch('/api/config', {cache: 'no-store'});
      clearInterval(wait);
      s.textContent = 'back up';
      s.className = 'ok';
      load();
    } catch (e) { /* still down - keep waiting */ }
  }, 1000);
};

load();
</script>
</body>
</html>)HTML";
