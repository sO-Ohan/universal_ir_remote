#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

const uint16_t kRecvPin = 14; // D5
const uint16_t kIrLed = 4;  // D2

IRrecv irrecv(kRecvPin);
IRsend irsend(kIrLed);
decode_results results;

ESP8266WebServer server(80);

const char* ssids[] = {"Yana-5G", "Yana"};
const char* password = "Ifaz@home1";

void connectToWiFi() {
  Serial.println("Scanning networks...");
  int n = WiFi.scanNetworks();
  
  String bestSSID = "";
  int bestRSSI = -1000;

  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < 2; j++) {
      if (WiFi.SSID(i) == ssids[j]) {
        if (WiFi.RSSI(i) > bestRSSI) {
          bestRSSI = WiFi.RSSI(i);
          bestSSID = ssids[j];
        }
      }
    }
  }

  if (bestSSID != "") {
    Serial.printf("Connecting to best network: %s (RSSI: %d)\n", bestSSID.c_str(), bestRSSI);
    WiFi.begin(bestSSID.c_str(), password);
  } else {
    Serial.println("Preferred networks not found. Trying Yana by default.");
    WiFi.begin("Yana", password);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // LOW turns it ON for ESP8266

  Serial.begin(115200);
  delay(1000);
  
  if (!LittleFS.begin()) {
    Serial.println("An Error has occurred while mounting LittleFS");
  }

  irrecv.enableIRIn();
  irsend.begin();

  connectToWiFi();

  // Serve static files
  server.serveStatic("/", LittleFS, "/index.html");

  // API Endpoints
  server.on("/api/remotes", HTTP_GET, []() {
    if (!LittleFS.exists("/remotes.json")) {
      server.send(200, "application/json", "[]");
      return;
    }
    File file = LittleFS.open("/remotes.json", "r");
    server.streamFile(file, "application/json");
    file.close();
  });

  server.on("/api/remotes", HTTP_POST, []() {
    File file = LittleFS.open("/remotes.json", "w");
    file.print(server.arg("plain"));
    file.close();
    server.send(200, "text/plain", "OK");
  });

  server.on("/api/learn", HTTP_GET, []() {
    String rId = server.arg("remote");
    String bId = server.arg("button");

    irrecv.resume();
    unsigned long startTime = millis();
    bool learned = false;

    while (millis() - startTime < 10000) { // 10 second timeout
      if (irrecv.decode(&results)) {
        if (results.decode_type != UNKNOWN) { // Don't save unknown noise
          learned = true;
          break;
        }
        irrecv.resume();
      }
      delay(50);
    }

    if (learned) {
      DynamicJsonDocument doc(4096);
      if (LittleFS.exists("/remotes.json")) {
        File file = LittleFS.open("/remotes.json", "r");
        deserializeJson(doc, file);
        file.close();
      } else {
        doc.to<JsonArray>();
      }

      bool found = false;
      for (JsonObject remote : doc.as<JsonArray>()) {
        if (remote["id"] == rId) {
          for (JsonObject button : remote["buttons"].as<JsonArray>()) {
            if (button["id"] == bId) {
              button["learned"] = true;
              button["ir_type"] = results.decode_type;
              button["ir_value"] = uint64ToString(results.value, 10);
              button["ir_bits"] = results.bits;
              found = true;
            }
          }
        }
      }

      if (found) {
        File file = LittleFS.open("/remotes.json", "w");
        serializeJson(doc, file);
        file.close();
        server.send(200, "text/plain", "Learned");
      } else {
        server.send(404, "text/plain", "Button not found");
      }
    } else {
      server.send(408, "text/plain", "Timeout");
    }
  });

  server.on("/api/send", HTTP_GET, []() {
    String rId = server.arg("remote");
    String bId = server.arg("button");

    DynamicJsonDocument doc(4096);
    if (LittleFS.exists("/remotes.json")) {
      File file = LittleFS.open("/remotes.json", "r");
      deserializeJson(doc, file);
      file.close();
    }

    bool found = false;
    for (JsonObject remote : doc.as<JsonArray>()) {
      if (remote["id"] == rId) {
        for (JsonObject button : remote["buttons"].as<JsonArray>()) {
          if (button["id"] == bId) {
            String irValStr = button["ir_value"];
            uint64_t irVal = strtoull(irValStr.c_str(), NULL, 10);
            int irType = button["ir_type"];
            int irBits = button["ir_bits"];

            if (irType > 0) { // Valid type
                bool success = irsend.send((decode_type_t)irType, irVal, irBits);
                found = true;
            }
          }
        }
      }
    }

    if (found) {
      server.send(200, "text/plain", "Sent");
    } else {
      server.send(404, "text/plain", "Not found or unlearned");
    }
  });

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
