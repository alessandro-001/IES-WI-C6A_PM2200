#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "config.h"
#include "secrets.h"
#include "wifi_config.h"
#include "provisioning.h"
#include "local_mqtt.h"
#include "rs485_sensor.h"
#include "pm2200.h"
#include <math.h>

// ── Log buffer ────────────────────────────────────────────────────────────────
#define LOG_BUF_SIZE 40
static String _logBuf[LOG_BUF_SIZE];
static uint32_t _logHead = 0;

void logPush(const String& line) {
    _logBuf[_logHead % LOG_BUF_SIZE] = line;
    _logHead++;
}

extern bool registerDevice();
extern bool deviceIsCommissioned();
extern void setCommissionedPublic();

WebServer server(80);

//* Wi-Fi Configuration Implementation + Web Server Endpoints + UI

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>BOSS FARM — Power Meter Setup</title>
  <style>
    :root {
      --bg:      #f9f7f3;
      --surface: #ffffff;
      --border:  #e8e4dc;
      --accent:  #2d5d3f;
      --green:   #2d5d3f;
      --yellow:  #d4a137;
      --red:     #c94c4c;
      --muted:   #8b8680;
      --text:    #3d3a36;
      --mono:    'Courier New', 'Lucida Console', monospace;
      --sans:    'Trebuchet MS', 'Segoe UI', sans-serif;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    html, body { width: 100%; min-height: 100%; }
    body {
      font-family: var(--sans);
      background: var(--bg);
      color: var(--text);
      width: 100vw;
      margin: 0 auto;
      padding: 24px 32px 40px;
      position: relative;
    }
    body::before {
      content: '';
      position: fixed; inset: 0;
      background-image:
        linear-gradient(rgba(45,93,63,0.02) 1px, transparent 1px),
        linear-gradient(90deg, rgba(45,93,63,0.02) 1px, transparent 1px);
      background-size: 40px 40px;
      pointer-events: none; z-index: 0;
    }
    .page { position: relative; z-index: 1; max-width: 1500px; margin: 0 auto; }
    .page-header { border-bottom: 2px solid var(--accent); padding-bottom: 24px; margin-bottom: 32px; }
    .page-header .label { font-family: var(--mono); font-size: 0.65rem; color: var(--accent); letter-spacing: 0.2em; text-transform: uppercase; margin-bottom: 8px; }
    .page-header h1 { font-size: 2.2rem; font-weight: 700; letter-spacing: -0.02em; color: var(--text); }
    .page-header h1 span { color: var(--accent); }
    
    /* Layout Grid */
    .row-layout { display: grid; gap: 20px; margin-bottom: 20px; }
    .row-2col { grid-template-columns: 1fr 1fr; }
    .row-1col { grid-template-columns: 1fr; }
    
    .card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; padding: 24px; transition: all 0.2s; box-shadow: 0 1px 3px rgba(0,0,0,0.05); }
    .card:hover { border-color: var(--accent); box-shadow: 0 2px 8px rgba(45,93,63,0.08); }
    .card-header { display: flex; align-items: center; gap: 12px; margin-bottom: 18px; padding-bottom: 12px; border-bottom: 1px solid var(--border); }
    .card-icon { font-size: 1.4rem; line-height: 1; }
    .card-title { font-size: 0.85rem; font-weight: 700; letter-spacing: 0.1em; text-transform: uppercase; color: var(--accent); font-family: var(--mono); }
    .row { display: flex; justify-content: space-between; align-items: center; padding: 10px 0; border-bottom: 1px solid var(--border); font-size: 0.9rem; }
    .row:last-child { border-bottom: none; }
    .row-label { color: var(--muted); font-size: 0.85rem; font-weight: 500; }
    .val { font-family: var(--mono); font-size: 1rem; font-weight: 600; color: var(--accent); word-break: break-all; }
    .val.small { font-size: 0.82rem; }
    
    /* Power meter: tiles, tables, charts, settings form */
    .tile-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 12px; margin-bottom: 20px; }
    .tile { background: var(--bg); border: 1px solid var(--border); border-radius: 10px; padding: 16px 12px; text-align: center; }
    .tile-label { font-family: var(--mono); font-size: 0.62rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.08em; margin-bottom: 8px; font-weight: 600; }
    .tile-val { font-family: var(--mono); font-size: 1.35rem; font-weight: 700; color: var(--accent); }
    .tile-sub { font-family: var(--mono); font-size: 0.68rem; color: var(--muted); margin-top: 4px; }

    .table-wrap { overflow-x: auto; margin-bottom: 20px; }
    .mtable { width: 100%; border-collapse: collapse; font-family: var(--mono); font-size: 0.85rem; }
    .mtable th { font-size: 0.65rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.1em; text-align: right; padding: 8px 10px; border-bottom: 2px solid var(--border); white-space: nowrap; }
    .mtable td { text-align: right; padding: 9px 10px; border-bottom: 1px solid var(--border); font-weight: 600; color: var(--accent); white-space: nowrap; }
    .mtable th:first-child, .mtable td:first-child { text-align: left; }
    .mtable td:first-child { color: var(--text); font-weight: 500; }
    .mtable td.tot { background: rgba(45,93,63,0.05); }
    .mtable small { display: block; color: var(--muted); font-size: 0.65rem; font-weight: 400; }

    .chart-row { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
    .chart-title { font-family: var(--mono); font-size: 0.7rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.1em; margin: 0 0 6px 0; font-weight: 600; }
    .chart svg { width: 100%; height: auto; background: var(--bg); border: 1px solid var(--border); border-radius: 10px; display: block; }
    .chart .axis { font: 10px 'Courier New', monospace; fill: var(--muted); }
    .legend { font-family: var(--mono); font-size: 0.68rem; margin-top: 6px; display: flex; gap: 12px; flex-wrap: wrap; }

    .form-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 16px; }
    select { width: 100%; padding: 10px 12px; background: var(--bg); border: 1.5px solid var(--border); border-radius: 8px; color: var(--text); font-family: var(--mono); font-size: 0.95rem; outline: none; }
    select:focus { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(45,93,63,0.1); }

    /* Button Styles */
    button { padding: 12px 16px; margin-top: 12px; border: none; border-radius: 8px; font-family: var(--sans); font-size: 0.95rem; font-weight: 700; cursor: pointer; letter-spacing: 0.02em; transition: all 0.15s; }
    button:active { transform: scale(0.98); }
    button:disabled { opacity: 0.4; cursor: not-allowed; }
    button.btn-primary { background: var(--accent); color: #fff; box-shadow: 0 2px 6px rgba(45,93,63,0.2); width: 100%; }
    button.btn-primary:hover { background: #1f4a2d; box-shadow: 0 4px 12px rgba(45,93,63,0.3); }
    button.btn-wifi    { background: transparent; color: var(--accent); border: 1.5px solid var(--accent); width: 100%; }
    button.btn-wifi:hover { background: rgba(45,93,63,0.1); }
    button.btn-save    { background: rgba(45,93,63,0.1); color: var(--green); border: 1.5px solid var(--green); width: auto; }
    button.btn-save:hover { background: rgba(45,93,63,0.15); }
    button.btn-reset   { background: rgba(201,76,76,0.1);  color: var(--red);   border: 1.5px solid var(--red); width: auto; }
    button.btn-reset:hover { background: rgba(201,76,76,0.15); }
    
    label { display: block; font-family: var(--mono); font-size: 0.68rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.1em; margin: 0 0 6px 0; font-weight: 600; }
    input { width: 100%; padding: 10px 12px; background: var(--bg); border: 1.5px solid var(--border); border-radius: 8px; color: var(--text); font-family: var(--mono); font-size: 0.95rem; outline: none; transition: all 0.2s; }
    input:focus { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(45,93,63,0.1); }
    
    .net { padding: 10px 12px; margin: 5px 0; background: var(--bg); border: 1px solid var(--border); border-radius: 8px; cursor: pointer; font-family: var(--mono); font-size: 0.85rem; word-break: break-all; transition: all 0.15s; color: var(--text); }
    .net:hover { border-color: var(--accent); color: var(--accent); background: rgba(45,93,63,0.03); }
    .hidden { display: none !important; }
    
    .badge { font-family: var(--mono); font-size: 0.7rem; padding: 4px 12px; border-radius: 20px; border: 1px solid var(--border); }
    .badge.online  { background: rgba(45,93,63,0.15);  color: var(--green);  border-color: var(--green); }
    .badge.offline { background: rgba(201,76,76,0.15);  color: var(--red);    border-color: var(--red); }
    .badge.waiting { background: rgba(212,161,55,0.15); color: var(--yellow); border-color: var(--yellow); }
    
    #popup-msg { position: fixed; top: -60px; left: 0; width: 100%; z-index: 9999; text-align: center; padding: 15px 0; font-family: var(--mono); font-size: 0.9rem; font-weight: 600; transition: top 0.4s cubic-bezier(.4,2,.6,1); border-bottom: 2px solid var(--accent); background: var(--surface); color: var(--text); box-shadow: 0 4px 12px rgba(0,0,0,0.1); }
    #popup-msg.success { background: rgba(45,93,63,0.95); color: #fff; border-bottom-color: var(--green); }
    #popup-msg.error   { background: rgba(201,76,76,0.95); color: #fff;   border-bottom-color: var(--red); }
    #popup-msg.info    { background: rgba(45,93,63,0.95); color: #fff; border-bottom-color: var(--accent); }
    
    /* Responsive */
    @media (max-width: 1200px) {
      .row-2col { grid-template-columns: 1fr; }
    }
    @media (max-width: 768px) {
      body { padding: 20px 20px 40px; }
      .page-header h1 { font-size: 1.8rem; }
      .card { padding: 18px; }
      .tile-grid, .form-grid { grid-template-columns: repeat(2, 1fr); }
      .chart-row { grid-template-columns: 1fr; }
    }
    @media (max-width: 640px) {
      body { padding: 16px 12px 40px; }
      .page-header h1 { font-size: 1.5rem; }
      .page-header { padding-bottom: 16px; margin-bottom: 24px; }
      .form-grid { grid-template-columns: 1fr; }
      .row-2col { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
<div id="popup-msg"></div>
<div class="page">

  <div class="page-header">
    <div class="label">// device setup</div>
    <h1>BOSS FARM <span>POWER METER</span></h1>
  </div>

  <!-- Device Info & WiFi Connection (Side by Side) -->
  <div class="row-layout row-2col">
    <!-- Device Info Card -->
    <div class="card" id="step1">
      <div class="card-header">
        <span class="card-icon">🔌</span>
        <span class="card-title">Device Information</span>
      </div>
      <div class="row">
        <span class="row-label">Device ID</span>
        <span class="val small" id="device-id">–</span>
      </div>
      <div class="row">
        <span class="row-label">Firmware</span>
        <span class="val small" id="device-firmware">–</span>
      </div>
      <div class="row">
        <span class="row-label">IP Address</span>
        <span class="val" id="device-ip">–</span>
      </div>
      <div class="row">
        <span class="row-label">Hostname</span>
        <span class="val small" id="device-mdns">–</span>
      </div>
      <div class="row">
        <span class="row-label">Signal</span>
        <span class="val" id="device-rssi">–</span>
      </div>
      <div class="row">
        <span class="row-label">Network Status</span>
        <span id="device-status"><span class="badge waiting">Checking...</span></span>
      </div>
    </div>

    <!-- WiFi Connection Card -->
    <div class="card" id="step2">
      <div class="card-header">
        <span class="card-icon">📶</span>
        <span class="card-title">WiFi Connection</span>
      </div>
      <div class="row">
        <span class="row-label">Network</span>
        <span class="val" id="curr-wifi">–</span>
      </div>
      <div class="row">
        <span class="row-label">Status</span>
        <span id="conn-status" style="font-family:var(--mono);font-size:0.85rem;">–</span>
      </div>
      <div id="wifi-config">
        <button class="btn-wifi" style="margin-top:12px;" onclick="scanWifi()">🔍 Scan Networks</button>
        <div id="networks"></div>
        <div id="wifi-form" class="hidden">
          <label>Network</label>
          <input type="text" id="wifi-ssid" readonly>
          <label>Password</label>
          <input type="password" id="wifi-pass" placeholder="Enter password">
          <button class="btn-primary" onclick="saveWifi()">💾 Connect</button>
        </div>
      </div>
    </div>
  </div>

  <!-- Power Meter: live values -->
  <div class="card row-1col" id="step-meter">
    <div class="card-header">
      <span class="card-icon">⚡</span>
      <span class="card-title">Power Meter · PM2200</span>
      <span class="badge waiting" id="meter-mode" style="margin-left:auto;">–</span>
      <span class="badge waiting" id="meter-status">–</span>
    </div>
    <div class="tile-grid">
      <div class="tile">
        <div class="tile-label">Active power</div>
        <div class="tile-val" id="t-power">–</div>
        <div class="tile-sub" id="t-power-sub">–</div>
      </div>
      <div class="tile">
        <div class="tile-label">Energy delivered</div>
        <div class="tile-val" id="t-energy">–</div>
        <div class="tile-sub" id="t-energy-sub">–</div>
      </div>
      <div class="tile">
        <div class="tile-label">Power factor</div>
        <div class="tile-val" id="t-pf">–</div>
      </div>
      <div class="tile">
        <div class="tile-label">Frequency</div>
        <div class="tile-val" id="t-freq">–</div>
      </div>
    </div>
    <div class="table-wrap">
      <table class="mtable">
        <thead><tr><th>Instantaneous</th><th>L1</th><th>L2</th><th>L3</th><th>Total / Avg</th></tr></thead>
        <tbody id="meter-rows"></tbody>
      </table>
    </div>
    <div class="table-wrap">
      <table class="mtable">
        <thead><tr><th>Energy (cumulative)</th><th>Delivered</th><th>Received</th></tr></thead>
        <tbody id="energy-rows"></tbody>
      </table>
    </div>
    <div class="chart-row">
      <div class="chart">
        <div class="chart-title">Active power (kW)</div>
        <svg id="chart-power" viewBox="0 0 600 150"></svg>
        <div class="legend"><span style="color:#2d5d3f">■ Total</span><span style="color:#4a7fb5">■ L1</span><span style="color:#d4a137">■ L2</span><span style="color:#c94c4c">■ L3</span></div>
      </div>
      <div class="chart">
        <div class="chart-title">Current (A)</div>
        <svg id="chart-current" viewBox="0 0 600 150"></svg>
        <div class="legend"><span style="color:#4a7fb5">■ L1</span><span style="color:#d4a137">■ L2</span><span style="color:#c94c4c">■ L3</span></div>
      </div>
    </div>
    <div class="row" style="margin-top:16px;">
      <span class="row-label">Quality</span>
      <span class="val small" id="m-quality">–</span>
    </div>
    <div class="row">
      <span class="row-label">Last update</span>
      <span class="val small" id="m-updated">–</span>
    </div>
  </div>

  <!-- Power Meter: Modbus settings -->
  <div class="card row-1col" id="step-modbus">
    <div class="card-header">
      <span class="card-icon">🔧</span>
      <span class="card-title">Meter Settings · Modbus RTU</span>
    </div>
    <div class="form-grid">
      <div>
        <label>Slave address (1–247)</label>
        <input type="number" id="m-addr" min="1" max="247" step="1">
      </div>
      <div>
        <label>Baud rate</label>
        <select id="m-baud">
          <option value="4800">4800</option>
          <option value="9600">9600</option>
          <option value="19200">19200</option>
          <option value="38400">38400</option>
        </select>
      </div>
      <div>
        <label>Parity</label>
        <select id="m-parity">
          <option value="E">Even (1 stop bit)</option>
          <option value="O">Odd (1 stop bit)</option>
          <option value="N">None (2 stop bits)</option>
        </select>
      </div>
      <div>
        <label>Data source</label>
        <select id="m-sim">
          <option value="0">PM2200 meter (RS485)</option>
          <option value="1">Simulated (no meter)</option>
        </select>
      </div>
    </div>
    <button class="btn-save" onclick="saveMeter()">💾 Save Meter Settings</button>
    <div class="row" style="margin-top:12px;">
      <span class="row-label" style="color:var(--muted);font-size:0.8rem;">
        Match address, baud rate and parity to the meter's front panel (Comm setup). Simulated data is flagged as simulated when published.
      </span>
    </div>
  </div>

  <!-- Device Log Terminal -->
  <div class="card row-1col">
    <div class="card-header">
      <span class="card-icon">🖥</span>
      <span class="card-title">Device Log</span>
    </div>
    <div id="terminal" style="background:#f5f5f1;border:1px solid rgba(45,93,63,0.2);border-radius:8px;padding:14px;height:240px;overflow-y:auto;font-family:var(--mono);font-size:0.72rem;color:#2d5d3f;line-height:1.8;text-shadow:0 0 2px rgba(45,93,63,0.1);box-shadow:inset 0 0 10px rgba(45,93,63,0.03);"></div>
    <button class="btn-wifi" style="margin-top:8px;font-size:0.8rem;padding:8px 12px;width:auto;" onclick="document.getElementById('terminal').innerHTML='';lastLogSeq=-1;">🗑 Clear</button>
  </div>

  <!-- Factory Reset -->
  <div class="card row-1col" id="step-reset">
    <div class="card-header">
      <span class="card-icon">⚠️</span>
      <span class="card-title">Factory Reset</span>
    </div>
    <div class="row">
      <span class="row-label" style="color:var(--muted);font-size:0.8rem;">
        Clears all settings and WiFi credentials. Device will reboot and AP will reappear.
      </span>
    </div>
    <button class="btn-reset" style="width:auto;" onclick="factoryReset()">🗑 Factory Reset</button>
  </div>

</div><!-- /page -->

<!-- ============================================================================
     SCRIPT SECTION

     API endpoints called (all registered in webServerInit()):
        GET  /device_info  -> device_id, firmware, mdns, ip, rssi, commissioned
        GET  /wifi         -> ssid, connected
        GET  /logs         -> seq, lines[]
        GET  /scan         -> array of {ssid, rssi, secure}
        POST /set_wifi     -> accepts ssid, pass, secure; returns {ok, ip, mdns, msg}
        POST /factory_reset
     ============================================================================ -->

<script>
// ═══════════════════════════════════════════════════════════════════════════
// DEVICE CONTROL FUNCTIONS (existing logic - do not modify)
// ═══════════════════════════════════════════════════════════════════════════

let selectedSecure = false;

function scanWifi() {
  const nets = document.getElementById('networks');
  nets.innerHTML = 'Scanning...';
  fetch('/scan').then(r => r.json()).then(list => {
    nets.innerHTML = list.length
      ? list.map(n => `<div class='net' onclick='selectNet("${n.ssid.replace(/'/g,"\\'").replace(/"/g,'\\"')}",${n.secure})'>${n.secure?"🔒":"🔓"} ${n.ssid} (${n.rssi} dBm)</div>`).join('')
      : 'No networks found';
  }).catch(() => nets.innerHTML = 'Scan failed');
}

function selectNet(ssid, secure) {
  selectedSecure = secure;
  document.getElementById('wifi-ssid').value = ssid;
  document.getElementById('wifi-pass').value = '';
  document.getElementById('wifi-form').classList.remove('hidden');
  if (secure) document.getElementById('wifi-pass').focus();
}

function saveWifi() {
  const ssid = document.getElementById('wifi-ssid').value.trim();
  const pass = document.getElementById('wifi-pass').value;
  if (!ssid) { showMsg('SSID required', 'error'); return; }
  showMsg('Connecting...', 'info');
  fetch('/set_wifi', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass) + '&secure=' + (selectedSecure ? 'true' : 'false')
  })
  .then(r => r.json())
  .then(res => {
    if (res.ok) showProvisionedOverlay(res.ip, res.mdns);
    else showMsg(res.msg || 'Connection failed', 'error');
  })
  .catch(() => showMsg('Connection failed', 'error'));
}

function showProvisionedOverlay(ip, mdns) {
  const overlay = document.createElement('div');
  overlay.id = 'prov-overlay';
  overlay.style.cssText = 'position:fixed;inset:0;z-index:99999;background:rgba(249,247,243,0.97);display:flex;flex-direction:column;align-items:center;justify-content:center;padding:32px;text-align:center;';
  overlay.innerHTML = `
    <div style="font-size:3rem;margin-bottom:16px;">✅</div>
    <div style="font-family:var(--mono);font-size:0.7rem;color:var(--accent);letter-spacing:0.2em;text-transform:uppercase;margin-bottom:8px;">Device Provisioned</div>
    <div style="font-size:1.3rem;font-weight:700;margin-bottom:24px;">Successfully joined network</div>
    <div style="background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:20px;width:100%;max-width:320px;margin-bottom:24px;">
      <div style="display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid var(--border);">
        <span style="color:var(--muted);font-size:0.85rem;">IP Address</span>
        <span style="font-family:var(--mono);color:var(--accent);">${ip}</span>
      </div>
      <div style="display:flex;justify-content:space-between;align-items:center;padding:8px 0;">
        <span style="color:var(--muted);font-size:0.85rem;">Local hostname</span>
        <span style="font-family:var(--mono);color:var(--accent);font-size:0.85rem;">${mdns}</span>
      </div>
    </div>
    <a href="http://${ip}" target="_blank" style="display:block;width:100%;max-width:320px;background:var(--accent);color:#fff;font-weight:700;font-size:1rem;padding:14px;border-radius:8px;text-decoration:none;margin-bottom:12px;">Open Device UI →</a>
    <a href="http://${mdns}" target="_blank" style="display:block;width:100%;max-width:320px;background:transparent;color:var(--accent);font-weight:600;font-size:0.9rem;padding:12px;border-radius:8px;text-decoration:none;border:1px solid var(--accent);margin-bottom:20px;">Open via ${mdns} →</a>
    <p style="color:var(--muted);font-size:0.78rem;font-family:var(--mono);line-height:1.6;">Reconnect your phone to your WiFi network<br>to access the device UI.</p>
  `;
  document.body.appendChild(overlay);
}

function updateStatus() {
  fetch('/wifi').then(r => r.json()).then(w => {
    document.getElementById('curr-wifi').textContent = w.ssid || '–';
    document.getElementById('conn-status').textContent = w.connected ? '✓ Connected' : '✗ Not connected';
    document.getElementById('conn-status').style.color = w.connected ? '#2d5d3f' : '#c94c4c';
  }).catch(() => {});

  fetch('/device_info').then(r => r.json()).then(info => {
    document.getElementById('device-id').textContent       = info.device_id  || '–';
    document.getElementById('device-firmware').textContent = info.firmware    || '–';
    document.getElementById('device-mdns').textContent     = info.mdns        || '–';

    const ip = info.ip && info.ip !== '0.0.0.0' ? info.ip : null;
    const ipEl = document.getElementById('device-ip');
    ipEl.textContent = ip || '–';
    ipEl.style.color = ip ? 'var(--accent)' : 'var(--muted)';

    const rssi = info.rssi || 0;
    const rssiEl = document.getElementById('device-rssi');
    rssiEl.textContent = rssi ? rssi + ' dBm' : '–';
    rssiEl.style.color = rssi >= -60 ? '#2d5d3f' : rssi >= -75 ? '#d4a137' : '#c94c4c';

    const statusEl = document.getElementById('device-status');
    if (info.commissioned && ip) {
      statusEl.innerHTML = '<span class="badge online">● Online</span>';
    } else if (!info.commissioned) {
      statusEl.innerHTML = '<span class="badge waiting">Not commissioned</span>';
    } else {
      statusEl.innerHTML = '<span class="badge offline">● Offline</span>';
    }
  }).catch(() => {});
}

function showMsg(txt, type) {
  const el = document.getElementById('popup-msg');
  el.textContent = txt; el.className = type || ''; el.style.top = '0';
  clearTimeout(el._hideTimer);
  el._hideTimer = setTimeout(() => { el.style.top = '-60px'; el.className = ''; }, 5000);
}

function factoryReset() {
  if (!confirm('Reset this device? All settings and WiFi credentials will be cleared. The AP hotspot will reappear.')) return;
  showMsg('Resetting...', 'info');
  fetch('/factory_reset', { method: 'POST' })
    .then(() => showMsg('Device reset — reconnect to the AP hotspot', 'success'))
    .catch(() => showMsg('Reset sent — reconnect to the AP hotspot', 'success'));
}

let lastLogSeq = -1;
function pollLogs() {
  fetch('/logs').then(r => r.json()).then(data => {
    if (data.seq === lastLogSeq) return;
    lastLogSeq = data.seq;
    const t = document.getElementById('terminal');
    t.innerHTML = data.lines.map(l => '<div>' + l.replace(/</g,'&lt;').replace(/>/g,'&gt;') + '</div>').join('');
    t.scrollTop = t.scrollHeight;
  }).catch(() => {});
}

// ═══════════════════════════════════════════════════════════════════════════
// POWER METER (PM2200) — values come from GET /power, same keys as the MQTT payload
// ═══════════════════════════════════════════════════════════════════════════

// Table layout: label, payload keys for L1 / L2 / L3 / Total-Avg, decimals, divisor (W -> kW)
const ROWS = [
  { label: 'Voltage L-N (V)',       keys: ['v1', 'v2', 'v3', 'v_avg'],       d: 1 },
  { label: 'Voltage L-L (V)',       keys: ['v12', 'v23', 'v31', 'vll_avg'],  d: 1, sub: 'L1-L2 · L2-L3 · L3-L1' },
  { label: 'Current (A)',           keys: ['i1', 'i2', 'i3', 'i_avg'],       d: 2 },
  { label: 'Neutral current (A)',   keys: [null, null, null, 'i_n'],        d: 2 },
  { label: 'Active power (kW)',     keys: ['p1', 'p2', 'p3', 'p_total_w'],   d: 2, k: 1000 },
  { label: 'Reactive power (kvar)', keys: ['q1', 'q2', 'q3', 'q_total_var'], d: 2, k: 1000 },
  { label: 'Apparent power (kVA)',  keys: ['s1', 's2', 's3', 's_total_va'],  d: 2, k: 1000 },
  { label: 'Power factor',          keys: ['pf1', 'pf2', 'pf3', 'pf'],       d: 2 }
];
// Energy table: label, [delivered key, received key] (Wh -> kWh)
const ENERGY = [
  { label: 'Active (kWh)',     keys: ['energy_wh', 'energy_wh_recv'] },
  { label: 'Reactive (kvarh)', keys: ['energy_varh', 'energy_varh_recv'] },
  { label: 'Apparent (kVAh)',  keys: ['energy_vah', 'energy_vah_recv'] }
];
const HIST_N = 60;   // samples kept in the charts (one per meter poll)
const hist = { pt: [], p1: [], p2: [], p3: [], i1: [], i2: [], i3: [] };
let lastPoll = -1;

// A key the board does not send (never read / register address not known yet) shows as a dash.
function fmt(v, d, k) { return (v == null || isNaN(v)) ? '–' : (v / (k || 1)).toFixed(d); }
function withUnit(s, u) { return s === '–' ? s : s + ' ' + u; }
function setText(id, txt) { document.getElementById(id).textContent = txt; }
function setBadge(id, cls, txt) {
  const el = document.getElementById(id);
  el.className = 'badge ' + cls;
  el.textContent = txt;
}
function pushHist(arr, v) {
  if (v == null || isNaN(v)) return;
  arr.push(v);
  if (arr.length > HIST_N) arr.shift();
}

function drawChart(id, series, colors) {
  const W = 600, H = 150, P = 8;
  let lo = Infinity, hi = -Infinity;
  series.forEach(s => s.forEach(v => { if (v < lo) lo = v; if (v > hi) hi = v; }));
  if (!isFinite(lo)) return;
  if (hi - lo < 0.01) { lo -= 0.5; hi += 0.5; }
  const x = i => (i / (HIST_N - 1) * W).toFixed(1);
  const y = v => (H - P - (v - lo) / (hi - lo) * (H - 2 * P)).toFixed(1);
  document.getElementById(id).innerHTML =
    series.map((s, k) => s.length < 2 ? '' :
      '<polyline fill="none" stroke="' + colors[k] + '" stroke-width="' + (k === 0 && series.length > 3 ? 3 : 2) +
      '" stroke-linejoin="round" points="' + s.map((v, i) => x(i) + ',' + y(v)).join(' ') + '"/>').join('') +
    '<text class="axis" x="6" y="14">' + hi.toFixed(2) + '</text>' +
    '<text class="axis" x="6" y="' + (H - 6) + '">' + lo.toFixed(2) + '</text>';
}

function renderMeter(d) {
  setBadge('meter-mode', d.simulated ? 'waiting' : 'online', d.simulated ? 'SIMULATED' : 'PM2200 METER');
  setBadge('meter-status', d.meter_ok ? 'online' : 'offline', d.meter_ok ? '● Reading OK' : '● No response');

  setText('t-power', withUnit(fmt(d.p_total_w, 2, 1000), 'kW'));
  setText('t-power-sub', 'S ' + withUnit(fmt(d.s_total_va, 2, 1000), 'kVA') + ' · Q ' + withUnit(fmt(d.q_total_var, 2, 1000), 'kvar'));
  setText('t-energy', d.energy_wh == null ? '–' : Math.round(d.energy_wh).toLocaleString('en-US') + ' Wh');
  setText('t-energy-sub', withUnit(fmt(d.energy_wh, 2, 1000), 'kWh'));
  setText('t-pf', fmt(d.pf, 2));
  setText('t-freq', withUnit(fmt(d.freq_hz, 2), 'Hz'));

  document.getElementById('meter-rows').innerHTML = ROWS.map(r =>
    '<tr><td>' + r.label + (r.sub ? '<small>' + r.sub + '</small>' : '') + '</td>' +
    r.keys.map((key, i) => '<td' + (i === 3 ? ' class="tot"' : '') + '>' + (key ? fmt(d[key], r.d, r.k) : '') + '</td>').join('') +
    '</tr>').join('');
  document.getElementById('energy-rows').innerHTML = ENERGY.map(r =>
    '<tr><td>' + r.label + '</td>' + r.keys.map(key => '<td>' + fmt(d[key], 2, 1000) + '</td>').join('') + '</tr>').join('');

  setText('m-quality', 'Neutral ' + withUnit(fmt(d.i_n, 2), 'A') +
          ' · I unbalance ' + withUnit(fmt(d.i_unbal_pct, 1), '%') +
          ' · V unbalance L-L ' + withUnit(fmt(d.v_unbal_ll_pct, 1), '%') +
          ' / L-N ' + withUnit(fmt(d.v_unbal_ln_pct, 1), '%'));

  // One chart sample per meter poll (the board polls every 5 s, this page asks every 2 s)
  if (d.poll !== lastPoll) {
    lastPoll = d.poll;
    if (d.meter_ok && d.p_total_w != null) {
      pushHist(hist.pt, d.p_total_w / 1000);
      pushHist(hist.p1, d.p1 / 1000);
      pushHist(hist.p2, d.p2 / 1000);
      pushHist(hist.p3, d.p3 / 1000);
      pushHist(hist.i1, d.i1);
      pushHist(hist.i2, d.i2);
      pushHist(hist.i3, d.i3);
      drawChart('chart-power', [hist.pt, hist.p1, hist.p2, hist.p3], ['#2d5d3f', '#4a7fb5', '#d4a137', '#c94c4c']);
      drawChart('chart-current', [hist.i1, hist.i2, hist.i3], ['#4a7fb5', '#d4a137', '#c94c4c']);
      setText('m-updated', new Date().toLocaleTimeString());
    }
  }
}

function pollMeter() {
  fetch('/power').then(r => r.json()).then(renderMeter).catch(err => console.error('pollMeter error', err));
}

function loadMeter() {
  fetch('/meter').then(r => r.json()).then(m => {
    document.getElementById('m-addr').value   = m.addr;
    document.getElementById('m-baud').value   = m.baud;
    document.getElementById('m-parity').value = m.parity;
    document.getElementById('m-sim').value    = m.sim ? '1' : '0';
  }).catch(() => {});
}

function saveMeter() {
  showMsg('Saving meter settings...', 'info');
  fetch('/set_meter', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'addr=' + encodeURIComponent(document.getElementById('m-addr').value) +
          '&baud=' + document.getElementById('m-baud').value +
          '&parity=' + document.getElementById('m-parity').value +
          '&sim=' + document.getElementById('m-sim').value
  })
  .then(r => r.json())
  .then(res => {
    if (res.status === 'ok') {
      Object.keys(hist).forEach(k => { hist[k].length = 0; });   // new data source: start the charts over
      lastPoll = -1;
      showMsg('✓ Meter settings saved', 'success');
      loadMeter();
    } else {
      showMsg(res.message || 'Invalid settings', 'error');
    }
  })
  .catch(() => showMsg('Failed to save', 'error'));
}

// Initialize on page load
window.addEventListener('load', function() {
  updateStatus();
  loadMeter();
  pollMeter();
  pollLogs();
  
  // Set up polling intervals
  setInterval(pollMeter, 2000);
  setInterval(updateStatus, 5000);
  setInterval(pollLogs, 1000);
});
</script>
</body>
</html>
)rawliteral";

