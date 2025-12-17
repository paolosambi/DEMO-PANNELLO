/*
        _    _   _ ____   ___  ____      _    
       / \  | | | |  _ \ / _ \|  _ \    / \   
      / _ \ | | | | |_) | | | | |_) |  / _ \  
     / ___ \| |_| |  _ <| |_| |  _ <  / ___ \ 
    /_/   \_\\___/|_| \_\\___/|_| \_\/_/   \_\
                                              
           ____  _____ __  __  ___  
          |  _ \| ____|  \/  |/ _ \ 
          | | | |  _| | |\/| | | | |
          | |_| | |___| |  | | |_| |
          |____/|_____|_|  |_|\___/ 

 Description:
 * This demonstrates a combination of the following libraries two:
    - "ESP32-HUB75-MatrixPanel-DMA" to send pixel data to the physical panels in combination with its 
       in-built "VirtualMatrix" class which used to create a virtual display of chained panels, so the
       graphical effects of the Aurora demonstration can be shown on a 'bigger' grid of physical panels
       acting as one big display.

    - "GFX_Lite" to provide a simple graphics library for drawing on the virtual display.
       GFX_Lite is a fork of AdaFruitGFX and FastLED library combined together, with a focus on simplicity and ease of use.

 Instructions:
  * Use the serial input to advance through the patterns, or to toggle auto advance. Sending 'n' will advance to the next
    pattern, 'p' will go to the previous pattern. Sending 'a' will toggle auto advance on and off.

*/

#define USE_GFX_LITE 1
#include <ESP32-VirtualMatrixPanel-I2S-DMA.h>

// For animated GIF support
#include "FS.h"
#include <SPIFFS.h>
#define FORMAT_SPIFFS_IF_FAILED true

// For WiFi and NTP clock
#include <WiFi.h>
#include <time.h>

// For OTA updates
#include <ArduinoOTA.h>

// For Web Server
#include <WebServer.h>
WebServer server(80);

// For saving settings
#include <Preferences.h>
Preferences prefs;

// Global control variables
volatile bool autoAdvance = true;
int currentBrightness = 75;
volatile bool showClock = false;

// WiFi credentials - MODIFY THESE
const char* ssid = "IL TUO SSID";
const char* password = "LA TUA PASSWORD";

// NTP Configuration for Italy (CET/CEST)
const char* ntpServer = "it.pool.ntp.org";
const long  gmtOffset_sec = 3600;      // UTC+1 for CET
const int   daylightOffset_sec = 3600; // +1 hour for CEST (summer time)

/***************************************************************************************************************************/

// Step 1) Provide the size of each individual physical panel LED Matrix panel that is chained (or not) together
#define PANEL_RES_X 64 // Number of pixels wide of each INDIVIDUAL panel module. 
#define PANEL_RES_Y 64 // Number of pixels tall of each INDIVIDUAL panel module.

// Step 2) Provide details of the physical panel chaining that is in place.
#define NUM_ROWS 1 // Number of rows of chained INDIVIDUAL PANELS
#define NUM_COLS 1 // Number of INDIVIDUAL PANELS per ROW
#define PANEL_CHAIN NUM_ROWS*NUM_COLS    // total number of panels chained one to another

// Step 3) How are the panels chained together?
#define PANEL_CHAIN_TYPE CHAIN_TOP_RIGHT_DOWN

// Refer to: https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA/tree/master/examples/VirtualMatrixPanel
//      and: https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA/blob/master/doc/VirtualMatrixPanel.pdf

// Virtual Panel dimensions - our combined panel would be a square 4x4 modules with a combined resolution of 128x128 pixels
#define VPANEL_W PANEL_RES_X*NUM_COLS // Kosso: All Pattern files have had the MATRIX_WIDTH and MATRIX_HEIGHT replaced by these.
#define VPANEL_H PANEL_RES_Y*NUM_ROWS //

/***************************************************************************************************************************/

// The palettes are set to change every 60 seconds. 
int lastPattern = 0;

// PIN PER ESP32S3
#define R1 6
#define G1 7
#define BL1 15
#define R2 16
#define G2 17
#define BL2 8
#define CH_A 4
#define CH_B 5
#define CH_C 9
#define CH_D 10
#define CH_E 18 // assign to any available pin if using two panels or 64x64 panels with 1/32 scan
#define CLK 11
#define LAT 12
#define OE 13

