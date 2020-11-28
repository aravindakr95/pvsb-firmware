/* -----------------------------------------------------
 * Photo-Voltaic Statistics Broadcaster Firmware (PVSB)
 * -----------------------------------------------------
 *
 * 2020© NIB303COM Individual Project, National Institute of Business Management (affiliated with Coventry University, England)
 * @author: Aravinda Rathnayake
 */

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

#include <ArduinoJson.h>

#define WIFI_SSID "Fibre-IoT" //todo: think way to configure wifi details on demand (wifi manager)
#define WIFI_SECRET "iot@4567"

#define LED_LOW LOW
#define LED_HIGH HIGH

//Define COM Related Variables
#define SLAVE_ID 0x01
#define MB_RX 10
#define MB_TX 11
#define RTS_PIN 9 //RS485 Direction Control
#define RS485_TRANSMIT HIGH
#define RS485_RETRIEVE LOW

const String deviceId = WiFi.macAddress();

const String fetchMode = "API"; //API, COM
const int maxErrorCount = 5;
const int requestInterval = 360; //approx. 6 (240 requests per day)

const String fetchUrl = "http://apiapp.le-pv.com:8080/api/equipDetail";
const String serialNumber = "8200331190301002";
const String associatedEmail = "f5881e4c218a42d3a01219d2adc27661@le-pv.com";

const int ledPinFault = D4;
const int ledPinTransmit = D6;

int wifiErrorCount = 0;
int httpErrorCount = 0;
int fetchErrorCount = 0;

SoftwareSerial RS485Serial(MB_RX, MB_TX); //config RS485Serial

//function prototypes
int fetchAPI();

int fetchCOM();

int sendPayload(String, String);

void checkError();

void sendError(String);

void handleOTA();

byte *getSlaveCommand(int);

byte readSlave(int);

void setup() {
    Serial.begin(115200); //initialize serial
    delay(3000);

    WiFi.mode(WIFI_STA);

    String HN = deviceId;
    HN.replace(":", "");
    HN = "EM-" + HN.substring(8);

    Serial.print("WiFi Hostname: ");
    Serial.println(HN);

    Serial.print("Device ID: ");
    Serial.println(deviceId);

    WiFi.hostname(HN);

    WiFi.begin(WIFI_SSID, WIFI_SECRET);
    Serial.begin(115200);

    Serial.println("");

    pinMode(ledPinFault, OUTPUT);
    pinMode(ledPinTransmit, OUTPUT);

    digitalWrite(ledPinFault, LED_HIGH);
    digitalWrite(ledPinTransmit, LED_HIGH);

    delay(2000);

    digitalWrite(ledPinFault, LED_LOW);
    digitalWrite(ledPinTransmit, LED_LOW);

    Serial.print(F("Connecting WiFi SSID: "));
    Serial.println(WIFI_SSID);

    digitalWrite(ledPinFault, LED_HIGH);

    int i = 0;

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);

        Serial.print(++i);
        Serial.print(' ');
        if (i >= 100) {
            ESP.restart();
        }
    }

    Serial.println('\n');
    Serial.println("WiFi connection established!");
    Serial.print(F("IPV4 Address: "));
    Serial.println(WiFi.localIP());

    handleOTA();

    digitalWrite(ledPinFault, LED_LOW);

    // COM Related Configurations
    pinMode(RTS_PIN, OUTPUT);
    RS485Serial.begin(9600);
}

int currentRound = 0;

void loop() {
    if (currentRound % requestInterval == 0) {
        int successReadsCount = 0;

        Serial.println("Looping Start...");

        if (WiFi.status() == WL_CONNECTED) {
            digitalWrite(ledPinTransmit, LED_HIGH);
            delay(200);
        }

        digitalWrite(ledPinFault, LED_LOW);
        digitalWrite(ledPinTransmit, LED_LOW);

        fetchMode == "COM" ? successReadsCount += fetchCOM() :
                successReadsCount += fetchAPI();

        if (successReadsCount == 0) {
            digitalWrite(ledPinFault, LED_HIGH);
            delay(200);
            digitalWrite(ledPinFault, LED_LOW);
            delay(200);
            digitalWrite(ledPinFault, LED_HIGH);
            delay(200);
            digitalWrite(ledPinFault, LED_LOW);
            delay(200);

            fetchErrorCount++;
        } else {
            fetchErrorCount = 0;
        }
        digitalWrite(ledPinTransmit, LED_LOW);
        checkError();

        Serial.println("Looping End...");
    }

    delay(1000); // wait second before next loop

    ArduinoOTA.handle();

    if (currentRound >= 99999) {
        currentRound = 0;
    }

    currentRound++;
}