// ── Device Discovery Page ─────────────────────────────────────────────────────
const char DISCOVER_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>BOSS FARM — Device Discovery</title>
  <style>
    :root {
      --bg:      #f9f7f3;
      --surface: #ffffff;
      --border:  #e8e4dc;
      --accent:  #2d5d3f;
      --green:   #2d5d3f;
      --yellow:  #d4a137;
      --red:     #c94c4c;
      --muted:   #8b8680;
      --text:    #3d3a36;
      --mono:    'Courier New', 'Lucida Console', monospace;
      --sans:    'Trebuchet MS', 'Segoe UI', sans-serif;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { background: var(--bg); color: var(--text); font-family: var(--sans); min-height: 100vh; overflow-x: hidden; }
    body::before {
      content: '';
      position: fixed; inset: 0;
      background-image:
        linear-gradient(rgba(45,93,63,0.02) 1px, transparent 1px),
        linear-gradient(90deg, rgba(45,93,63,0.02) 1px, transparent 1px);
      background-size: 40px 40px;
      pointer-events: none; z-index: 0;
    }
    .wrap { position: relative; z-index: 1; max-width: 1200px; margin: 0 auto; padding: 40px 32px; }
    header { display: flex; align-items: flex-end; justify-content: space-between; border-bottom: 2px solid var(--accent); padding-bottom: 24px; margin-bottom: 36px; flex-wrap: wrap; gap: 16px; }
    .logo-block .label { font-family: var(--mono); font-size: 0.7rem; color: var(--accent); letter-spacing: 0.2em; text-transform: uppercase; margin-bottom: 6px; }
    .logo-block h1 { font-size: 2.2rem; font-weight: 700; letter-spacing: -0.02em; line-height: 1; color: var(--text); }
    .logo-block h1 span { color: var(--accent); }
    .status-pill { font-family: var(--mono); font-size: 0.75rem; padding: 8px 16px; border-radius: 20px; border: 1px solid var(--border); color: var(--muted); display: flex; align-items: center; gap: 8px; background: var(--surface); }
    .status-pill .dot { width: 7px; height: 7px; border-radius: 50%; background: var(--muted); transition: background 0.3s; }
    .status-pill.scanning .dot { background: var(--yellow); animation: pulse 0.8s infinite; }
    .status-pill.done .dot { background: var(--green); }
    @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
    
    .controls { display: flex; gap: 12px; margin-bottom: 32px; flex-wrap: wrap; align-items: flex-end; }
    .field { display: flex; flex-direction: column; gap: 6px; flex: 1; min-width: 180px; }
    .field label { font-family: var(--mono); font-size: 0.7rem; color: var(--muted); letter-spacing: 0.1em; text-transform: uppercase; font-weight: 600; }
    .field input { background: var(--surface); border: 1.5px solid var(--border); border-radius: 8px; padding: 10px 14px; font-family: var(--mono); font-size: 0.95rem; color: var(--text); outline: none; transition: all 0.2s; }
    .field input:focus { border-color: var(--accent); box-shadow: 0 0 0 3px rgba(45,93,63,0.1); }
    
    .btn { padding: 10px 28px; border-radius: 8px; border: none; font-family: var(--sans); font-size: 0.95rem; font-weight: 700; cursor: pointer; letter-spacing: 0.02em; transition: all 0.15s; white-space: nowrap; height: 42px; }
    .btn:active { transform: scale(0.97); }
    .btn:disabled { opacity: 0.4; cursor: not-allowed; }
    .btn-primary { background: var(--accent); color: #fff; box-shadow: 0 2px 6px rgba(45,93,63,0.2); }
    .btn-primary:hover { background: #1f4a2d; box-shadow: 0 4px 12px rgba(45,93,63,0.3); }
    .btn-ghost { background: transparent; border: 1.5px solid var(--border); color: var(--muted); }
    .btn-ghost:hover { border-color: var(--accent); color: var(--accent); }
    
    .progress-wrap { height: 2px; background: var(--border); border-radius: 2px; margin-bottom: 32px; overflow: hidden; display: none; }
    .progress-wrap.active { display: block; }
    .progress-bar { height: 100%; background: var(--accent); width: 0%; transition: width 0.3s; border-radius: 2px; }
    
    .stats { display: flex; gap: 24px; margin-bottom: 28px; font-family: var(--mono); font-size: 0.78rem; color: var(--muted); }
    .stats span { color: var(--text); font-weight: 600; }
    
    .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); gap: 16px; }
    .card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; padding: 20px; cursor: pointer; transition: all 0.2s; text-decoration: none; color: inherit; display: block; animation: fadeIn 0.3s ease both; }
    .card:hover { border-color: var(--accent); transform: translateY(-2px); box-shadow: 0 2px 8px rgba(45,93,63,0.08); }
    @keyframes fadeIn { from { opacity: 0; transform: translateY(8px); } to { opacity: 1; transform: translateY(0); } }
    
    .card-header { display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 16px; }
    .device-name { font-weight: 600; font-size: 1.1rem; line-height: 1.3; color: var(--text); }
    .device-id { font-family: var(--mono); font-size: 0.7rem; color: var(--muted); margin-top: 4px; }
    .online-badge { font-family: var(--mono); font-size: 0.65rem; padding: 4px 10px; border-radius: 20px; background: rgba(45,93,63,0.15); color: var(--green); border: 1px solid var(--green); white-space: nowrap; }
    
    .card-ip { font-family: var(--mono); font-size: 0.85rem; color: var(--accent); margin-bottom: 16px; font-weight: 600; }
    
    .meter { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-bottom: 16px; }
    .meter-item { background: var(--bg); border: 1px solid var(--border); border-radius: 8px; padding: 10px 12px; }
    .meter-label { font-size: 0.65rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.08em; margin-bottom: 4px; font-weight: 600; }
    .meter-val { font-family: var(--mono); font-size: 0.95rem; font-weight: 600; color: var(--accent); }

    .card-footer { font-family: var(--mono); font-size: 0.7rem; color: var(--muted); display: flex; justify-content: space-between; border-top: 1px solid var(--border); padding-top: 12px; margin-top: 4px; }
    .card-footer span:last-child { color: var(--accent); font-weight: 600; }
    
    .empty { grid-column: 1 / -1; text-align: center; padding: 60px 20px; color: var(--muted); }
    .empty .icon { font-size: 3rem; margin-bottom: 16px; opacity: 0.5; }
    .empty p { font-size: 0.95rem; line-height: 1.7; }
    
    @media (max-width: 768px) {
      .wrap { padding: 32px 20px; }
      .logo-block h1 { font-size: 1.8rem; }
      .controls { flex-direction: column; }
      .grid { grid-template-columns: 1fr; }
    }
    @media (max-width: 500px) {
      .wrap { padding: 20px 16px; }
      .logo-block h1 { font-size: 1.5rem; }
      .controls { flex-direction: column; }
      .btn { width: 100%; }
      header { flex-direction: column; align-items: flex-start; }
      .status-pill { align-self: flex-start; }
    }
  </style>
