#include <WiFi.h>
#include <Wire.h>
#include <Arduino.h>
#include <website_h>

#define I2C_SLAVE_ADDR 0x08 // I2C address of the slave ESP32
#define SAVE_COMMAND_BYTE 0xFF 

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

#define I2C_TRANSFER_BYTES NUMSLIDERS

int sliderValues[NUMSLIDERS] = {0};

const char *ssid = "JANUSZ";
const char *password = "12345678";

void sendSliderValuesToClient(AsyncWebSocketClient *client)
{
  Wire.requestFrom(I2C_SLAVE_ADDR, NUMSLIDERS);
  unsigned long start = millis();
  int i = 0;

  while (i < NUMSLIDERS && (millis() - start < 50))
  { // Timeout: 50ms
    if (Wire.available())
    {
      sliderValues[i] = Wire.read();
      Serial.printf("I2C value %d: %d\n", i, sliderValues[i]);
      i++;
    }
  }

  String message = "";
  for (int j = 0; j < NUMSLIDERS; j++)
  {
    message += String(sliderValues[j]);
    if (j < NUMSLIDERS - 1)
      message += ",";
  }

  if (client)
  {
    client->text(message);
  }
}

void parseAndSendValues(String message, bool triggerSave = false)
{
  int startIndex = 0, endIndex = 0, valueIndex = 0;
  uint8_t tempValues[NUMSLIDERS] = {0};

  while (valueIndex < NUMSLIDERS)
  {
    endIndex = message.indexOf(',', startIndex);
    String token = (endIndex == -1) ? message.substring(startIndex)
                                    : message.substring(startIndex, endIndex);
    startIndex = (endIndex == -1) ? message.length() : endIndex + 1;

    token.trim();
    int val = token.toInt();
    tempValues[valueIndex++] = constrain(val, 0, 255);
  }

  // Send to I2C slave
  Wire.beginTransmission(I2C_SLAVE_ADDR);
  for (int i = 0; i < NUMSLIDERS; i++)
  {
    Wire.write(tempValues[i]);
    sliderValues[i] = tempValues[i]; // Update local copy
  }

  if (triggerSave)
  {
    Wire.write(SAVE_COMMAND_BYTE); // Send one extra byte to signal "save"
  }

  Wire.endTransmission();

  // Broadcast to all clients
  String outMsg = "";
  for (int j = 0; j < NUMSLIDERS; j++)
  {
    outMsg += String(sliderValues[j]);
    if (j < NUMSLIDERS - 1)
      outMsg += ",";
  }
  ws.textAll(outMsg);
}

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    Serial.printf("Client %u connected\n", client->id());

    // Delay before sending I2C values to allow UI to fully load
    AsyncWebSocketClient *targetClient = client;

    xTaskCreate(
        [](void *param)
        {
          AsyncWebSocketClient *c = (AsyncWebSocketClient *)param;
          delay(1000);                 // Wait for frontend to initialize
          sendSliderValuesToClient(c); // Read from slave and send to client
          vTaskDelete(NULL);           // End task
        },
        "InitSliderSender",   // Task name
        4096,                 // Stack size
        (void *)targetClient, // Parameter
        1,                    // Priority
        NULL                  // No handle needed
    );
  }
  else if (type == WS_EVT_DATA)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;

    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
    {
      String message = String((char *)data);

      if (message.startsWith("SAVE:")) {
        message.remove(0, 5); // Strip "SAVE:" prefix
        parseAndSendValues(message, true); // true = trigger save
      } else {
        parseAndSendValues(message); // normal update
      }
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000); // Prevent early logging crash

  WiFi.softAP(ssid, password);
  Serial.println("Access Point Started");
  Serial.println(WiFi.softAPIP());

  Wire.begin(); // Master

  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send_P(200, "text/html", getFinalHTML().c_str()); });

  server.begin();
}

void loop()
{
//not much
}