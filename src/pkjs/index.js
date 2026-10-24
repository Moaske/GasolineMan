var NUM_DAYS = 30;
var PRICE_SCALE = 1000; // price stored on the watch as euros * 1000
var MAX_RECENT = 10;    // how many recently used sensor IDs the settings page remembers

// ---- settings ----

function loadSettings() {
  return {
    haUrl: (localStorage.getItem('haUrl') || '').replace(/\/+$/, ''),
    haToken: localStorage.getItem('haToken') || '',
    sensor1: localStorage.getItem('sensor1') || '',
    sensor2: localStorage.getItem('sensor2') || '',
    sensor3: localStorage.getItem('sensor3') || '',
    label1: localStorage.getItem('label1') || 'Euro95 - CBS-NL',
    label2: localStorage.getItem('label2') || 'Euro95 - VanRaak',
    label3: localStorage.getItem('label3') || 'Super+ - VanRaak',
    color1: localStorage.getItem('color1') || '#000055',
    color2: localStorage.getItem('color2') || '#005500',
    color3: localStorage.getItem('color3') || '#550000'
  };
}

function saveSettings(cfg) {
  localStorage.setItem('haUrl', cfg.haUrl || '');
  localStorage.setItem('haToken', cfg.haToken || '');
  localStorage.setItem('sensor1', cfg.sensor1 || '');
  localStorage.setItem('sensor2', cfg.sensor2 || '');
  localStorage.setItem('sensor3', cfg.sensor3 || '');
  localStorage.setItem('label1', cfg.label1 || '');
  localStorage.setItem('label2', cfg.label2 || '');
  localStorage.setItem('label3', cfg.label3 || '');
  localStorage.setItem('color1', cfg.color1 || '');
  localStorage.setItem('color2', cfg.color2 || '');
  localStorage.setItem('color3', cfg.color3 || '');
}

// ---- recently used sensor IDs ----
// One shared list for all three rows (most recent first), so a sensor you used
// in one row can be picked again in any other row.

function loadRecents() {
  try {
    var list = JSON.parse(localStorage.getItem('recentSensors') || '[]');
    return Array.isArray(list) ? list : [];
  } catch (e) {
    return [];
  }
}

function rememberSensors(ids) {
  var list = loadRecents();
  ids.forEach(function (id) {
    if (!id) return;
    list = list.filter(function (x) { return x !== id; });
    list.unshift(id);
  });
  localStorage.setItem('recentSensors', JSON.stringify(list.slice(0, MAX_RECENT)));
}

// Packs a "#RRGGBB" colour into Pebble's 8-bit ARGB colour byte
// (2 bits alpha, always opaque here, + 2 bits each for R/G/B).
function hexToPebbleColorByte(hex) {
  hex = (hex || '').replace('#', '');
  if (hex.length !== 6) hex = '000000';
  var r = parseInt(hex.substring(0, 2), 16);
  var g = parseInt(hex.substring(2, 4), 16);
  var b = parseInt(hex.substring(4, 6), 16);
  function q(v) { return v < 64 ? 0 : v < 128 ? 1 : v < 192 ? 2 : 3; }
  return 0xC0 | (q(r) << 4) | (q(g) << 2) | q(b); // 0xC0 = opaque alpha bits
}

// ---- day-bucket helpers ----

function startOfDay(d) {
  return new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime();
}

// HA has returned the stat bucket's "start" as either an ISO string or an
// epoch number (seconds in some versions, ms in others) depending on version -
// handle all three.
function parseStatTime(raw) {
  if (typeof raw === 'number') {
    return new Date(raw < 1e12 ? raw * 1000 : raw);
  }
  return new Date(raw);
}

// ---- long-term statistics over WebSocket (same approach as BP Diary) ----