</head>
<body>
<div class="wrap">
  <header>
    <div class="logo-block">
      <div class="label">// device discovery</div>
      <h1>BOSS FARM <span>POWER METER</span></h1>
    </div>
    <div class="status-pill" id="status-pill">
      <span class="dot"></span>
      <span id="status-text">Ready</span>
    </div>
  </header>

  <div class="controls">
    <div class="field">
      <label>Subnet prefix</label>
      <input type="text" id="subnet" value="192.168.0" placeholder="e.g. 192.168.0">
    </div>
    <div class="field" style="max-width: 100px;">
      <label>From</label>
      <input type="number" id="range-from" value="1" min="1" max="254">
    </div>
    <div class="field" style="max-width: 100px;">
      <label>To</label>
      <input type="number" id="range-to" value="254" min="1" max="254">
    </div>
    <div class="field" style="max-width: 120px;">
      <label>Timeout (ms)</label>
      <input type="number" id="timeout" value="800" min="200" max="3000" step="100">
    </div>
    <button class="btn btn-primary" id="scan-btn" onclick="startScan()">Scan Network</button>
    <button class="btn btn-ghost" id="stop-btn" onclick="stopScan()" disabled>Stop</button>
  </div>

  <div class="progress-wrap" id="progress-wrap">
    <div class="progress-bar" id="progress-bar"></div>
  </div>

  <div class="stats" id="stats" style="display: none;">
    Scanned <span id="stat-scanned">0</span> / <span id="stat-total">0</span> &nbsp;·&nbsp;
    Found <span id="stat-found">0</span> device(s) &nbsp;·&nbsp;
    <span id="stat-elapsed">0s</span>
  </div>

  <div class="grid" id="grid">
    <div class="empty">
      <div class="icon">📡</div>
      <p>Enter your subnet and hit <strong>Scan Network</strong>.<br>All ESP32 units (PM2200 power meters included) on the same WiFi will appear here.</p>
    </div>
  </div>
