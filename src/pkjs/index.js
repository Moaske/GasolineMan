var NUM_DAYS = 30;
var PRICE_SCALE = 1000; // price stored on the watch as euros * 1000

// ---- settings ----

function loadSettings() {
  return {
    haUrl: (localStorage.getItem('haUrl') || '').replace(/\/+$/, ''),
    haToken: localStorage.getItem('haToken') || '',
    sensor1: localStorage.getItem('sensor1') || '', // Euro95 - CBS-NL
    sensor2: localStorage.getItem('sensor2') || '', // Euro95 - VanRaak
    sensor3: localStorage.getItem('sensor3') || ''  // Super+ - VanRaak
  };
}

function saveSettings(cfg) {
  localStorage.setItem('haUrl', cfg.haUrl || '');
  localStorage.setItem('haToken', cfg.haToken || '');
  localStorage.setItem('sensor1', cfg.sensor1 || '');
  localStorage.setItem('sensor2', cfg.sensor2 || '');
  localStorage.setItem('sensor3', cfg.sensor3 || '');
}

// ---- HA history fetch ----

// Fetches HA history for one entity over the last NUM_DAYS days and reduces it
// to one price-per-day array (most recent known state on/ before each day,
// 0 if nothing was recorded yet for that day).
function fetchDailyHistory(cfg, entityId, callback) {
  if (!cfg.haUrl || !cfg.haToken || !entityId) {
    callback(new Array(NUM_DAYS).fill(0));
    return;
  }

  var end = new Date();
  var start = new Date(end.getTime() - NUM_DAYS * 24 * 60 * 60 * 1000);

  var url = cfg.haUrl + '/api/history/period/' + start.toISOString() +
    '?filter_entity_id=' + encodeURIComponent(entityId) +
    '&end_time=' + encodeURIComponent(end.toISOString()) +
    '&minimal_response';

  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.setRequestHeader('Authorization', 'Bearer ' + cfg.haToken);
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.timeout = 15000;

  xhr.onload = function () {
    var daily = new Array(NUM_DAYS).fill(0);
    try {
      if (xhr.status >= 200 && xhr.status < 300) {
        var data = JSON.parse(xhr.responseText);
        var series = (data && data[0]) ? data[0] : [];
        if (series.length === 0) {
          console.log('No history returned for "' + entityId + '" - check the entity ID ' +
            'is exactly right (Developer Tools > States in HA) and that it has recorder history.');
        }

        // Build day-bucket boundaries: dayStart[i] = midnight, i days before today (i = NUM_DAYS-1 .. 0)
        var todayMidnight = new Date(end.getFullYear(), end.getMonth(), end.getDate());
        var lastKnown = 0;
        var seriesIdx = 0;

        for (var i = 0; i < NUM_DAYS; i++) {
          var dayEnd = new Date(todayMidnight.getTime() - (NUM_DAYS - 1 - i) * 24 * 60 * 60 * 1000 + 24 * 60 * 60 * 1000 - 1);

          while (seriesIdx < series.length) {
            var point = series[seriesIdx];
            var t = new Date(point.last_changed || point.last_updated);
            if (t.getTime() <= dayEnd.getTime()) {
              var v = parseFloat(point.state);
              if (!isNaN(v)) {
                lastKnown = Math.round(v * PRICE_SCALE);
              }
              seriesIdx++;
            } else {
              break;
            }
          }
          daily[i] = lastKnown;
        }
      }
    } catch (e) {
      console.log('History parse error for ' + entityId + ': ' + e);
    }
    callback(daily);
  };

  xhr.onerror = function () {
    console.log('History request failed for ' + entityId);
    callback(new Array(NUM_DAYS).fill(0));
  };
  xhr.ontimeout = xhr.onerror;

  xhr.send();
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

function sendToWatch(hist1, hist2, hist3) {
  var payload = {
    'HIST1': toByteArray(hist1),
    'HIST2': toByteArray(hist2),
    'HIST3': toByteArray(hist3)
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
  fetchDailyHistory(cfg, cfg.sensor1, function (h1) {
    fetchDailyHistory(cfg, cfg.sensor2, function (h2) {
      fetchDailyHistory(cfg, cfg.sensor3, function (h3) {
        sendToWatch(h1, h2, h3);
        refreshInFlight = false;
      });
    });
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
  var url = 'data:text/html;charset=utf-8,' + encodeURIComponent(buildConfigHtml(cfg));
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e.response) {
    return;
  }
  try {
    var cfg = JSON.parse(decodeURIComponent(e.response));
    saveSettings(cfg);
    refreshAndSend();
  } catch (err) {
    console.log('Could not parse config response: ' + err);
  }
});

// ---- config page (self-contained, no hosting required) ----

function buildConfigHtml(cfg) {
  return '<!DOCTYPE html><html><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width, initial-scale=1">' +
    '<title>GasolineMan settings</title>' +
    '<style>' +
    'body{font-family:sans-serif;margin:0;padding:16px;background:#f4f4f4;}' +
    'h2{font-size:18px;margin-bottom:4px;}' +
    'p.hint{color:#666;font-size:12px;margin-top:0;}' +
    'label{display:block;margin-top:14px;font-weight:bold;font-size:13px;}' +
    'input{width:100%;box-sizing:border-box;padding:8px;margin-top:4px;' +
    'border:1px solid #ccc;border-radius:4px;font-size:14px;}' +
    '.swatch{display:inline-block;width:12px;height:12px;border-radius:2px;margin-right:6px;vertical-align:middle;}' +
    'button{margin-top:22px;width:100%;padding:12px;background:#0a5;color:#fff;' +
    'border:none;border-radius:4px;font-size:16px;}' +
    '</style></head><body>' +
    '<h2>GasolineMan settings</h2>' +
    '<p class="hint">Home Assistant connection and the three fuel price sensors.</p>' +

    '<label>Home Assistant URL</label>' +
    '<input id="haUrl" placeholder="https://your-ha-instance" value="' + escapeAttr(cfg.haUrl) + '">' +

    '<label>Long-Lived Access Token</label>' +
    '<input id="haToken" placeholder="token" value="' + escapeAttr(cfg.haToken) + '">' +

    '<label><span class="swatch" style="background:#000055"></span>Euro95 - CBS-NL sensor entity ID</label>' +
    '<input id="sensor1" placeholder="sensor.euro95_cbs_nl" value="' + escapeAttr(cfg.sensor1) + '">' +

    '<label><span class="swatch" style="background:#005500"></span>Euro95 - VanRaak sensor entity ID</label>' +
    '<input id="sensor2" placeholder="sensor.euro95_van_raak" value="' + escapeAttr(cfg.sensor2) + '">' +

    '<label><span class="swatch" style="background:#550000"></span>Super+ - VanRaak sensor entity ID</label>' +
    '<input id="sensor3" placeholder="sensor.super_plus_van_raak" value="' + escapeAttr(cfg.sensor3) + '">' +

    '<button onclick="save()">Save</button>' +

    '<script>' +
    'function save(){' +
    'var cfg={' +
    'haUrl:document.getElementById("haUrl").value.trim(),' +
    'haToken:document.getElementById("haToken").value.trim(),' +
    'sensor1:document.getElementById("sensor1").value.trim(),' +
    'sensor2:document.getElementById("sensor2").value.trim(),' +
    'sensor3:document.getElementById("sensor3").value.trim()' +
    '};' +
    'document.location = "pebblejs://close#" + encodeURIComponent(JSON.stringify(cfg));' +
    '}' +
    '</script>' +
    '</body></html>';
}

function escapeAttr(s) {
  return String(s || '').replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
}
