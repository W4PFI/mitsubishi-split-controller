#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

struct WiFiCreds {
  String ssid;
  String pass;
  bool valid;
};

struct IPConfig {
  bool isStatic;
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
};

// Load saved WiFi credentials
WiFiCreds loadWiFiConfig() {
  WiFiCreds creds = {"", "", false};
  if (LittleFS.exists("/wifi.txt")) {
    File f = LittleFS.open("/wifi.txt", "r");
    if (f) {
      creds.ssid = f.readStringUntil('\n');
      creds.pass = f.readStringUntil('\n');
      creds.ssid.trim();
      creds.pass.trim();
      creds.valid = (creds.ssid.length() > 0);
      f.close();
    }
  }
  return creds;
}

// Load saved IP configuration
IPConfig loadIPConfig() {
  IPConfig cfg;
  cfg.isStatic = false;
  if (LittleFS.exists("/ip_cfg.dat")) {
    File f = LittleFS.open("/ip_cfg.dat", "r");
    if (f && f.size() == sizeof(IPConfig)) {
      f.read((uint8_t*)&cfg, sizeof(IPConfig));
    }
    f.close();
  }
  return cfg;
}

const char setup_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1'>
<title>AC Setup</title>
<style>
  :root { --primary: #2563eb; --bg: #f8fafc; --text: #1e293b; }
  body { font-family: -apple-system, system-ui, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; display: flex; align-items: center; justify-content: center; min-height: 100vh; }
  .card { background: white; padding: 32px; border-radius: 16px; border: 1px solid #e2e8f0; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.1); width: 100%; max-width: 400px; box-sizing: border-box; }
  h2 { margin-top: 0; font-weight: 800; letter-spacing: -0.5px; color: #0f172a; font-size: 1.5rem; }
  p { font-size: 0.95rem; color: #64748b; line-height: 1.5; margin-bottom: 24px; }
  label { font-size: 0.8em; font-weight: 700; color: #94a3b8; display: block; text-align: left; margin-bottom: 6px; text-transform: uppercase; }
  input { width: 100%; padding: 12px; margin-bottom: 15px; border: 1px solid #cbd5e1; border-radius: 10px; font-size: 1rem; box-sizing: border-box; }
  .static-fields { display: none; background: #f8fafc; padding: 15px; border-radius: 10px; margin-bottom: 20px; border: 1px solid #e2e8f0; }
  button { width: 100%; padding: 16px; background: var(--primary); color: white; border: none; border-radius: 12px; font-weight: 700; font-size: 1rem; cursor: pointer; }
</style>
<script>
  function toggleStatic() {
    var checkBox = document.getElementById("useStatic");
    var fields = document.getElementById("staticFields");
    fields.style.display = checkBox.checked ? "block" : "none";
  }
</script></head>
<body>
  <div class='card'>
    <h2>Controller Setup</h2>
    <form action='/connect' method='POST'>
      <label>WiFi SSID</label>
      <input type='text' name='ssid' required>
      <label>WiFi Password</label>
      <input type='password' name='pass'>
      <label>POSIX Timezone</label>
      <input type='text' name='tz' value='EST5EDT,M3.2.0,M11.1.0'>
      
      <div style="text-align:left; margin-bottom:15px;">
        <input type="checkbox" id="useStatic" name="isStatic" onchange="toggleStatic()" style="width:auto;"> 
        <label style="display:inline; text-transform:none;">Use Static IP</label>
      </div>

      <div id="staticFields" class="static-fields">
        <label>IP Address</label><input type="text" name="ip" placeholder="192.168.1.100">
        <label>Gateway</label><input type="text" name="gw" placeholder="192.168.1.1">
        <label>Subnet Mask</label><input type="text" name="sn" placeholder="255.255.255.0" value="255.255.255.0">
      </div>
      
      <button type='submit'>SAVE & CONNECT</button>
    </form>
  </div>
</body></html>
)rawliteral";

void startSetupMode(AsyncWebServer& server) {
  WiFi.softAP("AC-Controller-Setup");
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", setup_html);
  });

  server.on("/connect", HTTP_POST, [](AsyncWebServerRequest* req) {
    String ssid = req->arg("ssid");
    String pass = req->arg("pass");
    String tz = req->arg("tz");

    // Save WiFi & TZ
    File f = LittleFS.open("/wifi.txt", "w");
    f.println(ssid); f.println(pass); f.close();
    File f2 = LittleFS.open("/tz.txt", "w");
    f2.print(tz); f2.close();

    // Save IP Config
    IPConfig cfg;
    cfg.isStatic = req->hasParam("isStatic", true);
    if (cfg.isStatic) {
      cfg.ip.fromString(req->arg("ip"));
      cfg.gateway.fromString(req->arg("gw"));
      cfg.subnet.fromString(req->arg("sn"));
    }
    File f3 = LittleFS.open("/ip_cfg.dat", "w");
    f3.write((uint8_t*)&cfg, sizeof(IPConfig));
    f3.close();

    req->send(200, "text/plain", "Saved. Rebooting...");
    delay(2000);
    ESP.restart();
  });
  
  server.begin();
}
#endif