#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP085.h>
#include <math.h>

const char* WIFI_SSID = "your_wifi_ssid";
const char* WIFI_PASSWORD = "your_wifi_password";


const char* AI_SERVER_URL =
  "http://10.104.8.80:5000/ask";


constexpr uint8_t ZONE_PINS[3] = {32, 33, 25};
constexpr uint8_t ZONE_ACTIVE_LEVEL[3] = {LOW, LOW, LOW};

constexpr uint8_t LDR_PIN = 34;
constexpr uint8_t GREEN_LED_PIN = 4;

// Change LOW to HIGH if light status is reversed.
constexpr uint8_t LDR_LIGHT_LEVEL = LOW;

// Demonstration threshold only.
constexpr float TEMP_WARNING_C = 40.0f;

constexpr uint32_t SYMBOL_MS = 100;
constexpr uint32_t BMP_INTERVAL_MS = 1000;
constexpr uint32_t BMP_STALE_MS = 5000;
constexpr uint32_t OLED_INTERVAL_MS = 300;

// ================= OBJECTS =================

WebServer server(80);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_BMP085 bmp;

// ================= VARIABLES =================

bool oledReady = false;
bool bmpReady = false;
bool bmpValid = false;
bool zones[3] = {false, false, false};

int ldrRaw = HIGH;
bool lightDetected = false;

float temperatureC = NAN;
float pressureHPa = NAN;

uint32_t lastBMPRead = 0;
uint32_t lastValidBMP = 0;
uint32_t lastOLEDUpdate = 0;

String pendingMessage;
String completedMessage;
String txStatus = "Idle";

bool txActive = false;
bool showCompletedMessage = false;

uint8_t txPacket[64];
size_t txPacketLength = 0;
size_t txSymbolIndex = 0;
uint32_t lastTxStep = 0;

// ================= JSON HELPERS =================

String jsonEscape(const String& input) {
  String output;
  output.reserve(input.length() + 16);

  for (size_t i = 0; i < input.length(); i++) {
    unsigned char c = input[i];

    if (c == 34 || c == 92) {
      output += char(92);
      output += char(c);
    } else if (c == 10) {
      output += char(92);
      output += 'n';
    } else if (c == 13) {
      output += char(92);
      output += 'r';
    } else if (c == 9) {
      output += char(92);
      output += 't';
    } else if (c < 32) {
      output += ' ';
    } else {
      output += char(c);
    }
  }

  return output;
}

void jsonKey(String& json, const char* key) {
  json += char(34);
  json += key;
  json += char(34);
  json += ':';
}

void jsonText(String& json, const String& text) {
  json += char(34);
  json += jsonEscape(text);
  json += char(34);
}

// ================= STATUS =================

bool bmpFresh() {
  return bmpValid &&
         millis() - lastValidBMP <= BMP_STALE_MS;
}

bool temperatureWarning() {
  return bmpFresh() && temperatureC >= TEMP_WARNING_C;
}

String monitorStatus() {
  if (!bmpReady || !bmpValid) return "BMP180 UNAVAILABLE";
  if (!bmpFresh()) return "BMP180 DATA STALE";
  if (temperatureWarning()) return "TEMPERATURE WARNING";
  return "NO CONFIGURED WARNING";
}

String dashboardIP() {
  if (WiFi.getMode() == WIFI_AP) {
    return WiFi.softAPIP().toString();
  }

  return WiFi.localIP().toString();
}

// ================= SENSOR INPUTS =================

void readDigitalInputs() {
  for (int i = 0; i < 3; i++) {
    zones[i] =
      digitalRead(ZONE_PINS[i]) == ZONE_ACTIVE_LEVEL[i];
  }

  ldrRaw = digitalRead(LDR_PIN);
  lightDetected = ldrRaw == LDR_LIGHT_LEVEL;
}

void readBMP() {
  if (!bmpReady) {
    bmpValid = false;
    return;
  }

  float t = bmp.readTemperature();
  int32_t p = bmp.readPressure();

  bmpValid =
    isfinite(t) &&
    t >= -40.0f && t <= 85.0f &&
    p >= 30000 && p <= 110000;

  if (bmpValid) {
    temperatureC = t;
    pressureHPa = p / 100.0f;
    lastValidBMP = millis();
  } else {
    temperatureC = NAN;
    pressureHPa = NAN;
  }
}