</div>

<!-- ============================================================================
     DEVICE DISCOVERY SCRIPT (DO NOT MODIFY LOGIC)
     
     This script handles network scanning and device discovery.
     All functions below are client-side and do not require backend changes.
     ============================================================================ -->

<script>
let scanning = false, stopFlag = false, foundCount = 0, scanStart = 0, statsTimer = null;

function setStatus(text, state) {
  const pill = document.getElementById('status-pill');
  const txt = document.getElementById('status-text');
  pill.className = 'status-pill ' + (state || '');
  txt.textContent = text;
}

function renderCard(ip, info, power) {
  const p = power || {};
  const kw  = p.p_total_w == null ? '–' : (p.p_total_w / 1000).toFixed(2) + ' kW';
  const kwh = p.energy_wh == null ? '–' : (p.energy_wh / 1000).toFixed(1) + ' kWh';
  const mode = power ? (p.simulated ? 'simulated' : (p.meter_ok ? 'meter OK' : 'no meter response')) : 'no meter data';

  const card = document.createElement('a');
  card.className = 'card';
  card.href = 'http://' + ip;
  card.target = '_blank';
  card.innerHTML = `
    <div class="card-header">
      <div>
        <div class="device-name">${info.device_name || 'ESP32 Device'}</div>
        <div class="device-id">${info.device_id || '–'}</div>
      </div>
      <span class="online-badge">● ONLINE</span>
    </div>
    <div class="card-ip">${ip}</div>
    <div class="meter">
      <div class="meter-item"><div class="meter-label">⚡ Active power</div><div class="meter-val">${kw}</div></div>
      <div class="meter-item"><div class="meter-label">🔋 Energy</div><div class="meter-val">${kwh}</div></div>
    </div>
    <div class="card-footer">
      <span>fw ${info.firmware || '–'} · ${mode}</span>
      <span>Open UI →</span>
    </div>
  `;
  return card;
}

