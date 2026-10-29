#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>

// ===== CONFIG =====
const char* ap_ssid      = "ShutdownHub";
const char* ap_password  = "shutdown123";

const char* DASH_PIN     = "1234";

const int PC_COUNT = 2;
const char* PC_NAMES[PC_COUNT] = { "Gaming PC", "Office PC" };
const char* PC_IPS[PC_COUNT]   = { "192.168.4.2", "192.168.4.2" };
const int REMOTE_PC_PORT = 8000;

const int MAX_RETRIES = 10;
const unsigned long RETRY_INTERVAL_MS = 30000;
struct PendingCmd { bool active; int pc; int retries; unsigned long nextAt; };
PendingCmd pending[PC_COUNT];
// ==================

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);

long remainingSeconds = 0;
long totalSeconds = 0;
bool timerRunning = false;
bool themeFired = false;
unsigned long lastTick = 0;
String lastActivity = "{\"win\":\"—\",\"idle\":0,\"cpu\":0,\"ram\":0}";

String formatTime(long s) {
  char b[16];
  snprintf(b, sizeof(b), "%02ld:%02ld:%02ld", s/3600, (s%3600)/60, s%60);
  return String(b);
}

bool checkPin() {
  if (!server.hasArg("pin") || server.arg("pin") != DASH_PIN) {
    server.send(401, "text/plain", "Bad PIN");
    return false;
  }
  return true;
}

void logHistory(const String& event, const String& detail) {
  File f = LittleFS.open("/history.log", "a");
  if (!f) return;
  f.printf("%lu,%s,%s\n", (unsigned long)time(nullptr), event.c_str(), detail.c_str());
  f.close();
}

void broadcast(String msg) { webSocket.broadcastTXT(msg); }

bool getOnPc(int idx, const char* path) {
  WiFiClient c;
  c.setTimeout(1200);
  if (!c.connect(PC_IPS[idx], REMOTE_PC_PORT)) return false;
  c.println(String("GET ") + path + " HTTP/1.1");
  c.println("Host: " + String(PC_IPS[idx]));
  c.println("Connection: close");
  c.println();
  c.stop();
  return true;
}

bool pingPc(int idx) {
  WiFiClient c;
  c.setTimeout(500);
  bool ok = c.connect(PC_IPS[idx], REMOTE_PC_PORT);
  c.stop();
  return ok;
}

void abortAllListeners() {
  for (int i = 0; i < PC_COUNT; i++) {
    if (getOnPc(i, "/abort")) {
      Serial.print("ABORT -> "); Serial.println(PC_NAMES[i]);
    }
  }
}

void queueShutdown(int idx) {
  pending[idx].active  = true;
  pending[idx].pc      = idx;
  pending[idx].retries = 0;
  pending[idx].nextAt  = millis();
}

