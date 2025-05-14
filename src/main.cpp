#include <WiFi.h>
//#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <Arduino.h>
#include <website_h>

#define I2C_SLAVE_ADDR 0x08 // I2C address of the slave ESP32

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

#define NUMSLIDERS 14
#define I2C_TRANSFER_BYTES NUMSLIDERS 

int sliderValues[NUMSLIDERS] = {0};

const char* ssid = "ESP32-Sliders";
const char* password = "12345678";


void parseAndSendValues(String message) {
  int startIndex = 0;
  int endIndex = 0;

  // Temporary array for safe parsing
  uint8_t tempValues[NUMSLIDERS] = {0};
  int valueIndex = 0;

  while (valueIndex < NUMSLIDERS) {
    endIndex = message.indexOf(',', startIndex);

    String token;
    if (endIndex == -1) {
      token = message.substring(startIndex); // Last value
    } else {
      token = message.substring(startIndex, endIndex);
      startIndex = endIndex + 1;
    }

    token.trim(); // Remove any stray whitespace
    int val = token.toInt();

    // Clamp to byte range just in case
    if (val < 0) val = 0;
    if (val > 255) val = 255;

    tempValues[valueIndex++] = val;

    if (endIndex == -1) break; // No more commas
  }

  // Send exactly NUMSLIDERS bytes to slave
  Wire.beginTransmission(I2C_SLAVE_ADDR);
  for (int i = 0; i < NUMSLIDERS; i++) {
    Wire.write(tempValues[i]);
    sliderValues[i] = tempValues[i]; // Optional: update local copy
  }
  Wire.endTransmission();
}

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String message = String((char*)data);
      parseAndSendValues(message);
    }
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.softAP(ssid, password);
  Serial.println("Access Point Started");
  Serial.println(WiFi.softAPIP());

  Wire.begin(); // Master

  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);



  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String htmlPage = String(html);
    htmlPage.replace("%SLIDERS%", generateSliderHTML());
    request->send(200, "text/html", htmlPage);
  });

  server.begin();
}

unsigned long lastUpdate = 0;

void loop() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();

  if (now - lastUpdate > 500) {
    lastUpdate = now;

    // Safely request slider values from slave
    Wire.requestFrom(I2C_SLAVE_ADDR, NUMSLIDERS);
    int i = 0;
    while (Wire.available() && i < NUMSLIDERS) {
      sliderValues[i++] = Wire.read();
    }

    // Format message without trailing comma
    String message = "";
    for (int j = 0; j < NUMSLIDERS; j++) {
      message += String(sliderValues[j]);
      if (j < NUMSLIDERS - 1) message += ",";
    }

    // Broadcast to all WebSocket clients
    ws.textAll(message);
  }
}