async function fetchWithTimeout(url, ms) {
  const ctrl = new AbortController();
  const tid = setTimeout(() => ctrl.abort(), ms);
  try {
    const r = await fetch(url, { signal: ctrl.signal });
    clearTimeout(tid);
    return r;
  } catch {
    clearTimeout(tid);
    return null;
  }
}

async function probeIp(ip, timeoutMs) {
  const infoRes = await fetchWithTimeout('http://' + ip + '/device_info', timeoutMs);
  if (!infoRes || !infoRes.ok) return null;
  
  let info = {};
  try {
    info = await infoRes.json();
  } catch {
    return null;
  }
  
  if (!info.device_name || !info.device_name.startsWith('ESP32')) return null;

  let power = null;
  const powRes = await fetchWithTimeout('http://' + ip + '/power', timeoutMs);
  if (powRes && powRes.ok) {
    try {
      power = await powRes.json();
    } catch {}
  }

  return { ip, info, power };
}

async function startScan() {
  if (scanning) return;
  
  const subnet = document.getElementById('subnet').value.trim();
  const from = parseInt(document.getElementById('range-from').value);
  const to = parseInt(document.getElementById('range-to').value);
  const timeout = parseInt(document.getElementById('timeout').value);
  
  if (!subnet || isNaN(from) || isNaN(to) || from > to) {
    alert('Please check your subnet and range values.');
    return;
  }
  
  scanning = true;
  stopFlag = false;
  foundCount = 0;
  scanStart = Date.now();
  
  const total = to - from + 1;
  let scanned = 0;
  
  document.getElementById('scan-btn').disabled = true;
  document.getElementById('stop-btn').disabled = false;
  document.getElementById('progress-wrap').classList.add('active');
  document.getElementById('stats').style.display = 'flex';
  document.getElementById('stat-total').textContent = total;
  document.getElementById('stat-scanned').textContent = 0;
  document.getElementById('stat-found').textContent = 0;
  document.getElementById('grid').innerHTML = '';
  
  setStatus('Scanning...', 'scanning');
  
  statsTimer = setInterval(() => {
    document.getElementById('stat-elapsed').textContent = ((Date.now() - scanStart) / 1000).toFixed(1) + 's';
  }, 200);
  
  const ips = [];
  for (let i = from; i <= to; i++) {
    ips.push(subnet + '.' + i);
  }
  
  const BATCH = 30;
  for (let b = 0; b < ips.length && !stopFlag; b += BATCH) {
    const batch = ips.slice(b, b + BATCH);
    const results = await Promise.all(batch.map(ip => probeIp(ip, timeout)));
    
    for (const result of results) {
      scanned++;
      if (result) {
        foundCount++;
        document.getElementById('stat-found').textContent = foundCount;
        document.getElementById('grid').appendChild(renderCard(result.ip, result.info, result.power));
      }
    }
    
    document.getElementById('stat-scanned').textContent = scanned;
    document.getElementById('progress-bar').style.width = ((scanned / total) * 100).toFixed(1) + '%';
  }
  
  clearInterval(statsTimer);
  scanning = false;
  document.getElementById('scan-btn').disabled = false;
  document.getElementById('stop-btn').disabled = true;
  document.getElementById('progress-bar').style.width = '100%';
  
  if (foundCount === 0) {
    document.getElementById('grid').innerHTML = `
      <div class="empty">
        <div class="icon">🔍</div>
        <p>No ESP32 units found on <strong>${subnet}.${from}–${to}</strong>.<br>Make sure all units are on the same WiFi and try adjusting the subnet or timeout.</p>
      </div>
    `;
    setStatus('No devices found', '');
  } else {
    setStatus('Found ' + foundCount + ' device' + (foundCount > 1 ? 's' : ''), 'done');
  }
}