// PIN PER ESP32
//#define R1 25
//#define G1 26
//#define BL1 27
//#define R2 14
//#define G2 12
//#define BL2 13
//#define CH_A 23
//#define CH_B 19
//#define CH_C 5
//#define CH_D 17
//#define CH_E 18 // assign to any available pin if using two panels or 64x64 panels with 1/32 scan
//#define CLK 16
//#define LAT 4
//#define OE 15
// placeholder for the matrix object
MatrixPanel_I2S_DMA *matrix = nullptr;

// placeholder for the virtual display object
VirtualMatrixPanel  *virtualDisp = nullptr;


#include "EffectsLayer.hpp" // FastLED CRGB Pixel Buffer for which the patterns are drawn
EffectsLayer effects(VPANEL_W, VPANEL_H);

#include "Drawable.hpp"
#include "Geometry.hpp"

#include "Patterns.hpp"
Patterns patterns;

/* -------------------------- Some variables -------------------------- */
unsigned long ms_current  = 0;
unsigned long ms_previous = 0;
unsigned long ms_previous_palette = 0;
unsigned long ms_animation_max_duration = 30000; // 10 seconds
unsigned long next_frame = 0;

void listPatterns();

// Clock variables
char currentTimeStr[9] = "00:00:00";
unsigned long lastClockUpdate = 0;

// Settings save delay
unsigned long lastSettingChange = 0;
bool settingsChanged = false;

// Salva impostazioni in flash
void saveSettings() {
  prefs.begin("aurora", false);
  prefs.putInt("pattern", patterns.getCurrentIndex());
  prefs.putInt("brightness", currentBrightness);
  prefs.putBool("autoAdv", autoAdvance);
  prefs.putBool("clock", showClock);
  prefs.end();
  Serial.println("Settings saved!");
}

// Carica impostazioni da flash
void loadSettings() {
  prefs.begin("aurora", true);  // read-only
  int savedPattern = prefs.getInt("pattern", 0);
  currentBrightness = prefs.getInt("brightness", 75);
  autoAdvance = prefs.getBool("autoAdv", true);
  showClock = prefs.getBool("clock", false);
  prefs.end();

  Serial.println("Settings loaded:");
  Serial.print("  Pattern: "); Serial.println(savedPattern);
  Serial.print("  Brightness: "); Serial.println(currentBrightness);
  Serial.print("  AutoAdvance: "); Serial.println(autoAdvance ? "ON" : "OFF");
  Serial.print("  Clock: "); Serial.println(showClock ? "ON" : "OFF");

  // Applica pattern
  patterns.setPattern(savedPattern);

  // Applica brightness
  int brightness = map(currentBrightness, 0, 100, 0, 255);
  matrix->setBrightness8(brightness);
}

// Marca che le impostazioni sono cambiate (salva dopo un delay)
void markSettingsChanged() {
  settingsChanged = true;
  lastSettingChange = millis();
}

// Font 5x7 più grande per orologio
const uint8_t clockFont[11][7] = {
  {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}, // 0
  {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // 1
  {0b01110, 0b10001, 0b00001, 0b00110, 0b01000, 0b10000, 0b11111}, // 2
  {0b01110, 0b10001, 0b00001, 0b00110, 0b00001, 0b10001, 0b01110}, // 3
  {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}, // 4
  {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}, // 5
  {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}, // 6
  {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}, // 7
  {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}, // 8
  {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}, // 9
  {0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000}, // :
};

// Disegna orologio nel buffer effects (prima della copia al display)
void drawClockOverlay() {
  // Aggiorna stringa ora ogni 500ms
  unsigned long now = millis();
  if (now - lastClockUpdate >= 500) {
    lastClockUpdate = now;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5)) {
      strftime(currentTimeStr, sizeof(currentTimeStr), "%H:%M:%S", &timeinfo);
    }
  }

  // Posizione centrata in basso
  int charW = 6;
  int charH = 7;
  int totalWidth = 8 * charW;
  int xStart = (VPANEL_W - totalWidth) / 2;
  int yPos = VPANEL_H - charH - 2;

  // Disegna sfondo nero nel buffer effects (area più ampia)
  CRGB black = CRGB::Black;
  for (int y = yPos - 1; y < yPos + charH + 1; y++) {
    for (int x = xStart - 2; x < xStart + totalWidth + 2; x++) {
      if (x >= 0 && x < VPANEL_W && y >= 0 && y < VPANEL_H) {
        effects.leds[y * VPANEL_W + x] = black;
      }
    }
  }

  // Disegna caratteri nel buffer effects
  CRGB white = CRGB::White;
  for (int c = 0; c < 8; c++) {
    int digit;
    char ch = currentTimeStr[c];
    if (ch >= '0' && ch <= '9') digit = ch - '0';
    else if (ch == ':') digit = 10;
    else continue;

    int xBase = xStart + c * charW;
    for (int row = 0; row < 7; row++) {
      for (int col = 0; col < 5; col++) {
        if (clockFont[digit][row] & (0b10000 >> col)) {
          int x = xBase + col;
          int y = yPos + row;
          if (x >= 0 && x < VPANEL_W && y >= 0 && y < VPANEL_H) {
            effects.leds[y * VPANEL_W + x] = white;
          }
        }
      }
    }
  }
}

