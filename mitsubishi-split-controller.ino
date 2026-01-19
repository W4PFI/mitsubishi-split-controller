#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <time.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Mitsubishi.h>
#include "wifi_setup.h"

#define BTN_PIN 0
#define IR_SEND_PIN 4
#define LED_PIN 2
#define PROTECTION_DELAY_MS 300000
#define RESET_HOLD_TIME_MS 10000
#define NUM_TIMERS 4

AsyncWebServer server(80);
IRMitsubishiAC ac(IR_SEND_PIN);

enum UIMode : uint8_t { MODE_COOL = 0, MODE_HEAT = 1 };

struct Timer {
  int onH, onM;
  int offH, offM;
  uint8_t mode;
  uint8_t tempF;
  bool active;
  bool onEnabled;
  bool offEnabled;
};

Timer timers[NUM_TIMERS];
String tzConfig = "UTC0";
bool currentACState = false;
uint8_t currentMode = MODE_HEAT;
uint8_t currentTempF = 68;
bool isSetupMode = false;
unsigned long lastSendTime = 0;
unsigned long btnPressStartTime = 0;
String lastActionLog = "Waiting for WiFi...";

static int clampInt(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi ? hi : v); }
static int FtoC_rounded(uint8_t f) {
  float c = ((float)f - 32.0f) * (5.0f / 9.0f);
  return clampInt((int)lroundf(c), 16, 31);
}
static const char* modeToStr(uint8_t m) { return (m == MODE_COOL) ? "COOL" : "HEAT"; }

static void setActionLog(const String& msg) {
  struct tm ti;
  char ts[32] = "NoTime";
  if (getLocalTime(&ti)) strftime(ts, sizeof(ts), "%H:%M:%S", &ti);
  lastActionLog = String(ts) + " - " + msg;
  Serial.println(lastActionLog);
}

void saveTimers() {
  File file = LittleFS.open("/timers.dat", "w");
  if (file) { file.write((uint8_t*)timers, sizeof(timers)); file.close(); }
}

void loadTimersOrInit() {
  for (int i = 0; i < NUM_TIMERS; i++) {
    timers[i].onEnabled = true; timers[i].offEnabled = true;
    timers[i].onH = 7; timers[i].onM = 0;
    timers[i].offH = 22; timers[i].offM = 0;
    timers[i].mode = MODE_HEAT; timers[i].tempF = 68;
    timers[i].active = false;
  }
  if (LittleFS.exists("/timers.dat")) {
    File file = LittleFS.open("/timers.dat", "r");
    if (file && file.size() == sizeof(timers)) file.read((uint8_t*)timers, sizeof(timers));
    file.close();
  }
}