int fetchCOM() {
    String jsonBody;

    StaticJsonDocument<400> JSONDoc;

    JsonObject results = JSONDoc.createNestedObject("results");

    JSONDoc["isOwn"] = true;
    JSONDoc["success"] = true;
    JSONDoc["messageCode"] = NULL;

    results["GRID"] = float(readSlave(0));
    results["LOAD"] = float(readSlave(1));
    results["PV"] = float(readSlave(2));
    results["ENERGY_TODAY"] = float(readSlave(3));
    results["ENERGY_TOTAL"] = float(readSlave(4));
    results["BATTERY_CAPACITY"] = String(readSlave(5));
    results["CAPACITY_CHARGE"] = float(readSlave(6));
    results["TMP"] = float(readSlave(7));
    results["bat_type"] = int(readSlave(8));
    results["FACTORY_NAME_EN"] = String(readSlave(9));
    results["EQUMODEL_NAME"] = String(readSlave(10));
    results["INV_SN"] = String(readSlave(11));

    serializeJson(JSONDoc, jsonBody);

    return sendPayload(jsonBody, "COM");
}

int fetchAPI() {
    int state = 0;

    wifiErrorCount++;

    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;

        String jsonBody;

        StaticJsonDocument<200> JSONDoc;

        JSONDoc["SN"] = serialNumber;
        JSONDoc["email"] = associatedEmail;

        serializeJson(JSONDoc, jsonBody);

        Serial.print("[HTTP](1) begin...\n");

        const bool isPosted = http.begin(client, fetchUrl);
        http.addHeader("Content-Type", "application/json");

        if (isPosted) {
            int httpCode = http.POST(jsonBody);

            if (httpCode > 0) {
                Serial.printf("[HTTP](1) POST... code: %d\n", httpCode);

                if (httpCode == HTTP_CODE_OK ||
                    httpCode == HTTP_CODE_MOVED_PERMANENTLY) {

                    const String payload = http.getString();
                    state = sendPayload(payload, "API"); // if success, state set to 1
                }

                httpErrorCount = 0;
            } else {
                Serial.printf("[HTTP](1) POST... failed, error: %s\n",
                              http.errorToString(httpCode).c_str());
            }

            Serial.print("[HTTP](1) end...\n");
            http.end();

            wifiErrorCount = 0;
        } else {
            Serial.printf("[HTTP](1) Unable to connect\n");
        }
    } else {
        Serial.print("Skipping, WiFi not available: ");
        Serial.println(wifiErrorCount);
    }

    return state;
}

int sendPayload(String payload, String fetchMode) {
    int state = 0;

    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;

        const String dataUploadUrl =
                "https://sete.brilliant-apps.club/v1/sete/pvsb/payloads?deviceId=" + deviceId + "&fetchMode=" + fetchMode;
        const String authToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6ImFyYXZpbmRhY2xvdWRAZ21haWwuY29tIiwiYWNjb3VudE51bWJlciI6NDMwMzM4MDkxMiwiaWF0IjoxNjA2NDkxNDU5fQ.DqiQY2Gplu055QkajqVnRTZUPyTp7rGb_92KNsUmr3Y";

        Serial.print("[HTTP](2) begin...\n");

        const bool isPosted = http.begin(client, dataUploadUrl);

        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + authToken);

        if (isPosted) {
            int httpCode = http.POST(payload);

            if (httpCode > 0) {
                Serial.printf("[HTTP](2) POST... code: %d\n", httpCode);
                if (httpCode == HTTP_CODE_OK ||
                    httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    state = 1;
                }

                httpErrorCount = 0;
            } else {
                Serial.printf("[HTTP](2) POST... failed, error: %s\n",
                              http.errorToString(httpCode).c_str());
            }

            Serial.print("[HTTP](2) end...\n");
            http.end();

            wifiErrorCount = 0;
        } else {
            Serial.printf("[HTTP](2) Unable to connect\n");
        }
    } else {
        Serial.print("Skipping, WiFi not available: ");
        Serial.println(wifiErrorCount);
    }

    return state;
}

void checkError() {
    if (httpErrorCount >= maxErrorCount ||
        wifiErrorCount >= maxErrorCount ||
        fetchErrorCount > maxErrorCount) {
        Serial.print("Fetch Errors: ");
        Serial.print(fetchErrorCount);
        Serial.print(" ,HTTP Errors: ");
        Serial.print(httpErrorCount);
        Serial.print(" ,WiFi Errors: ");
        Serial.print(wifiErrorCount);
        Serial.println("Restarting system...");

        sendError("MAX_ERROR_COUNT_REACHED");

        ESP.restart();
    }

    if (wifiErrorCount > 1) {
        digitalWrite(ledPinFault, LED_HIGH);
    }
}