function stopScan() {
  stopFlag = true;
  clearInterval(statsTimer);
  setStatus('Stopped', '');
  document.getElementById('scan-btn').disabled = false;
  document.getElementById('stop-btn').disabled = true;
  scanning = false;
}
</script>
</body>
</html>
)rawliteral";

// ── Helpers ───────────────────────────────────────────────────────────────────

static String escapeJson(const String& input) {
    String output;
    for (unsigned int i = 0; i < input.length(); i++) {
        char c = input[i];
        if      (c == '"')  output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else if (c == '<')  output += "\\u003c";
        else if (c == '>')  output += "\\u003e";
        else                output += c;
    }
    return output;
}

static String getDeviceName() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char name[32];
    snprintf(name, sizeof(name), "ESP32-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(name);
}

static String getDeviceMacString() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// ── HTTP Handlers ─────────────────────────────────────────────────────────────

static void handleCaptivePortal() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

static void addCorsHeaders();

static void handleRoot() {
  addCorsHeaders();
  server.send(200, "text/html", HTML_PAGE);
}

static void handleWifiGet() {
  addCorsHeaders();
  char buf[160];
  snprintf(buf, sizeof(buf), "{\"ssid\":\"%s\",\"connected\":%s}",
       escapeJson(wifiConfigSsid()).c_str(),
       WiFi.status() == WL_CONNECTED ? "true" : "false");
  server.send(200, "application/json", buf);
}

