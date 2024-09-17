#include "SPIFFS.h"  // Change to SPIFFS
#include "FS.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <mcp_can.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "cert.h"


// CAN Bus (HSPI) Pin Definitions
#define CAN_SCK 14
#define CAN_MISO 12
#define CAN_MOSI 13
#define CAN_CS 15

// SD Card (VSPI) Pin Definitions
#define SD_SCK 18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS 5              // VSPI CS pin for SD card

// Replace with your Wi-Fi credentials
const char* ssid = "";
const char* password = "";

//const int SPI_CS_PIN = 5;
MCP_CAN CAN(CAN_CS);  // Create a CAN object

unsigned long bytesSent = 0;
bool dataTransferStarted = false;
const int bufferSize = 8;
char buffer[bufferSize];

const char* url;

SPIClass spiSD(VSPI);  // Create a new SPIClass instance for VSPI

// Web server on port 80
WebServer server(80);

void handleUpdateFirmware() {
  if (server.hasArg("plain")) {
    String requestBody = server.arg("plain");
    StaticJsonDocument<200> doc;

    // Parse the received JSON
    DeserializationError error = deserializeJson(doc, requestBody);
    if (error) {
      Serial.println("Failed to parse JSON");
      server.send(400, "application/json", "{\"status\":\"error\"}");
      return;
    }

    // Extract the URL from the JSON
    url = doc["url"];
  
    // Attempt to download the firmware
    if (downloadFirmware(url, "/firmware.bin")) {
      server.send(200, "application/json", "{\"status\":\"success\"}");
    } else {
      server.send(500, "application/json", "{\"status\":\"error\"}");
    }
  } else {
    server.send(400, "application/json", "{\"status\":\"error\"}");
  }
}

bool downloadFirmware(const char* url, const char* filename) {
  
    Serial.println(url);
    WiFiClientSecure * client = new WiFiClientSecure;

    if (client) {
        client->setCACert(rootCACertificate);  // Set root CA certificate for HTTPS

        HTTPClient https;
        if (https.begin( * client, url)) {
            Serial.print("[HTTPS] GET...\n");
            delay(100);
            int httpCode = https.GET();
            if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_SEE_OTHER) {
              String redirectURL = https.getLocation();  // Get the new URL from the Location header
              Serial.print("Redirecting to: ");
              Serial.println(redirectURL);
              https.end();  // Close the initial connection
          
              // Begin a new connection with the redirect URL
              https.begin(* client, redirectURL);
              httpCode = https.GET();  // Make the new request
            }
            delay(100);
            if (httpCode == HTTP_CODE_OK) {
              
                Serial.println("Downloading file...");

                File file = SD.open(filename, FILE_WRITE);
                
                
                
                
                WiFiClient* stream = https.getStreamPtr();
                static uint8_t buffer[16384];
                int totalDownloaded = 0;
                unsigned long startTime = millis();
                while (https.connected() && (https.getSize() > 0 || https.getSize() == -1)) {
                    size_t size = stream->available();
                    if (size) {
                        int bytesRead = stream->readBytes(buffer, ((size > sizeof(buffer)) ? sizeof(buffer) : size));
                        file.write(buffer, bytesRead);
                        totalDownloaded += bytesRead;
                        Serial.print("Downloaded: ");
                        Serial.print(totalDownloaded);
                        Serial.println(" bytes");
                    }
                     // Break the loop when all data has been downloaded
                    if (https.getSize() != -1 && totalDownloaded >= https.getSize()) {
                        break;
                    }
                    
                }

                file.close();
                Serial.print("Download completed in ");
                Serial.print(millis() - startTime);
                Serial.println(" ms");
            } else {
                Serial.print("Error Occurred During Download: ");
                Serial.println(httpCode);
                
            }
            https.end();
        }
        delete client;
        return true;
    }
    return false;
}


void setup() {
  Serial.begin(115200);
  
   // Initialize CAN Bus on HSPI
  SPI.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);  // Initialize HSPI
  if (CAN.begin(MCP_STDEXT, CAN_500KBPS, MCP_8MHZ) == CAN_OK) {
    Serial.println("CAN Bus initialized on HSPI");
    CAN.setMode(MCP_NORMAL);  // Set CAN to normal mode
  } else {
    Serial.println("Error Initializing CAN Bus");
    while (1);
  }

  // Initialize SD Card on VSPI
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);  // Initialize VSPI for SD
  if (!SD.begin(SD_CS, spiSD)) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }
  Serial.println("SD Card initialized on VSPI");

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  // Set up server routes
  server.on("/update-firmware", HTTP_POST, handleUpdateFirmware);
  server.begin();
  Serial.println("HTTP server started");
  
}

void loop() {
  server.handleClient();
}