// ============== WEB SERVER ==============
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Aurora Panel</title><style>";
  html += "body{font-family:Arial;background:#1a1a2e;color:#eee;margin:0;padding:20px}";
  html += "h1{color:#00d4ff;text-align:center}.container{max-width:600px;margin:0 auto}";
  html += ".card{background:#16213e;border-radius:10px;padding:20px;margin:15px 0}";
  html += ".btn{background:#0f3460;color:#fff;border:none;padding:12px 20px;margin:5px;border-radius:8px;cursor:pointer;font-size:16px}";
  html += ".btn:hover{background:#00d4ff;color:#000}";
  html += ".btn.active{background:#00ff88;color:#000;font-weight:bold;box-shadow:0 0 15px #00ff88}";
  html += ".btn-nav{background:#e94560;width:45%}.patterns{display:flex;flex-wrap:wrap;justify-content:center}";
  html += ".patterns .btn{width:calc(50% - 15px)}.slider{width:100%;margin:10px 0;height:25px}";
  html += ".status{text-align:center;font-size:20px;color:#00d4ff;font-weight:bold}";
  html += ".controls{display:flex;justify-content:space-between}";
  html += ".cam-box{text-align:center}.cam-box img{width:100%;max-width:320px;border-radius:8px;border:2px solid #00d4ff}";
  html += "</style></head><body><div class='container'>";
  html += "<h1>Aurora Panel</h1>";
  html += "<div class='card'><div class='status'>Effetto: <span id='cur'>--</span></div></div>";
  html += "<div class='card'><h3>Camera Live</h3><div class='cam-box'><img id='cam' src='http://192.168.1.100:81/stream' style='max-height:240px'></div></div>";
  html += "<div class='card'><h3>Navigazione</h3><div class='controls'>";
  html += "<button class='btn btn-nav' onclick='snd(\"/prev\")'>Prev</button>";
  html += "<button class='btn btn-nav' onclick='snd(\"/next\")'>Next</button></div><br>";
  html += "<button class='btn' id='autoBtn' onclick='snd(\"/auto\")' style='width:48%'>Auto: ON</button>";
  html += "<button class='btn' id='clockBtn' onclick='toggleClock()' style='width:48%'>Orologio: OFF</button></div>";
  html += "<div class='card'><h3>Luminosita: <span id='brVal'>75</span>%</h3>";
  html += "<input type='range' class='slider' id='br' min='0' max='100' value='75' onchange='setBr(this.value)'></div>";
  html += "<div class='card'><h3>Effetti</h3><div class='patterns' id='pl'></div></div></div>";
  html += "<script>";
  html += "var p=['JuliaSet','Matrix','Starfield','Attract','Bounce','Cube','ElectricMandala','Fire','Flock','Spiro','Radar','FlowField','Drift','Drift2','Infinity','Maze','Munch','PendulumWave','Plasma','Radar2','SimplexNoise','Spiro2','Wave','Snake','Spiral','GIF','Camera'];";
  html += "var pl=document.getElementById('pl');";
  html += "for(var i=0;i<p.length;i++){pl.innerHTML+='<button class=\"btn\" id=\"p'+i+'\" onclick=\"setP('+i+')\">'+p[i]+'</button>';}";
  html += "function snd(u){var x=new XMLHttpRequest();x.open('GET',u+(u.indexOf('?')>0?'&':'?')+'t='+Date.now(),true);x.onload=function(){setTimeout(upd,300);};x.send();}";
  html += "function setP(i){snd('/pattern?id='+i);}";
  html += "function setBr(v){document.getElementById('brVal').innerText=v;var x=new XMLHttpRequest();x.open('GET','/brightness?val='+v,true);x.send();}";
  html += "function toggleClock(){var x=new XMLHttpRequest();x.open('GET','/clock',true);x.onload=function(){console.log('Clock:'+this.responseText);setTimeout(upd,200);};x.send();}";
  html += "function upd(){var x=new XMLHttpRequest();x.open('GET','/status?t='+Date.now(),true);x.onload=function(){";
  html += "var d=JSON.parse(this.responseText);";
  html += "document.getElementById('cur').innerText=d.pattern;";
  html += "document.getElementById('autoBtn').innerText='Auto: '+(d.auto?'ON':'OFF');";
  html += "document.getElementById('autoBtn').className='btn'+(d.auto?' active':'');";
  html += "document.getElementById('clockBtn').innerText='Orologio: '+(d.clock?'ON':'OFF');";
  html += "document.getElementById('clockBtn').className='btn'+(d.clock?' active':'');";
  html += "document.getElementById('br').value=d.brightness;";
  html += "document.getElementById('brVal').innerText=d.brightness;";
  html += "for(var i=0;i<p.length;i++){var b=document.getElementById('p'+i);if(b)b.className=(i==d.patternIndex)?'btn active':'btn';}";
  html += "};x.send();}upd();setInterval(upd,5000);";
  html += "</script></body></html>";
  server.send(200, "text/html", html);
}