void processRetryQueue() {
  for (int i = 0; i < PC_COUNT; i++) {
    if (!pending[i].active) continue;
    if (millis() < pending[i].nextAt) continue;
    if (getOnPc(i, "/shutdown")) {
      pending[i].active = false;
      broadcast("{\"type\":\"log\",\"msg\":\"Retry OK: " + String(PC_NAMES[i]) + "\"}");
      logHistory("SHUTDOWN_OK_RETRY", PC_NAMES[i]);
    } else if (++pending[i].retries >= MAX_RETRIES) {
      pending[i].active = false;
      broadcast("{\"type\":\"log\",\"msg\":\"Gave up: " + String(PC_NAMES[i]) + "\"}");
      logHistory("SHUTDOWN_FAILED", PC_NAMES[i]);
    } else {
      pending[i].nextAt = millis() + RETRY_INTERVAL_MS;
    }
  }
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Control Hub</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:system-ui;background:#0f1220;color:#e8eaff;padding:20px;text-align:center}
h1{color:#ff4d6d;margin:10px}
h2{color:#9aa0c4;font-size:1rem;margin-top:24px}
.row{margin:10px 0;display:flex;gap:8px;justify-content:center;flex-wrap:wrap}
input,select{padding:10px;font-size:1rem;border-radius:8px;border:1px solid #333;background:#12152a;color:#fff}
input[type=number]{width:90px;text-align:center}
input[type=password]{width:120px}
button{padding:10px 18px;font-size:.9rem;font-weight:700;border:none;border-radius:8px;cursor:pointer;color:#fff}
.start{background:linear-gradient(90deg,#6c5ce7,#00d4ff)}
.cancel{background:#ff4d6d}
.ghost{background:#2a2e4a}
.pin{background:#fdcb6e;color:#111}
#countdown{font-size:3rem;font-weight:800;color:#00d4ff;margin:14px 0}
#selected{font-size:1rem;color:#9aa0c4;margin-top:4px;min-height:22px}
#status{color:#9aa0c4;min-height:22px;margin-top:8px}
.pc-list{display:flex;flex-direction:column;gap:10px;max-width:520px;margin:14px auto}
.pc-card{background:#12152a;padding:14px;border-radius:10px;display:flex;justify-content:space-between;align-items:center;border-left:4px solid #6c5ce7}
.pc-name{font-weight:700}
.pc-ip{color:#9aa0c4;font-size:.78rem}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block;margin-right:6px;background:#555}
.dot.alive{background:#00e676;box-shadow:0 0 8px #00e676}
.activity{max-width:520px;margin:14px auto;background:#12152a;border-radius:10px;padding:14px;border-left:4px solid #00d4ff;text-align:left}
.row-act{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #1a1d33}
.row-act:last-child{border-bottom:none}
.lbl{color:#9aa0c4;font-size:.85rem}
.val{font-weight:700;color:#e8eaff;font-size:.9rem;max-width:320px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.hist{max-width:520px;margin:14px auto;text-align:left;font-size:.8rem;color:#9aa0c4}
</style></head><body>
<h1>CONTROL HUB</h1>
<div class="row">
  <input type="password" id="pin" placeholder="PIN">
  <button class="pin" onclick="savePin()">Unlock</button>
</div>
<div id="countdown">--:--:--</div>
<div id="selected">Selected: —</div>
<div class="row">
  <input type="number" id="timeValue" value="5" min="1">
  <select id="unit">
    <option value="s">Seconds</option>
    <option value="m" selected>Minutes</option>
    <option value="h">Hours</option>
  </select>
</div>
<div class="row">
  <button class="ghost" onclick="quick(60)">1m</button>
  <button class="ghost" onclick="quick(300)">5m</button>
  <button class="ghost" onclick="quick(600)">10m</button>
  <button class="ghost" onclick="quick(1800)">30m</button>
  <button class="ghost" onclick="quick(3600)">1h</button>
</div>
<div class="row">
  <button class="start" onclick="startTimer()">START</button>
  <button class="cancel" onclick="call('/cancel')">CANCEL</button>
  <button class="cancel" onclick="call('/abort')">ABORT</button>
</div>
<div id="status"></div>

<h2>PCs</h2>
<div class="pc-list" id="pcList"></div>

<h2>PC Status</h2>
<div class="activity" id="activity">
  <div class="row-act"><span class="lbl">Active window</span><span class="val" id="actWin">—</span></div>
  <div class="row-act"><span class="lbl">Idle</span><span class="val" id="actIdle">—</span></div>
  <div class="row-act"><span class="lbl">CPU</span><span class="val" id="actCpu">—</span></div>
  <div class="row-act"><span class="lbl">RAM</span><span class="val" id="actRam">—</span></div>
</div>

<h2>History</h2>
<div class="hist" id="hist"></div>

<script>
let PIN='';
function savePin(){PIN=document.getElementById('pin').value;localStorage.setItem('pin',PIN);document.getElementById('status').innerText='PIN saved';loadHistory();checkAll();fetch('/activity').then(r=>r.json()).then(renderActivity);}
window.addEventListener('load',()=>{PIN=localStorage.getItem('pin')||'';document.getElementById('pin').value=PIN;loadHistory();checkAll();fetch('/activity').then(r=>r.json()).then(renderActivity);});

const ws=new WebSocket('ws://'+location.hostname+':81/');
ws.onmessage=e=>{
  const d=JSON.parse(e.data);
  const cd=document.getElementById('countdown'),st=document.getElementById('status'),sel=document.getElementById('selected');
  if(d.type==='start'){cd.innerText=d.display;sel.innerText='Selected: '+d.display;st.innerText='Countdown running...';}
  else if(d.type==='tick'){cd.innerText=d.display;}
  else if(d.type==='done'){cd.innerText='00:00:00';sel.innerText='';st.innerText='SELF-DESTRUCT TRIGGERED';}
  else if(d.type==='cancelled'){cd.innerText='--:--:--';sel.innerText='Selected: —';st.innerText='Cancelled';}
  else if(d.type==='activity'){renderActivity(d.data);}
  else if(d.type==='log'){console.log(d.msg);}
};
function call(path){fetch(path+'?pin='+encodeURIComponent(PIN)).then(r=>r.text()).then(t=>{document.getElementById('status').innerText=t;loadHistory();});}
function startTimer(){
  const v=document.getElementById('timeValue').value;
  const u=document.getElementById('unit').value;
  fetch('/start?v='+v+'&u='+u+'&pin='+encodeURIComponent(PIN)).then(r=>r.text()).then(t=>document.getElementById('status').innerText=t);
}
function quick(s){document.getElementById('timeValue').value=s;document.getElementById('unit').value='s';startTimer();}
function renderActivity(d){
  if(!d)return;
  document.getElementById('actWin').innerText=d.win||'—';
  const idle=d.idle||0;
  document.getElementById('actIdle').innerText=idle<5?'active':idle+' s';
  document.getElementById('actCpu').innerText=(d.cpu??'—')+' %';
  document.getElementById('actRam').innerText=(d.ram??'—')+' %';
}
function renderPCs(list){
  const c=document.getElementById('pcList');c.innerHTML='';
  list.forEach((pc,i)=>{
    const d=document.createElement('div');d.className='pc-card';
    d.innerHTML='<div><div class="pc-name"><span class="dot" id="dot'+i+'"></span>'+pc.name+'</div><div class="pc-ip">'+pc.ip+'</div></div>'+
      '<div><button class="cancel" onclick="call(\'/shutdown?pc='+i+'\')">SHUTDOWN</button></div>';
    c.appendChild(d);
  });
  checkAll();
}
function checkAll(){
  fetch('/pcs').then(r=>r.json()).then(list=>{
    if(!document.getElementById('pcList').children.length)renderPCs(list);
    list.forEach((_,i)=>{fetch('/ping?pc='+i).then(r=>r.json()).then(j=>{document.getElementById('dot'+i).className='dot'+(j.alive?' alive':'');});});
  });
}
setInterval(checkAll,10000);
function loadHistory(){
  fetch('/history').then(r=>r.json()).then(list=>{
    const h=document.getElementById('hist');h.innerHTML=list.map(x=>`<div>${x.ts} — ${x.event} — ${x.detail}</div>`).join('');
  });
}
fetch('/pcs').then(r=>r.json()).then(renderPCs);
</script></body></html>
)rawliteral";

void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void handleStart() {
  if (!checkPin()) return;
  if (!server.hasArg("v") || !server.hasArg("u")) { server.send(400,"text/plain","missing"); return; }
  long v = server.arg("v").toInt(); String u = server.arg("u");
  if (v <= 0) { server.send(400,"text/plain","invalid"); return; }
  long s = u=="s"?v : u=="m"?v*60 : u=="h"?v*3600 : 0;
  if (!s) { server.send(400,"text/plain","invalid unit"); return; }

  remainingSeconds = totalSeconds = s;
  timerRunning = true;
  themeFired = false;
  lastTick = millis();

  String display = formatTime(remainingSeconds);
  broadcast("{\"type\":\"start\",\"display\":\"" + display + "\"}");
  Serial.print("TIMER START: "); Serial.println(display);

  server.send(200,"text/plain","Timer started: " + String(v) + u);
  logHistory("START", String(v) + u);
}

void handleCancel() {
  if (!checkPin()) return;
  if (!timerRunning) { server.send(200,"text/plain","No timer running"); return; }
  timerRunning = false; remainingSeconds = 0;
  Serial.println("ABORT");
  Serial.flush();
  abortAllListeners();
  broadcast("{\"type\":\"cancelled\"}");
  server.send(200,"text/plain","Timer cancelled");
  logHistory("CANCEL","");
}

void handleAbort() {
  if (!checkPin()) return;
  timerRunning = false; remainingSeconds = 0;
  Serial.println("ABORT");
  Serial.flush();
  abortAllListeners();
  broadcast("{\"type\":\"cancelled\"}");
  server.send(200,"text/plain","ABORTED");
  logHistory("ABORT","");
}

void handlePcList() {
  String json = "[";
  for (int i = 0; i < PC_COUNT; i++) {
    if (i) json += ",";
    json += "{\"name\":\"" + String(PC_NAMES[i]) + "\",\"ip\":\"" + String(PC_IPS[i]) + "\"}";
  }
  json += "]";
  server.send(200,"application/json",json);
}

void handlePing() {
  if (!server.hasArg("pc")) { server.send(400,"text/plain","missing"); return; }
  int i = server.arg("pc").toInt();
  bool ok = pingPc(i);
  String body = "{\"alive\":";
  body += ok ? "true" : "false";
  body += "}";
  server.send(200, "application/json", body);
}

void handleShutdown() {
  if (!checkPin()) return;
  int i = server.arg("pc").toInt();
  if (i<0||i>=PC_COUNT) { server.send(400,"text/plain","bad"); return; }
  bool ok = getOnPc(i, "/shutdown");
  if (ok) { server.send(200,"text/plain","Shutdown sent to " + String(PC_NAMES[i])); logHistory("SHUTDOWN",PC_NAMES[i]); }
  else    { queueShutdown(i); server.send(200,"text/plain","Queued (will retry) " + String(PC_NAMES[i])); logHistory("SHUTDOWN_QUEUED",PC_NAMES[i]); }
}

void handleHistory() {
  File f = LittleFS.open("/history.log","r");
  String json = "[";
  bool first = true;
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      int c1 = line.indexOf(',');
      int c2 = line.indexOf(',', c1+1);
      if (c1<0||c2<0) continue;
      String ts = line.substring(0,c1);
      String ev = line.substring(c1+1,c2);
      String de = line.substring(c2+1);
      if (!first) json += ",";
      first = false;
      json += "{\"ts\":\""+ts+"\",\"event\":\""+ev+"\",\"detail\":\""+de+"\"}";
    }
    f.close();
  }
  json += "]";
  server.send(200,"application/json",json);
}

void handleActivity() {
  if (server.hasArg("data")) {
    lastActivity = server.arg("data");
    String msg = "{\"type\":\"activity\",\"data\":" + lastActivity + "}";
    webSocket.broadcastTXT(msg);
  }
  server.send(200,"text/plain","ok");
}

void tickTimer() {
  if (!timerRunning) return;
  if (millis() - lastTick < 1000) return;
  lastTick = millis();
  remainingSeconds--;
  String display = formatTime(remainingSeconds);
  broadcast("{\"type\":\"tick\",\"display\":\""+display+"\"}");

  Serial.print("T-"); Serial.println(display);

  // Fire the theme once, when exactly 15 seconds remain
  if (!themeFired && remainingSeconds <= 15 && remainingSeconds > 0) {
    themeFired = true;
    Serial.println("PLAY_THEME");
    Serial.flush();
    logHistory("THEME_FIRED", "15s");
  }

  if (remainingSeconds <= 0) {
    timerRunning = false;
    broadcast("{\"type\":\"done\"}");
    logHistory("SELF_DESTRUCT","");
    Serial.println("SELF_DESTRUCT");
    Serial.flush();
  }
}

// ---------- setup ----------
void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n=== CONTROL HUB BOOT ===");

  LittleFS.begin();

  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(300);
  WiFi.mode(WIFI_OFF);
  delay(300);
  WiFi.mode(WIFI_AP);
  delay(300);

  bool apOk = WiFi.softAP(ap_ssid, ap_password, 6, 0);
  Serial.print("AP started: "); Serial.println(apOk ? "YES" : "NO");
  Serial.print("AP SSID: "); Serial.println(ap_ssid);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/cancel", handleCancel);
  server.on("/abort", handleAbort);
  server.on("/pcs", handlePcList);
  server.on("/ping", handlePing);
  server.on("/shutdown", handleShutdown);
  server.on("/history", handleHistory);
  server.on("/activity", handleActivity);
  server.begin();

  webSocket.begin();
  webSocket.onEvent([](uint8_t, WStype_t, uint8_t*, size_t){});

  for (int i = 0; i < PC_COUNT; i++) pending[i].active = false;

  Serial.println("Ready.");
}

void loop() {
  server.handleClient();
  webSocket.loop();
  processRetryQueue();
  tickTimer();
}