static void handleScan() {
    server.send(200, "application/json", wifiScanNetworks());
}

static void handleSetWifi() {
    if (!server.hasArg("ssid") || !server.hasArg("pass")) {
        server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Missing ssid/pass\"}");
        return;
    }

    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    // Open network => ignore password
    bool isOpen = !server.hasArg("secure") || server.arg("secure") == "false";
    if (isOpen) pass = "";

    if (!wifiConfigSave(ssid, pass)) {
        server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Invalid password (min 8 chars)\"}");
        return;
    }

    bool ok = wifiConfigConnect(10000);
    if (!ok) {
        server.send(200, "application/json", "{\"ok\":false,\"msg\":\"Connection failed — check password\"}");
        return;
    }

    // Success path
    setCommissionedPublic();
    localMqttInit();  // start local mqtt now that WiFi is up

    String ip       = WiFi.localIP().toString();
    String mac      = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    String hostname = String(MDNS_PREFIX) + "-" + mac.substring(8);

    String json = "{\"ok\":true,\"ip\":\"" + ip + "\",\"mdns\":\"" + hostname + ".local\"}";
    server.send(200, "application/json", json);

    // IMPORTANT: disconnect AP AFTER response is sent
    delay(200);
    WiFi.softAPdisconnect(true);
    Serial.println("[AP] Hotspot hidden after WiFi connect via web UI");
}

static void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Accept");
}

static void handleDeviceInfo() {
    addCorsHeaders();
    String mac     = getDeviceMacString();
    String devName = getDeviceName();
    String mac2    = WiFi.macAddress();
    mac2.replace(":", "");
    mac2.toLowerCase();
    String mdns = String(MDNS_PREFIX) + "-" + mac2.substring(8) + ".local";
    char buf[320];
    snprintf(buf, sizeof(buf),
        "{\"device_id\":\"%s\",\"device_name\":\"%s\",\"mdns\":\"%s\","
        "\"ip\":\"%s\",\"rssi\":%d,\"firmware\":\"%s\",\"commissioned\":%s}",
        mac.c_str(), devName.c_str(), mdns.c_str(),
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI(), FIRMWARE_VERSION,
        deviceIsCommissioned() ? "true" : "false"
    );
    server.send(200, "application/json", buf);
}