// Fetches one €-price-per-day array (0 = no data for that day) for each of the
// given entity IDs in a single WebSocket round trip, using HA's long-term
// statistics tables rather than the short-retention raw states table.
function fetchStatsHistory(cfg, entityIds, callback) {
  var results = {};
  entityIds.forEach(function (id) {
    if (id) results[id] = new Array(NUM_DAYS).fill(0);
  });

  var validIds = entityIds.filter(function (id) { return !!id; });
  if (!cfg.haUrl || !cfg.haToken || validIds.length === 0) {
    callback(results);
    return;
  }

  var wsUrl = cfg.haUrl.replace(/^http/, 'ws') + '/api/websocket';
  var ws;
  var settled = false;
  var msgId = 1;

  var timeout = setTimeout(function () {
    if (settled) return;
    settled = true;
    console.log('HA websocket timed out');
    try { ws.close(); } catch (e) {}
    callback(results);
  }, 15000);

  function finish() {
    if (settled) return;
    settled = true;
    clearTimeout(timeout);
    try { ws.close(); } catch (e) {}
    callback(results);
  }

  try {
    ws = new WebSocket(wsUrl);
  } catch (e) {
    console.log('Could not open HA websocket: ' + e);
    finish();
    return;
  }

  ws.onerror = function () {
    console.log('HA websocket error');
    finish();
  };

  ws.onclose = function () {
    finish();
  };

  ws.onmessage = function (evt) {
    var msg;
    try {
      msg = JSON.parse(evt.data);
    } catch (e) {
      return;
    }

    if (msg.type === 'auth_required') {
      ws.send(JSON.stringify({ type: 'auth', access_token: cfg.haToken }));
      return;
    }

    if (msg.type === 'auth_invalid') {
      console.log('HA websocket auth failed - check the Long-Lived Access Token');
      finish();
      return;
    }

    if (msg.type === 'auth_ok') {
      var end = new Date();
      var start = new Date(startOfDay(end) - (NUM_DAYS - 1) * 24 * 60 * 60 * 1000);

      ws.send(JSON.stringify({
        id: msgId,
        type: 'recorder/statistics_during_period',
        start_time: start.toISOString(),
        end_time: end.toISOString(),
        statistic_ids: validIds,
        period: 'day',
        types: ['mean']
      }));
      return;
    }

    if (msg.type === 'result' && msg.id === msgId) {
      if (!msg.success) {
        console.log('HA statistics request failed: ' + JSON.stringify(msg.error));
        finish();
        return;
      }

      var today0 = startOfDay(new Date());

      validIds.forEach(function (id) {
        var points = (msg.result && msg.result[id]) ? msg.result[id] : [];
        if (points.length === 0) {
          console.log('No long-term statistics for "' + id + '" - it likely needs ' +
            '"state_class: measurement" set so HA starts building history for it.');
          return;
        }
        points.forEach(function (p) {
          if (typeof p.mean !== 'number') return;
          var dayStart = startOfDay(parseStatTime(p.start));
          var dayIndex = Math.round((dayStart - today0) / (24 * 60 * 60 * 1000)) + (NUM_DAYS - 1);
          if (dayIndex >= 0 && dayIndex < NUM_DAYS) {
            results[id][dayIndex] = Math.round(p.mean * PRICE_SCALE);
          }
        });
      });

      finish();
    }
  };
}

function toByteArray(dailyPrices) {
  // little-endian uint16 per day
  var bytes = [];
  for (var i = 0; i < dailyPrices.length; i++) {
    var v = dailyPrices[i] & 0xFFFF;
    bytes.push(v & 0xFF);
    bytes.push((v >> 8) & 0xFF);
  }
  return bytes;
}

function sendToWatch(hist1, hist2, hist3, cfg) {
  var payload = {
    'HIST1': toByteArray(hist1),
    'HIST2': toByteArray(hist2),
    'HIST3': toByteArray(hist3),
    'LABEL1': cfg.label1,
    'LABEL2': cfg.label2,
    'LABEL3': cfg.label3,
    'COLOR1': hexToPebbleColorByte(cfg.color1),
    'COLOR2': hexToPebbleColorByte(cfg.color2),
    'COLOR3': hexToPebbleColorByte(cfg.color3)
  };
  Pebble.sendAppMessage(payload, function () {
    console.log('Sent price history to watch');
  }, function (e) {
    console.log('Failed to send to watch: ' + JSON.stringify(e));
  });
}

var refreshInFlight = false;

function refreshAndSend() {
  if (refreshInFlight) {
    console.log('Refresh already in progress, ignoring extra request');
    return;
  }
  refreshInFlight = true;

  var cfg = loadSettings();
  var ids = [cfg.sensor1, cfg.sensor2, cfg.sensor3];

  fetchStatsHistory(cfg, ids, function (results) {
    var h1 = results[cfg.sensor1] || new Array(NUM_DAYS).fill(0);
    var h2 = results[cfg.sensor2] || new Array(NUM_DAYS).fill(0);
    var h3 = results[cfg.sensor3] || new Array(NUM_DAYS).fill(0);
    sendToWatch(h1, h2, h3, cfg);
    refreshInFlight = false;
  });
}

// ---- Pebble events ----

Pebble.addEventListener('ready', function () {
  console.log('GasolineMan pkjs ready');
  refreshAndSend();
});

Pebble.addEventListener('appmessage', function (e) {
  // Any inbound message from the watch is treated as a refresh request
  // (the watch only ever sends the REFRESH key on the SELECT button).
  refreshAndSend();
});

Pebble.addEventListener('showConfiguration', function () {
  var cfg = loadSettings();
  // Make sure the sensors currently in use are in the recent list, so they can
  // be picked again after switching away from them.
  rememberSensors([cfg.sensor3, cfg.sensor2, cfg.sensor1]);
  var url = 'data:text/html;charset=utf-8,' + encodeURIComponent(buildConfigHtml(cfg, loadRecents()));
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e.response) {
    return;
  }
  try {
    var cfg = JSON.parse(decodeURIComponent(e.response));
    saveSettings(cfg);
    rememberSensors([cfg.sensor3, cfg.sensor2, cfg.sensor1]);
    if (cfg.clearRecents) {
      // "Clear list" was pressed: keep only the sensors now in use.
      localStorage.setItem('recentSensors', '[]');
      rememberSensors([cfg.sensor3, cfg.sensor2, cfg.sensor1]);
    }
    refreshAndSend();
  } catch (err) {
    console.log('Could not parse config response: ' + err);
  }
});