void handlePattern() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    patterns.setPattern(id);
    ms_previous = millis();
    markSettingsChanged();
  }
  server.send(200, "text/plain", "OK");
}

void handleNext() {
  patterns.move(1);
  ms_previous = millis();
  markSettingsChanged();
  server.send(200, "text/plain", "OK");
}

void handlePrev() {
  patterns.move(-1);
  ms_previous = millis();
  markSettingsChanged();
  server.send(200, "text/plain", "OK");
}

void handleAuto() {
  autoAdvance = !autoAdvance;
  markSettingsChanged();
  server.send(200, "text/plain", autoAdvance ? "ON" : "OFF");
}

void handleBrightness() {
  if (server.hasArg("val")) {
    currentBrightness = server.arg("val").toInt();
    int brightness = map(currentBrightness, 0, 100, 0, 255);
    matrix->setBrightness8(brightness);
    markSettingsChanged();
  }
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{\"pattern\":\"" + String(patterns.getCurrentPatternName()) + "\",";
  json += "\"patternIndex\":" + String(patterns.getCurrentIndex()) + ",";
  json += "\"auto\":" + String(autoAdvance ? "true" : "false") + ",";
  json += "\"clock\":" + String(showClock ? "true" : "false") + ",";
  json += "\"brightness\":" + String(currentBrightness) + "}";
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "application/json", json);
}

void handleClock() {
  showClock = !showClock;
  markSettingsChanged();
  Serial.print("Clock toggled: ");
  Serial.println(showClock ? "ON" : "OFF");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "text/plain", showClock ? "ON" : "OFF");
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/pattern", HTTP_GET, handlePattern);
  server.on("/next", HTTP_GET, handleNext);
  server.on("/prev", HTTP_GET, handlePrev);
  server.on("/auto", HTTP_GET, handleAuto);
  server.on("/clock", HTTP_GET, handleClock);
  server.on("/brightness", HTTP_GET, handleBrightness);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();
  Serial.println("Web server started!");
}