// ================= OLED DISPLAY =================

void printZonesOLED() {
  display.print("Z1:");
  display.print(zones[0] ? "1 " : "0 ");
  display.print("Z2:");
  display.print(zones[1] ? "1 " : "0 ");
  display.print("Z3:");
  display.print(zones[2] ? "1" : "0");
}

void updateOLED() {
  if (!oledReady) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  if (txActive) {
    // Do not show the typed message during transmission.
    display.setCursor(0, 0);
    display.println("OPTICAL TRANSMISSION");
    display.setCursor(0, 18);
    display.println("TRANSMITTING...");
    display.setCursor(0, 34);
    display.println("Please wait");
    display.setCursor(0, 50);
    display.println("Message after TX");

  } else if (showCompletedMessage) {
    // Message appears only after the LED sequence finishes.
    display.setCursor(0, 0);
    display.println("TRANSMISSION COMPLETE");
    display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

    display.setCursor(0, 18);
    display.print(completedMessage.substring(0, 21));

    display.setCursor(0, 30);
    display.print(completedMessage.substring(21));

    display.setCursor(0, 44);
    printZonesOLED();

    display.setCursor(0, 55);

    if (!bmpFresh()) display.print("CHECK BMP DATA");
    else if (temperatureWarning()) display.print("TEMP WARNING");
    else display.print("MESSAGE COMPLETED");

  } else {
    display.setCursor(0, 0);
    display.println("UNDERGROUND SAFETY");

    display.setCursor(0, 12);
    printZonesOLED();

    display.setCursor(0, 24);

    if (bmpFresh()) {
      display.print("T:");
      display.print(temperatureC, 1);
      display.print("C P:");
      display.print(pressureHPa, 0);
    } else {
      display.print("BMP DATA UNAVAILABLE");
    }

    display.setCursor(0, 36);
    display.print("LDR:");
    display.print(ldrRaw);
    display.print(lightDetected ? " LIGHT" : " NO LIGHT");

    display.setCursor(0, 48);
    display.print("Waiting for message");
  }

  display.display();
}

// ================= GREEN LED TRANSMISSION =================

void writeTxSymbol() {
  size_t byteIndex = txSymbolIndex / 10;
  uint8_t position = txSymbolIndex % 10;

  bool level;

  // Per byte: start LOW, 8 data bits LSB-first, stop HIGH.
  if (position == 0) level = LOW;
  else if (position == 9) level = HIGH;
  else level = (txPacket[byteIndex] >> (position - 1)) & 1;

  digitalWrite(GREEN_LED_PIN, level);
}

void startTransmission(const String& message) {
  pendingMessage = message;
  showCompletedMessage = false;

  // Header + payload length + payload + XOR checksum.
  txPacket[0] = 0xAA;
  txPacket[1] = 0x55;
  txPacket[2] = (uint8_t)message.length();

  uint8_t checksum = txPacket[2];

  for (size_t i = 0; i < message.length(); i++) {
    txPacket[3 + i] = (uint8_t)message[i];
    checksum ^= (uint8_t)message[i];
  }

  txPacket[3 + message.length()] = checksum;
  txPacketLength = message.length() + 4;

  txSymbolIndex = 0;
  txActive = true;
  txStatus = "Transmitting; OLED message pending";

  // Draw waiting screen before starting timed signalling.
  updateOLED();

  lastTxStep = millis();
  writeTxSymbol();
}

void serviceTransmission() {
  if (!txActive) return;

  uint32_t now = millis();
  if (now - lastTxStep < SYMBOL_MS) return;

  lastTxStep = now;
  txSymbolIndex++;

  if (txSymbolIndex >= txPacketLength * 10) {
    digitalWrite(GREEN_LED_PIN, LOW);
    txActive = false;

    // Commit only after the full transmission completes.
    completedMessage = pendingMessage;
    pendingMessage = "";
    showCompletedMessage = true;

    txStatus =
      "Transmission complete; OLED updated; RX not verified";

    readDigitalInputs();

    // Immediately show the message after turning the LED off.
    updateOLED();
    lastOLEDUpdate = millis();

    Serial.print("Completed OLED message: ");
    Serial.println(completedMessage);
    return;
  }

  writeTxSymbol();
}

