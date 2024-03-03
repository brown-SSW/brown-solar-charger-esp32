#include <Arduino.h>
#include <WiFi.h>

#include <Firebase_ESP_Client.h>

// Provide the token generation process info.
#include <addons/TokenHelper.h>

// Provide the RTDB payload printing info and other helper functions.
#include <addons/RTDBHelper.h>

#include "secret.h"

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

struct DayData {
    int WGen;
    int WUse;
    int batPercent;
    time_t time;
};
DayData dayData = { 0, 0, 0, 0 };

struct LiveData {
    boolean available;
    int WGen;
    int WUse;
    int batPercent;
    int cumulativeWhGen;
    time_t time;
};
LiveData liveData = { false, 0, 0, 0, 0, 0 };

struct MonthData {
    int WhGen;
    int WhUse;
    float hUsed;
    time_t time;
};
MonthData monthData = { 0, 0, 0.0, 0 };

unsigned long sendLiveDataPrevMillis = 0;
unsigned long sendDayDataPrevMillis = 0;
unsigned long sendMonthDataPrevMillis = 0;
unsigned long timeUpdateMillis = 0;

#include "sntp.h"
#include <time.h>
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3600 * 5;
boolean timeAvailable = false;

time_t utcTime;
int localHour;
int localMinute;

boolean dailyMessageFlag = false;

void setup()
{

    Serial.begin(115200);

    // WiFi.begin(wifi_ssid);
    WiFi.begin("router", "password"); // TODO:
    config.api_key = FIREBASE_API_KEY;
    auth.user.email = email_address;
    auth.user.password = firebase_password;
    config.database_url = FIREBASE_URL;
    config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h, it Serial prints information

    fbdo.setResponseSize(2048);

    Firebase.begin(&config, &auth);

    Firebase.reconnectWiFi(true);

    Firebase.setDoubleDigits(5);
    configTime(gmtOffset_sec, 3600, ntpServer);
}

void loop()
{
    ///////////////// get time ///////////////
    if (millis() - timeUpdateMillis > 1000) {
        timeUpdateMillis = millis();
        time_t potentialUtcTime;
        time(&potentialUtcTime);
        if (potentialUtcTime > 1679089322) { // if not connected to ntp time() returns seconds since boot, not a valid time in the future of when this code was written.
            utcTime = potentialUtcTime;
            timeAvailable = (utcTime != 0);
        }
        localHour = localtime(&utcTime)->tm_hour;
        localMinute = localtime(&utcTime)->tm_min;
        Serial.println(localHour);
        Serial.println(localMinute);
    }
    /////////////////////////////////////////

    if (timeAvailable && Firebase.ready() && (millis() - sendDayDataPrevMillis > 15000 || sendDayDataPrevMillis == 0)) {
        sendDayDataPrevMillis = millis();

        dayData.time = utcTime;

        sendFirebaseDayData(dayData);
        firebaseDeleteOldData("/data/dayData", 60, 2);
    }
    if (timeAvailable && Firebase.ready() && (millis() - sendMonthDataPrevMillis > 15000 || sendMonthDataPrevMillis == 0)) {
        sendMonthDataPrevMillis = millis();

        monthData.time = utcTime;

        sendFirebaseMonthData(monthData);
        firebaseDeleteOldData("/data/monthData", 60, 2);
    }
    if (timeAvailable && Firebase.ready() && (millis() - sendLiveDataPrevMillis > 15000 || sendLiveDataPrevMillis == 0)) {
        sendLiveDataPrevMillis = millis();

        liveData.time = utcTime;

        sendFirebaseLiveData(liveData);
        firebaseDeleteOldData("/data/liveData", 60, 2);
    }
}

boolean sendFirebaseLiveData(LiveData liveData)
{
    FirebaseJson json;

    json.set(F("available"), liveData.available);
    json.set(F("WGen"), liveData.WGen);
    json.set(F("WUse"), liveData.WUse);
    json.set(F("batPercent"), liveData.batPercent);
    json.set(F("cumulativeWhGen"), liveData.cumulativeWhGen);
    json.set(F("time"), liveData.time);

    if (!Firebase.RTDB.setJSON(&fbdo, F("/data/liveData"), &json)) {
        Serial.println("ERROR! Firebase (send liveData)");
        Serial.println(fbdo.errorReason().c_str());
        return false;
    }
    return true;
}

boolean sendFirebaseDayData(DayData dayData)
{
    FirebaseJson json;

    json.set(F("WGen"), dayData.WGen);
    json.set(F("WUse"), dayData.WUse);
    json.set(F("batPercent"), dayData.batPercent);
    json.set(F("time"), dayData.time);
    if (!Firebase.RTDB.pushJSON(&fbdo, F("/data/dayData"), &json)) {
        Serial.println("ERROR! Firebase (send dayData)");
        Serial.println(fbdo.errorReason().c_str());
        return false;
    }
    return true;
}

boolean sendFirebaseMonthData(MonthData monthData)
{
    FirebaseJson json;

    json.set(F("hUsed"), monthData.hUsed);
    json.set(F("time"), monthData.time);
    json.set(F("WhGen"), monthData.WhGen);
    json.set(F("WhUse"), monthData.WhUse);
    if (!Firebase.RTDB.pushJSON(&fbdo, F("/data/monthData"), &json)) {
        Serial.println("ERROR! Firebase (send monthData)");
        Serial.println(fbdo.errorReason().c_str());
        return false;
    }
    return true;
}

boolean firebaseDeleteOldData(String path, unsigned long interval, byte num)
{
    boolean report = true;

    for (byte i_i = 0; i_i < num; i_i++) { // run multiple times so the number of entries can be reduced not just maintained
        FirebaseJson fjson;
        QueryFilter fquery;
        fquery.orderBy("$key");
        fquery.limitToFirst(1);

        if (Firebase.RTDB.getJSON(&fbdo, path.c_str(), &fquery)) {
            fjson = fbdo.jsonObject();

            fjson.iteratorBegin();
            String key;
            String value;
            int type;

            fjson.iteratorGet(0, type, key, value);

            FirebaseJsonData timeJsonData;
            fjson.get(timeJsonData, key + "/time");
            if (!timeJsonData.success || timeJsonData.intValue < utcTime - interval) { // old data
                String nodeToDelete = path + "/" + key;
                if (!Firebase.RTDB.deleteNode(&fbdo, nodeToDelete.c_str())) {
                    Serial.println("ERROR! Firebase (delete delete)");
                    Serial.println(fbdo.errorReason());
                    report = false;
                }
            }
            fjson.iteratorEnd();
            fjson.clear();

        } else {
            // Failed to get JSON data at defined node, print out the error reason
            Serial.println("ERROR! Firebase (delete)");
            Serial.println(fbdo.errorReason());
            report = false;
        }
        fquery.clear();
    }

    return report;
}