// Connect to WiFi
void setupWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Initialize NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("NTP time configured");

    // Setup OTA
    ArduinoOTA.setHostname("AuroraPanel");

    ArduinoOTA.onStart([]() {
      Serial.println("OTA Update starting...");
    });

    ArduinoOTA.onEnd([]() {
      Serial.println("\nOTA Update complete!");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("OTA Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();
    Serial.println("OTA Ready!");

    // Start Web Server
    setupWebServer();
  } else {
    Serial.println(" Failed to connect!");
  }
}

void setup()
{
  // Setup serial interface
  Serial.begin(115200);
  delay(250);

  // Initialize SPIFFS for GIF files
  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
      Serial.println("SPIFFS Mount Failed");
  } else {
      Serial.println("SPIFFS Mounted OK");
  }

  // Configure your matrix setup here
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);

  // Double buffer disabilitato
  mxconfig.double_buff = false;

  // custom pin mapping (if required)
  HUB75_I2S_CFG::i2s_pins _pins={R1, G1, BL1, R2, G2, BL2, CH_A, CH_B, CH_C, CH_D, CH_E, LAT, OE, CLK};
  mxconfig.gpio = _pins;

  // *** CONFIGURAZIONE ESP32S3 ***
  mxconfig.clkphase = false;  // Prova true se non funziona
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;  // Velocità ridotta per stabilità

  // Per pannelli 64x64 con 1/32 scan
  mxconfig.mx_height = 64;  // Altezza fisica del pannello

  // Pannello con righe alternate - prova driver ICN2038S
  mxconfig.driver = HUB75_I2S_CFG::ICN2038S;

  // FM6126A panels could be cloked at 20MHz with no visual artefacts
  // mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;

  // OK, now we can create our matrix object
  matrix = new MatrixPanel_I2S_DMA(mxconfig);

  // Allocate memory and start DMA display
  if( not matrix->begin() )
      Serial.println("****** !KABOOM! I2S memory allocation failed ***********");

  // let's adjust default brightness to about 75%
  matrix->setBrightness8(192);    // range is 0-255, 0 - 0%, 255 - 100%

  // create VirtualDisplay object based on our newly created dma_display object
  virtualDisp = new VirtualMatrixPanel((*matrix), NUM_ROWS, NUM_COLS, PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN_TYPE);

  Serial.println("**************** Starting Aurora Effects Demo ****************");

  Serial.print("MATRIX_WIDTH: ");  Serial.println(PANEL_RES_X*PANEL_CHAIN);
  Serial.print("MATRIX_HEIGHT: "); Serial.println(PANEL_RES_Y);

#ifdef VPANEL_W
  Serial.println("VIRTUAL PANEL WIDTH " + String(VPANEL_W));
  Serial.println("VIRTUAL PANEL HEIGHT " + String(VPANEL_H));
#endif


  listPatterns();

  // Carica impostazioni salvate
  loadSettings();
  patterns.start();

  ms_previous = millis();

  Serial.print("Starting with pattern: ");
  Serial.println(patterns.getCurrentPatternName());

  // Connect to WiFi and setup NTP
  setupWiFi();
}


char   incomingByte    = 0;
void handleSerialRead()
{
    if (Serial.available() > 0) {

        // read the incoming byte:
        incomingByte = Serial.read();

        if (incomingByte == 'n') {
            Serial.println("Going to next pattern");
            patterns.move(1);
            markSettingsChanged();
        }

        if (incomingByte == 'p') {
            Serial.println("Going to previous pattern");
            patterns.move(-1);
            markSettingsChanged();
        }

        if (incomingByte == 'a') {
            autoAdvance = !autoAdvance;
            markSettingsChanged();

            if (autoAdvance)
              Serial.println("Auto pattern advance is ON");
            else
              Serial.println("Auto pattern advance is OFF");
        }

        ms_previous = millis();
    }
} // end handleSerialRead


void loop()
{
  // Handle OTA updates
  ArduinoOTA.handle();

  // Handle Web Server
  server.handleClient();

  // Salva impostazioni 2 secondi dopo l'ultimo cambiamento
  if (settingsChanged && (millis() - lastSettingChange > 2000)) {
    saveSettings();
    settingsChanged = false;
  }

  handleSerialRead();

    ms_current = millis();

    if (ms_current - ms_previous_palette > 10000) // change colour palette evert 10 seconds
    {
      effects.RandomPalette();
      ms_previous_palette = ms_current;
    }

    if ( ((ms_current - ms_previous) > ms_animation_max_duration) && autoAdvance) 
    {

       patterns.move(1);

       ms_previous = ms_current;
    }
 
    if ( next_frame < ms_current)
      next_frame = patterns.drawFrame() + ms_current;

    // Orologio gestito automaticamente in ShowFrame() di EffectsLayer
}


void listPatterns() {
  patterns.listPatterns();
}