void sendError(String error) {
    if (WiFi.status() == WL_CONNECTED) {
        String errorJson;

        WiFiClient client;
        HTTPClient http;

        const String errorUploadUrl = "https://sete.brilliant-apps.club/v1/sete/pvsb/errors?deviceId=" + deviceId;
        const String authToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6ImFyYXZpbmRhY2xvdWRAZ21haWwuY29tIiwiYWNjb3VudE51bWJlciI6NDMwMzM4MDkxMiwiaWF0IjoxNjA2NDkxNDU5fQ.DqiQY2Gplu055QkajqVnRTZUPyTp7rGb_92KNsUmr3Y";

        StaticJsonDocument<400> JSONDoc;

        JSONDoc["error"] = error;
        JSONDoc["rssi"] = WiFi.RSSI();
        JSONDoc["wifiFailCount"] = String(wifiErrorCount);
        JSONDoc["httpFailCount"] = String(httpErrorCount);

        serializeJson(JSONDoc, errorJson);

        Serial.print("[HTTP](3) begin...\n");

        httpErrorCount++;

        const bool isPosted = http.begin(client, errorUploadUrl);

        if (isPosted) {
            Serial.print("[HTTP](3) GET...\n");

            int httpCode = http.POST(errorJson);

            Serial.println(httpCode);

            if (httpCode > 0) {
                Serial.printf("[HTTP](3) GET... code: %d\n", httpCode);

                if (httpCode == HTTP_CODE_OK ||
                    httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    String payload = http.getString();
                    Serial.println(payload);
                }

                httpErrorCount = 0;
            } else {
                Serial.printf("[HTTP](3) GET... failed, error: %s\n",
                              http.errorToString(httpCode).c_str());
            }

            http.end();
            Serial.print("[HTTP](3) end...\n");
        } else {
            Serial.printf("[HTTP](3) Unable to connect\n");
        }

        wifiErrorCount = 0;
    } else {
        wifiErrorCount++;
        Serial.println("Skipping, No WiFi network");
    }
}

void handleOTA() {
    ArduinoOTA.setPasswordHash("8048dff8fe79031bfc7a0e84f539620c");

    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else { // U_FS
            type = "filesystem";
        }

        // NOTE: if updating FS this would be the place to unmount FS using FS.end()
        Serial.println("Start updating: " + type);
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
        }
    });

    ArduinoOTA.begin();

    Serial.println("OTA Ready");
    Serial.print(F("IPV4 Address: "));
    Serial.println(WiFi.localIP());
}

byte readSlave(int param) {
    digitalWrite(RTS_PIN, RS485_TRANSMIT); //init Transmit

    byte *request;
    byte readingBuffer[8];

    request = getSlaveCommand(param);

    RS485Serial.write(request, sizeof(request));
    RS485Serial.flush();

    digitalWrite(RTS_PIN, RS485_RETRIEVE); // init receive
    RS485Serial.readBytes(readingBuffer, 8);

    Serial.print("Reading: ");
    for (byte i = 0; i < 7; i++) {
        Serial.print(readingBuffer[i], HEX);
        Serial.print(" ");
    }
    Serial.print(" ==> ");
    Serial.print(readingBuffer[4]);
    Serial.println();

    delay(1000);
}

byte *getSlaveCommand(int param) {
    switch (param) {
        case 0: {
            //importEnergyW
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
            return output;
        }
        case 1: {
            //load
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x02, 0x00, 0x02, 0x65, 0xCB};
            return output;
        }
        case 2: {
            //pvGenerated
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x04, 0x00, 0x02, 0x85, 0xCA};
            return output;
        }
        case 3: {
            //energyToday
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x06, 0x00, 0x02, 0x24, 0x0A};
            break;
        }
        case 4: {
            //totalEnergy
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x08, 0x00, 0x02, 0x45, 0xC9};
            return output;
        }
        case 5: {
            //batteryCapacity
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x0A, 0x00, 0x02, 0xE4, 0x09};
            return output;
        }
        case 6: {
            //chargeCapacity
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x0C, 0x00, 0x02, 0x04, 0x08};
            return output;
        }
        case 7: {
            //inverterTemp
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x0E, 0x00, 0x02, 0xA5, 0xC8};
            return output;
        }
        case 8: {
            //batType
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x10, 0x00, 0x02, 0xC5, 0xCE};
            return output;
        }
        case 9: {
            //factoryName
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x12, 0x00, 0x02, 0x64, 0x0E};
            return output;
        }
        case 10: {
            //inverterModel
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x14, 0x00, 0x02, 0x84, 0x0F};
            return output;
        }
        case 11: {
            //inverterSN
            byte output[8] = {SLAVE_ID, 0x03, 0x00, 0x16, 0x00, 0x02, 0x25, 0xCF};
            return output;
        }
        default:
            byte output[0] = {};
    }
}