// ---- config page (self-contained, no hosting required) ----

function buildConfigHtml(cfg, recents) {
  var recentOptions = recents.map(function (id) {
    return '<option value="' + escapeAttr(id) + '">' + escapeAttr(id) + '</option>';
  }).join('');

  function row(n, cfg) {
    return (
      '<div class="row">' +
      '<h3>Row ' + n + '</h3>' +
      '<label>Sensor entity ID</label>' +
      '<input id="sensor' + n + '" placeholder="sensor.example_price" value="' + escapeAttr(cfg['sensor' + n]) + '">' +
      '<select id="recent' + n + '" onchange="pick(' + n + ')">' +
      '<option value="">Recently used sensors...</option>' + recentOptions +
      '</select>' +
      '<label>Label text</label>' +
      '<input id="label' + n + '" maxlength="23" value="' + escapeAttr(cfg['label' + n]) + '">' +
      '<label>Banner colour</label>' +
      '<input id="color' + n + '" type="color" value="' + escapeAttr(cfg['color' + n]) + '">' +
      '</div>'
    );
  }

  return '<!DOCTYPE html><html><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width, initial-scale=1">' +
    '<title>GasolineMan settings</title>' +
    '<style>' +
    'body{font-family:sans-serif;margin:0;padding:16px;background:#f4f4f4;}' +
    'h2{font-size:18px;margin-bottom:4px;}' +
    'p.hint{color:#666;font-size:12px;margin-top:0;}' +
    '.row{background:#fff;border-radius:6px;padding:12px 14px;margin-top:14px;' +
    'border:1px solid #ddd;}' +
    '.row h3{margin:0 0 4px 0;font-size:14px;}' +
    'label{display:block;margin-top:10px;font-weight:bold;font-size:13px;}' +
    'input[type=text],input:not([type]){width:100%;box-sizing:border-box;padding:8px;margin-top:4px;' +
    'border:1px solid #ccc;border-radius:4px;font-size:14px;}' +
    'input[type=color]{width:100%;height:36px;margin-top:4px;padding:2px;' +
    'border:1px solid #ccc;border-radius:4px;}' +
    'select{width:100%;box-sizing:border-box;padding:7px;margin-top:6px;' +
    'border:1px solid #ccc;border-radius:4px;font-size:13px;background:#fafafa;color:#444;}' +
    'button{margin-top:22px;width:100%;padding:12px;background:#0a5;color:#fff;' +
    'border:none;border-radius:4px;font-size:16px;}' +
    'button.secondary{margin-top:10px;background:#888;font-size:13px;padding:8px;}' +
    '</style></head><body>' +
    '<h2>GasolineMan settings</h2>' +
    '<p class="hint">Home Assistant connection, and each row\'s sensor, label and colour.</p>' +

    '<label>Home Assistant URL</label>' +
    '<input id="haUrl" placeholder="https://your-ha-instance" value="' + escapeAttr(cfg.haUrl) + '">' +

    '<label>Long-Lived Access Token</label>' +
    '<input id="haToken" placeholder="token" value="' + escapeAttr(cfg.haToken) + '">' +

    row(1, cfg) + row(2, cfg) + row(3, cfg) +

    '<button onclick="save(false)">Save</button>' +
    '<button class="secondary" onclick="save(true)">Save and clear recent sensor list</button>' +

    '<script>' +
    'function pick(n){' +
    'var sel=document.getElementById("recent"+n);' +
    'if(sel.value){document.getElementById("sensor"+n).value=sel.value;}' +
    'sel.selectedIndex=0;' +
    '}' +
    'function save(clearRecents){' +
    'var cfg={' +
    'haUrl:document.getElementById("haUrl").value.trim(),' +
    'haToken:document.getElementById("haToken").value.trim(),' +
    'sensor1:document.getElementById("sensor1").value.trim(),' +
    'sensor2:document.getElementById("sensor2").value.trim(),' +
    'sensor3:document.getElementById("sensor3").value.trim(),' +
    'label1:document.getElementById("label1").value.trim(),' +
    'label2:document.getElementById("label2").value.trim(),' +
    'label3:document.getElementById("label3").value.trim(),' +
    'color1:document.getElementById("color1").value,' +
    'color2:document.getElementById("color2").value,' +
    'color3:document.getElementById("color3").value,' +
    'clearRecents:clearRecents' +
    '};' +
    'document.location = "pebblejs://close#" + encodeURIComponent(JSON.stringify(cfg));' +
    '}' +
    '</script>' +
    '</body></html>';
}

function escapeAttr(s) {
  return String(s || '').replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
}