// Live meter values for the setup page and the discovery cards. Same keys/units as the MQTT
// `reading` object (docs/payload.md) plus status, so the page shows exactly what is published.
static void handlePower() {
    addCorsHeaders();

    char buf[1400];
    int n = snprintf(buf, sizeof(buf),
        "{\"sensor_ok\":%s,\"meter_ok\":%s,\"simulated\":%s,\"status\":\"%s\",\"poll\":%lu",
        pm2200SensorOK ? "true" : "false",
        pm2200MeterOK  ? "true" : "false",
        pm2200Simulated() ? "true" : "false",
        pm2200StatusLabel(),
        (unsigned long)rs485PollCount);

    int m = pm2200ReadingJson(buf + n, sizeof(buf) - n - 2);   // leave room for "}" and the NUL
    if (n < 0 || m < 0) {
        server.send(500, "application/json", "{\"status\":\"error\"}");
        return;
    }
    strcpy(buf + n + m, "}");
    server.send(200, "application/json", buf);
}

static void handleGetMeter() {
    addCorsHeaders();
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"addr\":%u,\"baud\":%lu,\"parity\":\"%c\",\"sim\":%s}",
             pm2200Addr(), (unsigned long)pm2200Baud(), pm2200Parity(),
             pm2200Simulated() ? "true" : "false");
    server.send(200, "application/json", buf);
}

static void handleSetMeter() {
    addCorsHeaders();
    long   addr   = server.arg("addr").toInt();
    long   baud   = server.arg("baud").toInt();
    String parity = server.arg("parity");

    bool baudOk   = (baud == 4800 || baud == 9600 || baud == 19200 || baud == 38400);
    bool parityOk = (parity == "E" || parity == "O" || parity == "N");
    if (addr < 1 || addr > 247 || !baudOk || !parityOk) {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid address, baud or parity\"}");
        return;
    }
    pm2200ApplyConfig((uint8_t)addr, (uint32_t)baud, parity[0], server.arg("sim") == "1");
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleProvStatus() {
    bool provisioned = provisioningHasToken();
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"provisioned\":%s}", provisioned ? "true" : "false");
    server.send(200, "application/json", buf);
}

static void handleProvision() {
    if (!WiFi.isConnected()) {
        server.send(200, "application/json", "{\"status\":\"error\",\"message\":\"WiFi not connected\"}");
        return;
    }
    String token = provisioningRequest();
    if (!token.isEmpty()) {
        server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Provisioning successful\"}");
    } else {
        server.send(200, "application/json", "{\"status\":\"error\",\"message\":\"Provisioning failed\"}");
    }
}

static void handleSetToken() {
    if (!server.hasArg("token")) { server.send(400, "application/json", "{\"status\":\"error\"}"); return; }
    String token = server.arg("token");
    if (token.length() < 5) { server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Token too short\"}"); return; }
    Preferences prefs;
    prefs.begin("provision", false);
    prefs.putString("token", token);
    prefs.end();
    Serial.printf("[Web] Token saved: %.10s...\n", token.c_str());
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleFactoryReset() {
    Serial.println("[Reset] Web factory reset — clearing all NVS...");
    const char* namespaces[] = { "device", "netcfg", "thresholds", "provision", nullptr };
    for (int i = 0; namespaces[i] != nullptr; i++) {
        Preferences p;
        p.begin(namespaces[i], false);
        p.clear();
        p.end();
        Serial.printf("[Reset] Cleared: %s\n", namespaces[i]);
    }
    server.send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
}

static void handleDiscover() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", DISCOVER_PAGE);
}

static void handleLogs() {
    addCorsHeaders();
    uint32_t count = _logHead < LOG_BUF_SIZE ? _logHead : LOG_BUF_SIZE;
    uint32_t start = _logHead >= LOG_BUF_SIZE ? _logHead % LOG_BUF_SIZE : 0;
    String json = "{\"seq\":" + String(_logHead) + ",\"lines\":[";
    for (uint32_t i = 0; i < count; i++) {
        if (i > 0) json += ",";
        String line = _logBuf[(start + i) % LOG_BUF_SIZE];
        line.replace("\\", "\\\\");
        line.replace("\"", "\\\"");
        json += "\"" + line + "\"";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

static void handleRegister() {
    bool wifiOk = registerDevice();
    if (wifiOk) {
        String ip       = WiFi.localIP().toString();
        String mac      = WiFi.macAddress();
        mac.replace(":", "");
        mac.toLowerCase();
        String hostname = String(MDNS_PREFIX) + "-" + mac.substring(8);
        String json     = "{\"wifi\":true,\"broker\":" +
                          String(localMqttIsConnected() ? "true" : "false") +
                          ",\"ip\":\"" + ip + "\",\"mdns\":\"" + hostname + ".local\"}";
        server.send(200, "application/json", json);
    } else {
        server.send(200, "application/json", "{\"wifi\":false,\"broker\":false,\"ip\":\"\",\"mdns\":\"\"}");
    }
}

static void handleSetBroker() {
    if (!server.hasArg("ip")) { server.send(400, "application/json", "{\"status\":\"error\"}"); return; }
    String ip     = server.arg("ip");
    uint16_t port = server.hasArg("port") ? (uint16_t)server.arg("port").toInt() : 1883;
    localMqttSetBroker(ip, port);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleBrokerStatus() {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"connected\":%s,\"ip\":\"%s\",\"port\":%d}",
             localMqttIsConnected()        ? "true" : "false",
             localMqttGetBrokerIP().c_str(),
             localMqttGetBrokerPort());
    server.send(200, "application/json", buf);
}

// ── Server Init — ALL routes registered before server.begin() ─────────────────
void webServerInit() {
    server.on("/",                          handleRoot);
    server.on("/discover",    HTTP_GET,     handleDiscover);

    // Captive portal
    server.on("/hotspot-detect.html",       handleCaptivePortal);
    server.on("/library/test/success.html", handleCaptivePortal);
    server.on("/success.txt",               handleCaptivePortal);
    server.on("/generate_204",              handleCaptivePortal);
    server.on("/gen_204",                   handleCaptivePortal);
    server.onNotFound(handleCaptivePortal);

    // API
    server.on("/power",         HTTP_GET,  handlePower);
    server.on("/meter",         HTTP_GET,  handleGetMeter);
    server.on("/set_meter",     HTTP_POST, handleSetMeter);
    server.on("/wifi",          HTTP_GET,  handleWifiGet);
    server.on("/scan",          HTTP_GET,  handleScan);
    server.on("/set_wifi",      HTTP_POST, handleSetWifi);
    server.on("/prov_status",   HTTP_GET,  handleProvStatus);
    server.on("/provision",     HTTP_POST, handleProvision);
    server.on("/device_info",   HTTP_GET,  handleDeviceInfo);
    server.on("/set_token",     HTTP_POST, handleSetToken);
    server.on("/factory_reset", HTTP_POST, handleFactoryReset);
    server.on("/register",      HTTP_POST, handleRegister);
    server.on("/set_broker",    HTTP_POST, handleSetBroker);
    server.on("/broker_status", HTTP_GET,  handleBrokerStatus);
    server.on("/logs",          HTTP_GET,  handleLogs);

    server.begin();
    Serial.println("[Web] Server started");
}

void webServerHandle() {
    server.handleClient();
}