// ================= SENSOR JSON =================

void handleData() {
  String json;
  json.reserve(1100);

  bool fresh = bmpFresh();

  json = "{";

  jsonKey(json, "uptime_ms");
  json += String(millis());

  json += ',';
  jsonKey(json, "zone1");
  json += (zones[0] ? "true" : "false");

  json += ',';
  jsonKey(json, "zone2");
  json += (zones[1] ? "true" : "false");

  json += ',';
  jsonKey(json, "zone3");
  json += (zones[2] ? "true" : "false");

  json += ',';
  jsonKey(json, "bmp_valid");
  json += (fresh ? "true" : "false");

  json += ',';
  jsonKey(json, "temp");
  json += fresh ? String(temperatureC, 1) : String("null");

  json += ',';
  jsonKey(json, "pressure");
  json += fresh ? String(pressureHPa, 1) : String("null");

  json += ',';
  jsonKey(json, "bmp_age_ms");
  json += bmpValid
    ? String(millis() - lastValidBMP)
    : String("null");

  json += ',';
  jsonKey(json, "ldr");
  json += String(ldrRaw);

  json += ',';
  jsonKey(json, "ldr_type");
  jsonText(json, "digital");

  json += ',';
  jsonKey(json, "light_detected");
  json += (lightDetected ? "true" : "false");

  json += ',';
  jsonKey(json, "warning");
  json += (temperatureWarning() ? "true" : "false");

  json += ',';
  jsonKey(json, "status");
  jsonText(json, monitorStatus());

  json += ',';
  jsonKey(json, "lifiTx");
  jsonText(json, completedMessage);

  json += ',';
  jsonKey(json, "pending_message");
  jsonText(json, pendingMessage);

  json += ',';
  jsonKey(json, "tx_status");
  jsonText(json, txStatus);

  json += ',';
  jsonKey(json, "tx_busy");
  json += (txActive ? "true" : "false");

  json += ',';
  jsonKey(json, "tx_progress");

  if (txActive && txPacketLength > 0) {
    json += String(
      (unsigned int)(
        txSymbolIndex * 100UL / (txPacketLength * 10UL)
      )
    );
  } else {
    json += (completedMessage.length() > 0 ? "100" : "0");
  }

  json += ',';
  jsonKey(json, "temp_warning_c");
  json += String(TEMP_WARNING_C, 1);

  json += '}';

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

// ================= MESSAGE REQUEST =================

void handleSend() {
  if (txActive) {
    server.send(409, "text/plain", "Transmitter busy. Please wait.");
    return;
  }

  if (!server.hasArg("msg")) {
    server.send(400, "text/plain", "Message missing.");
    return;
  }

  String message = server.arg("msg");
  message.trim();

  if (message.length() == 0 || message.length() > 25) {
    server.send(
      400, "text/plain",
      "Enter 1 to 25 printable ASCII characters."
    );
    return;
  }

  for (size_t i = 0; i < message.length(); i++) {
    uint8_t c = message[i];

    if (c < 32 || c > 126) {
      server.send(
        400, "text/plain",
        "Use printable English/ASCII characters."
      );
      return;
    }
  }

  startTransmission(message);

  server.send(
    202, "text/plain",
    "Transmission started. OLED will show the message after completion."
  );
}

// ================= EMBEDDED WEB DASHBOARD =================

const char DASHBOARD[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LiFi Underground Safety</title>
<style>
*{box-sizing:border-box}
body{margin:0;background:#090e09;color:#e9eee5;font-family:Arial,sans-serif}
main{max-width:1250px;margin:auto;padding:22px}
header{display:flex;justify-content:space-between;align-items:center;
gap:15px;border-bottom:1px solid #35452c;padding-bottom:20px}
h1{margin:0;font-size:23px;color:#e7bd64}
small,.sub{font-size:12px;color:#a8b39d;line-height:1.6}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;margin-top:20px}
.card{min-width:0;padding:18px;border:1px solid #35472c;border-radius:15px;
background:linear-gradient(145deg,#1b2618,#10180f);box-shadow:0 8px 24px #0004}
h3{font-size:12px;color:#dec17b;text-transform:uppercase;letter-spacing:1px}
.value{font-size:25px;margin:14px 0;overflow-wrap:anywhere}
.good{color:#75e5a3}.warn{color:#ffac78}
.full{grid-column:1/-1}.wide{grid-column:span 2}
canvas{display:block;width:100%;height:170px}
input{width:100%;padding:12px;margin:8px 0;background:#0a1109;color:#eee;
border:1px solid #53653d;border-radius:8px}
button{padding:11px 15px;border:0;border-radius:8px;
background:#e7bd64;color:#171e0f;cursor:pointer;font-weight:bold}
button:disabled{opacity:.5;cursor:wait}
.output{padding:13px;background:#0a1109;border-radius:9px;margin:12px 0;
white-space:pre-wrap;overflow-wrap:anywhere;line-height:1.6}
progress{width:100%;height:16px;accent-color:#e7bd64}
#events{max-height:170px;overflow:auto;font-size:13px}
#events div{padding:5px 0;border-bottom:1px solid #283720}
@media(max-width:760px){
.grid{grid-template-columns:1fr}.wide{grid-column:auto}
header{align-items:flex-start;flex-direction:column}}
</style>
</head>
<body>
<main>
<header>
<div>
<h1>UNDERGROUND SAFETY COMMAND CENTER</h1>
<div class="sub">ESP32 monitoring · Digital LDR · Optical TX · AI advisory</div>
</div>
<div id="connection" class="warn">CONNECTING</div>
</header>

<div class="grid">
<section class="card">
<h3>Zone 1 / TCRT5000</h3><div id="z1" class="value">--</div>
<small>Nearby object detection</small>
</section>

<section class="card">
<h3>Zone 2 / IR 1</h3><div id="z2" class="value">--</div>
<small>Nearby object detection</small>
</section>

<section class="card">
<h3>Zone 3 / IR 2</h3><div id="z3" class="value">--</div>
<small>Nearby object detection</small>
</section>

<section class="card">
<h3>Temperature</h3><div id="temperature" class="value">--</div>
<canvas id="tc"></canvas><small>Recent temperature trend</small>
</section>

<section class="card">
<h3>Atmospheric pressure</h3><div id="pressure" class="value">--</div>
<canvas id="pc"></canvas><small>Not gas or collapse detection</small>
</section>

<section class="card">
<h3>Digital LDR</h3><div id="light" class="value">--</div>
<div id="raw" class="sub">Raw state: --</div>
<canvas id="lc"></canvas>
<small>Approximately 1-second digital samples; not a complete pulse waveform.</small>
</section>

<section class="card">
<h3>Monitor status</h3><div id="monitor" class="value">--</div>
<small>Laboratory prototype. No warning does not mean the mine is safe.</small>
</section>

<section class="card wide">
<h3>Green LED transmission</h3>
<input id="message" maxlength="25" placeholder="Up to 25 English/ASCII characters">
<button id="sendButton" onclick="sendMessage()">Transmit message</button>
<div id="transmission" class="output">Idle</div>
<progress id="progress" max="100" value="0"></progress>
<h3>Last completed / OLED message</h3>
<div id="completed" class="output">No completed message.</div>
<small>OLED message appears after transmission finishes. Reception is not verified.</small>
</section>

<section class="card full">
<h3>AI Monitoring Assistant</h3>
<small>Requires laptop Python + Ollama. Read-only assistant.</small>
<input id="question" maxlength="300" placeholder="Ask about current readings">
<button id="askButton" onclick="askAI()">Ask AI</button>
<div id="answer" class="output">Ask a question about the readings.</div>
<small>AI may make mistakes. Verify important information.</small>
</section>

<section class="card full">
<h3>Browser session events</h3><div id="events"></div>
</section>
</div>
</main>

<script>
const AI_URL = __AI_URL__;
const samples = {tc:[],pc:[],lc:[]};
let previousStatus = null;
let previousZones = null;
let previousTx = null;
let sendRequestActive = false;

async function timedFetch(url,options={},timeoutMs=4000){
 const controller=new AbortController();
 const timer=setTimeout(()=>controller.abort(),timeoutMs);
 try{return await fetch(url,{...options,signal:controller.signal});}
 finally{clearTimeout(timer);}
}

function addLog(message){
 const row=document.createElement("div");
 row.textContent=new Date().toLocaleTimeString()+" — "+message;
 const parent=document.getElementById("events");
 parent.prepend(row);
 while(parent.children.length>60)parent.lastChild.remove();
}

function plot(id,value,color,digital=false){
 const data=samples[id];
 data.push(value);
 if(data.length>60)data.shift();

 const c=document.getElementById(id);
 const w=c.getBoundingClientRect().width,h=170;
 const ratio=window.devicePixelRatio||1;
 c.width=Math.max(1,Math.round(w*ratio));
 c.height=Math.round(h*ratio);

 const g=c.getContext("2d");
 g.scale(ratio,ratio);
 g.strokeStyle="#2d3d25";
 g.lineWidth=1;

 for(let i=1;i<5;i++){
  g.beginPath();g.moveTo(0,i*h/5);g.lineTo(w,i*h/5);g.stroke();
 }

 const valid=data.filter(Number.isFinite);
 if(!valid.length)return;

 let low=0,high=1;
 if(!digital){
  low=Math.min(...valid);high=Math.max(...valid);
  const pad=Math.max((high-low)*.15,.5);
  low-=pad;high+=pad;
 }

 g.fillStyle="#a9b69a";g.font="11px Arial";
 g.fillText(digital?"1":high.toFixed(1),4,12);
 g.fillText(digital?"0":low.toFixed(1),4,h-4);
 g.strokeStyle=color;g.lineWidth=2;g.beginPath();

 let started=false,oldY=0;
 data.forEach((v,i)=>{
  if(!Number.isFinite(v)){started=false;return;}
  const x=i*w/Math.max(data.length-1,1);
  const y=20+(h-40)*(1-(v-low)/(high-low));
  if(!started)g.moveTo(x,y);
  else if(digital){g.lineTo(x,oldY);g.lineTo(x,y);}
  else g.lineTo(x,y);
  oldY=y;started=true;
 });
 g.stroke();
}

async function pollData(){
 try{
  const response=await timedFetch("/data",{cache:"no-store"});
  if(!response.ok)throw Error("Telemetry failed");
  const d=await response.json();

  const connection=document.getElementById("connection");
  connection.textContent="LIVE";connection.className="good";

  const zones=[d.zone1,d.zone2,d.zone3];
  zones.forEach((v,i)=>{
   const e=document.getElementById("z"+(i+1));
   e.textContent=v?"DETECTED":"NO DETECTION";
   e.className="value "+(v?"good":"");
   if(previousZones&&previousZones[i]!==v)
    addLog("Zone "+(i+1)+": "+(v?"object detected":"no detection"));
  });
  previousZones=zones;

  document.getElementById("temperature").textContent=
   d.bmp_valid?d.temp+" °C":"Unavailable";
  document.getElementById("pressure").textContent=
   d.bmp_valid?d.pressure+" hPa":"Unavailable";
  document.getElementById("light").textContent=
   d.light_detected?"LIGHT DETECTED":"NO LIGHT DETECTED";
  document.getElementById("raw").textContent="Raw digital state: "+d.ldr;

  const monitor=document.getElementById("monitor");
  monitor.textContent=d.status;
  monitor.className="value "+(!d.bmp_valid||d.warning?"warn":"good");

  document.getElementById("transmission").textContent=
   d.tx_status+(d.tx_busy?" ("+d.tx_progress+"%)":"");
  document.getElementById("completed").textContent=
   d.lifiTx||"No completed message.";
  document.getElementById("progress").value=d.tx_progress;
  document.getElementById("sendButton").disabled=d.tx_busy||sendRequestActive;

  plot("tc",d.temp,"#e6b65d");
  plot("pc",d.pressure,"#72dc9b");
  plot("lc",d.ldr,"#d8df88",true);

  if(d.status!==previousStatus){addLog(d.status);previousStatus=d.status;}
  if(previousTx!==null&&d.tx_status!==previousTx)addLog(d.tx_status);
  previousTx=d.tx_status;

 }catch(error){
  const connection=document.getElementById("connection");
  connection.textContent="DATA STALE / CONNECTION LOST";
  connection.className="warn";
  const monitor=document.getElementById("monitor");
  monitor.textContent="CURRENT CONDITIONS UNKNOWN";
  monitor.className="value warn";
 }finally{setTimeout(pollData,1000);}
}

async function sendMessage(){
 const input=document.getElementById("message");
 const message=input.value.trim();
 if(!message||sendRequestActive)return;

 sendRequestActive=true;
 document.getElementById("sendButton").disabled=true;

 try{
  const r=await timedFetch("/send",{
   method:"POST",
   headers:{"Content-Type":"application/x-www-form-urlencoded"},
   body:new URLSearchParams({msg:message})
  },5000);

  const result=await r.text();
  addLog(result);
  if(r.ok)input.value="";
 }catch(error){addLog("Message request failed.");}
 finally{sendRequestActive=false;}
}

async function askAI(){
 const question=document.getElementById("question").value.trim();
 if(!question)return;

 const button=document.getElementById("askButton");
 const output=document.getElementById("answer");
 button.disabled=true;
 output.textContent="Fetching readings and generating an answer...";

 try{
  const r=await timedFetch(AI_URL,{
   method:"POST",
   headers:{"Content-Type":"application/json"},
   body:JSON.stringify({question})
  },125000);

  const result=await r.json();
  if(!r.ok)throw Error(result.error||"AI service error");

  output.textContent=
   result.answer+String.fromCharCode(10,10)+result.note;
 }catch(error){
  output.textContent="AI unavailable: "+error.message+
   String.fromCharCode(10)+
   "Check Python, Ollama, laptop IP and firewall.";
 }finally{button.disabled=false;}
}

pollData();
</script>
</body>
</html>
)HTML";

// ================= ROOT PAGE =================

void handleRoot() {
  String page = FPSTR(DASHBOARD);
  String aiURL;

  jsonText(aiURL, String(AI_SERVER_URL));
  page.replace("__AI_URL__", aiURL);

  server.send(200, "text/html; charset=utf-8", page);
}

// ================= WIFI =================

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting");

  uint32_t start = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 20000
  ) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
  } else {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP("LiFi-Safety-Demo", "MineDemo123");

    Serial.println("Fallback WiFi: LiFi-Safety-Demo");
    Serial.println("Password: MineDemo123");
    Serial.println(
      "AI needs laptop connection and matching IP settings."
    );
  }

  Serial.print("Dashboard: http://");
  Serial.println(dashboardIP());
}

// ================= SETUP =================

void setup() {
  Serial.begin(115200);

  pinMode(GREEN_LED_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, LOW);

  for (int i = 0; i < 3; i++) {
    pinMode(ZONE_PINS[i], INPUT);
  }

  // Digital input. GPIO34 has no internal pull-up.
  pinMode(LDR_PIN, INPUT);

  Wire.begin(21, 22);
  Wire.setTimeOut(50);

  oledReady = display.begin(
    SSD1306_SWITCHCAPVCC, 0x3C
  );

  if (oledReady) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("UNDERGROUND SAFETY");
    display.println("Starting...");
    display.display();
  } else {
    Serial.println("OLED unavailable");
  }

  bmpReady = bmp.begin();
  if (!bmpReady) Serial.println("BMP180 unavailable");

  readDigitalInputs();
  readBMP();
  connectWiFi();

  if (oledReady) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("DASHBOARD IP:");
    display.println(dashboardIP());
    display.println();
    display.println("Open in browser");
    display.display();
    delay(2500);
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/send", HTTP_POST, handleSend);

  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("Web server started");

  lastBMPRead = millis();
  updateOLED();
}

// ================= LOOP =================

void loop() {
  serviceTransmission();
  server.handleClient();
  readDigitalInputs();

  uint32_t now = millis();

  // Pause I2C work during software-timed optical transmission.
  if (
    !txActive &&
    now - lastBMPRead >= BMP_INTERVAL_MS
  ) {
    lastBMPRead = now;
    readBMP();
  }

  if (
    !txActive &&
    now - lastOLEDUpdate >= OLED_INTERVAL_MS
  ) {
    lastOLEDUpdate = now;
    updateOLED();
  }
}
