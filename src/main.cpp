/* -----------------------------------------------------
 * Photo-Voltaic Statistics Broadcaster Firmware (PVSB)
 * -----------------------------------------------------
 *
 * 2020 NIB303COM Individual Project, National Institute of Business Management (affiliated with Coventry University, UK)
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

const String deviceId = WiFi.macAddress();

const String fetchMode = "API"; //API, COM
const int maxErrorCount = 5;

const int ledPinFault = D4;
const int ledPinTransmit = D6;

int wifiErrorCount = 0;
int httpErrorCount = 0;
int fetchErrorCount = 0;

SoftwareSerial swSerSDM;   //Config SoftwareSerial

//function prototypes
int fetchAPI();

int fetchCOM();

int sendCustomPayload(String);

void checkError();

void sendError(String);

void handleOTA();

void setup() {
    Serial.begin(115200); //Initialize serial
    delay(3000);

    Serial.print("---- ");

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
}

int currentRound = 0;

void loop() {
    currentRound++;

    // start looping in every 360 number of current rounds (approx. 12 mins)
    if (currentRound % 720 == 0) {
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

    delay(1000); //Wait second before next loop

    ArduinoOTA.handle();

    if (currentRound >= 99999) {
        currentRound = 0;
    }
}

int fetchCOM() {} //todo: to be implemented

int fetchAPI() {
    int state = 0;

    wifiErrorCount++;

    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;

        String jsonBody;

        const String dataFetchUrl = "http://192.168.1.8:3334/api/equipDetail";

        StaticJsonDocument<200> JSONDoc;

        JSONDoc["SN"] = "8200331190301002";
        JSONDoc["email"] = "f5881e4c218a42d3a01219d2adc27661@le-pv.com";

        serializeJson(JSONDoc, jsonBody);

        Serial.print("[HTTP](1) begin...\n");

        const bool isPosted = http.begin(client, dataFetchUrl);
        http.addHeader("Content-Type", "application/json");

        if (isPosted) {
            int httpCode = http.POST(jsonBody);

            if (httpCode > 0) {
                Serial.printf("[HTTP](1) GET... code: %d\n", httpCode);

                if (httpCode == HTTP_CODE_OK ||
                    httpCode == HTTP_CODE_MOVED_PERMANENTLY) {

                    const String payload = http.getString();
                    state = sendCustomPayload(payload); //if success, state set to 1
                }

                httpErrorCount = 0;
            } else {
                Serial.printf("[HTTP](1) GET... failed, error: %s\n",
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

int sendCustomPayload(String payload) {
    int state = 0;

    if (WiFi.status() == WL_CONNECTED) {
        String payloadJson;
        String cuPayloadJson;

        WiFiClient client;
        HTTPClient http;

        const String dataUploadUrl = "http://192.168.1.8:4000/v1/sete/pvsb/payloads?deviceId=" + deviceId;
        const String authToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6ImFyYXZpbmRhY2xvdWRAZ21haWwuY29tIiwic3VwcGxpZXIiOiJDRUIiLCJhY2NvdW50TnVtYmVyIjo0MzAzMTgwOTMxLCJpYXQiOjE2MDI1MDYzNzN9.u0bcQN2bpPWKBxrBxUrtV4l3vQcBqjfRD8Wi6ObiDow";

//        StaticJsonDocument<1000> PayloadDoc;
//        DeserializationError error = deserializeJson(PayloadDoc, payload);

//        if (error) {
//            Serial.print(F("deserializeJson() failed: "));
//            Serial.println(error.c_str());
//            return 0;
//        }

//        StaticJsonDocument<1000> JSONDoc;

//        JSONDoc["deviceId"] = deviceId;
//        JSONDoc["snapshotTimestamp"] = PayloadDoc["TIME"];
//        JSONDoc["load"] = PayloadDoc["LOAD"];
//        JSONDoc["pv"] = PayloadDoc["PV"];
//        JSONDoc["energyToday"] = PayloadDoc["ENERGY_TODAY"];
//        JSONDoc["totalEnergy"] = PayloadDoc["ENERGY_TOTAL"];
//        JSONDoc["importEnergy"] = PayloadDoc["GRID"];
//        JSONDoc["batteryCapacity"] = float(PayloadDoc["BATTERY_CAPACITY"]);
//        JSONDoc["chargeCapacity"] = PayloadDoc["CAPACITY_CHARGE"];
//        JSONDoc["inverterTemp"] = PayloadDoc["TMP"];
//
//        JSONDoc["batType"] = PayloadDoc["bat_type"];
//        JSONDoc["batteryStatus"] = PayloadDoc["bat_type"] ? true : false;
//
//        JSONDoc["factoryName"] = PayloadDoc["FACTORY_NAME_EN"];
//        JSONDoc["inverterModel"] = PayloadDoc["EQUMODEL_NAME"];
//        JSONDoc["inverterSerial"] = PayloadDoc["INV_SN"];
//
//        serializeJson(JSONDoc, cuPayloadJson);

        Serial.print("[HTTP](2) begin...\n");

        const bool isPosted = http.begin(client, dataUploadUrl);

        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + authToken);

        if (isPosted) {
            int httpCode = http.POST(payload);

            if (httpCode > 0) {
                Serial.printf("[HTTP](2) GET... code: %d\n", httpCode);
                if (httpCode == HTTP_CODE_OK ||
                    httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    state = 1;
                }

                httpErrorCount = 0;
            } else {
                Serial.printf("[HTTP](2) GET... failed, error: %s\n",
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

        const String errorUploadUrl = "http://192.168.1.8:4000/v1/sete/pvsb/errors";
        const String authToken = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJlbWFpbCI6ImFyYXZpbmRhY2xvdWRAZ21haWwuY29tIiwic3VwcGxpZXIiOiJDRUIiLCJhY2NvdW50TnVtYmVyIjo0MzAzMTgwOTMxLCJpYXQiOjE2MDI1MDYzNzN9.u0bcQN2bpPWKBxrBxUrtV4l3vQcBqjfRD8Wi6ObiDow";

        StaticJsonDocument<400> JSONDoc;

        JSONDoc["deviceId"] = deviceId;
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