void applyACState(bool powerOn, uint8_t mode, uint8_t tempF, const char* reason) {
  if (isSetupMode) return;
  // Safety: Prevent rapid cycling of compressor
  if (powerOn && !currentACState && (millis() - lastSendTime < PROTECTION_DELAY_MS)) {
    setActionLog("Protect Delay Active"); return;
  }
  ac.stateReset();
  if (powerOn) ac.on(); else ac.off();
  ac.setMode(mode == MODE_COOL ? kMitsubishiAcCool : kMitsubishiAcHeat);
  ac.setTemp(FtoC_rounded(tempF));
  ac.setFan(kMitsubishiAcFanAuto);
  ac.setVane(kMitsubishiAcVaneAuto);
  ac.send();
  
  lastSendTime = millis();
  currentACState = powerOn; currentMode = mode; currentTempF = tempF;
  digitalWrite(LED_PIN, powerOn ? HIGH : LOW);
  setActionLog(String(powerOn ? "ON " : "OFF ") + modeToStr(mode) + " " + String(tempF) + "F [" + reason + "]");
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1'>
<title>AC Remote</title>
<style>
  :root { --primary: #2563eb; --success: #10b981; --danger: #ef4444; --bg: #f8fafc; --text: #1e293b; }
  body { font-family: -apple-system, system-ui, sans-serif; text-align: center; background: var(--bg); color: var(--text); margin: 0; padding: 15px; }
  .card { background: white; padding: 24px; margin: 0 auto 16px; max-width: 480px; border-radius: 16px; border: 1px solid #e2e8f0; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.05); }
  .status { font-size: 1em; font-weight: 700; padding: 12px; border-radius: 12px; margin-bottom: 16px; display: inline-block; width: 100%%; box-sizing: border-box; }
  .status-on { background: #dcfce7; color: #166534; }
  .status-off { background: #fee2e2; color: #991b1b; }
  .pill { padding: 6px 14px; border-radius: 20px; background: #f1f5f9; color: #475569; font-size: 0.9em; font-weight: 700; margin: 0 4px; border: 1px solid #e2e8f0; display: inline-block; }
  input[type=number], select { padding: 10px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 1.1em; text-align: center; background: white; }
  input::-webkit-outer-spin-button, input::-webkit-inner-spin-button { -webkit-appearance: none; margin: 0; }
  .timer-row { text-align: left; padding: 14px; border-bottom: 1px solid #f1f5f9; margin-bottom: 10px; line-height: 2.2; }
  .btn-row { display: flex; gap: 10px; margin-top: 15px; }
  button { flex: 1; padding: 18px; border: none; border-radius: 12px; font-weight: 800; cursor: pointer; font-size: 1.1em; transition: opacity 0.2s; }
  button:active { opacity: 0.7; }
  .btn-on { background: var(--success); color: white; }
  .btn-off { background: var(--danger); color: white; }
  .btn-save { background: var(--primary); color: white; width: 100%%; margin-top: 10px; }
  label { font-size: 0.75rem; font-weight: 800; color: #64748b; display: block; margin-bottom: 5px; }
</style></head>
<body>
  <h2>Climate Control</h2>
  <div class='card'>
    <div style='color:#64748b; margin-bottom:8px;'>%CURRENT_TIME%</div>
    <div class='status %STATUS_CLASS%'>SYSTEM %STATUS_TEXT%</div>
    <div style='margin-bottom:20px;'><span class='pill'>%CUR_MODE%</span><span class='pill'>%CUR_TEMP%</span></div>
    <form action="/apply" method="POST">
      <div style='display:grid; grid-template-columns: 1fr 1fr; gap:10px; text-align:left;'>
        <div><label>MODE</label><select name="mode" style='width:100%%;'><option value="cool" %SEL_COOL%>COOL</option><option value="heat" %SEL_HEAT%>HEAT</option></select></div>
        <div><label>TEMP</label><input type="number" name="tempF" min="60" max="88" value="%CUR_TEMP_VAL%" style='width:100%%;'></div>
      </div>
      <div class='btn-row'>
        <button type="submit" class='btn-on' name="power" value="on">POWER ON</button>
        <button type="submit" class='btn-off' name="power" value="off">POWER OFF</button>
      </div>
    </form>
    <div style='font-family:monospace; font-size:0.8em; margin-top:15px; opacity:0.6;'>%LAST_LOG%</div>
  </div>
  <div class='card'><h3>Scheduling</h3><form action='/save' method='POST'>%TIMERS%<button type='submit' class='btn-save'>SAVE SCHEDULES</button></form></div>
  <button style='background:none; border:none; color:#94a3b8; text-decoration:underline;' onclick="if(confirm('Restart?')) location.href='/reboot'">Restart ESP32</button>
</body></html>
)rawliteral";

String processor(const String& var) {
  if (var == "CURRENT_TIME") {
    struct tm ti; if (!getLocalTime(&ti)) return "Syncing...";
    char b[32]; strftime(b, sizeof(b), "%a %I:%M %p", &ti); return String(b);
  }
  if (var == "STATUS_TEXT") return currentACState ? "ACTIVE" : "INACTIVE";
  if (var == "STATUS_CLASS") return currentACState ? "status-on" : "status-off";
  if (var == "LAST_LOG") return lastActionLog;
  if (var == "CUR_MODE") return modeToStr(currentMode);
  if (var == "CUR_TEMP") return String(currentTempF) + "&deg;F";
  if (var == "CUR_TEMP_VAL") return String(currentTempF);
  if (var == "SEL_COOL") return (currentMode == MODE_COOL) ? "selected" : "";
  if (var == "SEL_HEAT") return (currentMode == MODE_HEAT) ? "selected" : "";

  if (var == "TIMERS") {
    String str = "";
    for (int i = 0; i < NUM_TIMERS; i++) {
      String id = String(i);
      str += "<div class='timer-row'><strong>T" + String(i + 1) + "</strong> ";
      str += "<input type='checkbox' name='act" + id + "' " + (timers[i].active ? "checked" : "") + "> Active<br>";
      str += "<input type='checkbox' name='onE" + id + "' " + (timers[i].onEnabled ? "checked" : "") + "> ON at ";
      str += "<input type='number' name='onH" + id + "' min='0' max='23' value='" + String(timers[i].onH) + "' style='width:45px'>:";
      str += "<input type='number' name='onM" + id + "' min='0' max='59' value='" + String(timers[i].onM) + "' style='width:45px'><br>";
      str += "<input type='checkbox' name='offE" + id + "' " + (timers[i].offEnabled ? "checked" : "") + "> OFF at ";
      str += "<input type='number' name='offH" + id + "' min='0' max='23' value='" + String(timers[i].offH) + "' style='width:45px'>:";
      str += "<input type='number' name='offM" + id + "' min='0' max='59' value='" + String(timers[i].offM) + "' style='width:45px'><br>";
      str += "Mode: <select name='mode" + id + "'><option value='cool' " + (timers[i].mode == MODE_COOL ? "selected":"") + ">COOL</option><option value='heat' " + (timers[i].mode == MODE_HEAT ? "selected":"") + ">HEAT</option></select> ";
      str += "Temp: <input type='number' name='tempF" + id + "' min='60' max='88' value='" + String(timers[i].tempF) + "' style='width:55px'></div>";
    }
    return str;
  }
  return "%" + var + "%";
}

void setup() {
  Serial.begin(115200); pinMode(LED_PIN, OUTPUT); pinMode(BTN_PIN, INPUT_PULLUP); ac.begin();
  lastSendTime = millis() - PROTECTION_DELAY_MS; // remove protection delay at init
  if (!LittleFS.begin(true)) Serial.println("FS Error");
  if (LittleFS.exists("/tz.txt")) { File f = LittleFS.open("/tz.txt", "r"); tzConfig = f.readString(); f.close(); }
  loadTimersOrInit();

  WiFiCreds creds = loadWiFiConfig(); IPConfig ipCfg = loadIPConfig();
  if (creds.valid) {
    if (ipCfg.isStatic) WiFi.config(ipCfg.ip, ipCfg.gateway, ipCfg.subnet);
    WiFi.begin(creds.ssid.c_str(), creds.pass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) { delay(500); Serial.print("."); }

    if (WiFi.status() == WL_CONNECTED) {
      if (MDNS.begin("hvac")) MDNS.addService("http", "tcp", 80);
      configTzTime(tzConfig.c_str(), "pool.ntp.org", "time.google.com");

      server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){ req->send_P(200, "text/html", index_html, processor); });
      
      server.on("/apply", HTTP_POST, [](AsyncWebServerRequest* req){
        bool power = false;
        if(req->hasParam("power", true)) power = (req->getParam("power", true)->value() == "on");
        
        uint8_t mode = (req->arg("mode") == "cool" ? MODE_COOL : MODE_HEAT);
        uint8_t temp = (uint8_t)req->arg("tempF").toInt();
        
        applyACState(power, mode, temp, "web");
        req->redirect("/");
      });

      server.on("/save", HTTP_POST, [](AsyncWebServerRequest* req){
        for (int i=0; i<NUM_TIMERS; i++){
          String id = String(i);
          if(req->hasParam("onH"+id, true)) timers[i].onH = clampInt(req->getParam("onH"+id, true)->value().toInt(), 0, 23);
          if(req->hasParam("onM"+id, true)) timers[i].onM = clampInt(req->getParam("onM"+id, true)->value().toInt(), 0, 59);
          if(req->hasParam("offH"+id, true)) timers[i].offH = clampInt(req->getParam("offH"+id, true)->value().toInt(), 0, 23);
          if(req->hasParam("offM"+id, true)) timers[i].offM = clampInt(req->getParam("offM"+id, true)->value().toInt(), 0, 59);
          if(req->hasParam("tempF"+id, true)) timers[i].tempF = (uint8_t)clampInt(req->getParam("tempF"+id, true)->value().toInt(), 60, 88);
          if(req->hasParam("mode"+id, true)) timers[i].mode = (req->getParam("mode"+id, true)->value() == "cool" ? MODE_COOL : MODE_HEAT);
          
          timers[i].active = req->hasParam("act"+id, true);
          timers[i].onEnabled = req->hasParam("onE"+id, true);
          timers[i].offEnabled = req->hasParam("offE"+id, true);
        }
        saveTimers(); req->redirect("/");
      });

      server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest* req){ req->send(200, "text/plain", "Rebooting..."); delay(500); ESP.restart(); });
      server.begin(); setActionLog("Online");
    } else { isSetupMode = true; startSetupMode(server); }
  } else { isSetupMode = true; startSetupMode(server); }
}

void loop() {
  if (digitalRead(BTN_PIN) == LOW) {
    if (btnPressStartTime == 0) btnPressStartTime = millis();
    else if (millis() - btnPressStartTime > RESET_HOLD_TIME_MS) { LittleFS.format(); ESP.restart(); }
  } else {
    if (btnPressStartTime > 0) {
      if (!isSetupMode && (millis() - btnPressStartTime < 5000)) applyACState(!currentACState, currentMode, currentTempF, "btn");
      btnPressStartTime = 0;
    }
  }
  if (isSetupMode) return;
  static int lastMin = -1;
  struct tm ti;
  if (getLocalTime(&ti) && ti.tm_min != lastMin) {
    lastMin = ti.tm_min;
    for (int i=0; i<NUM_TIMERS; i++) {
      if (!timers[i].active) continue;
      if (timers[i].onEnabled && ti.tm_hour == timers[i].onH && ti.tm_min == timers[i].onM) applyACState(true, timers[i].mode, timers[i].tempF, "timer");
      if (timers[i].offEnabled && ti.tm_hour == timers[i].offH && ti.tm_min == timers[i].offM) applyACState(false, currentMode, currentTempF, "timer");
    }
  }
}