#include "Net.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

#include "Config.h"

namespace {
constexpr const char* PREFS_NS = "wifi";
constexpr const char* HOSTNAME = "cyd-touch-os";
constexpr uint32_t RETRY_MS = 30000;  // how often to try again while not connected
// The core gives up on a scan after 20 x 300 ms, which a full scan on this board
// can just overrun; wait for the SCAN_DONE event ourselves, a little longer.
constexpr uint32_t SCAN_TIMEOUT_MS = 15000;
constexpr const char* NTP_SERVER1 = "pool.ntp.org";
constexpr const char* NTP_SERVER2 = "time.google.com";
// Anything before this means the clock hasn't been set yet (it starts at 1970).
constexpr time_t VALID_TIME = 1700000000;  // November 2023

char ssid_[33] = "";  // 802.11 limits: 32-byte SSID, 63-char passphrase
char pass_[64] = "";
uint32_t lastAttemptMs_ = 0;

// Why the last attempt failed. Written from the Wi-Fi event task, so keep it
// a single byte.
volatile uint8_t lastReason_ = 0;
bool scanActive_ = false;
uint32_t scanStartMs_ = 0;
volatile bool scanDone_ = false;  // set from the Wi-Fi event task
bool sntpStarted_ = false;
bool timeLogged_ = false;

void onDisconnected(arduino_event_id_t, arduino_event_info_t info) {
    const uint8_t reason = info.wifi_sta_disconnected.reason;
    // ASSOC_LEAVE is our own disconnect() before a retry or scan, not a failure.
    if (reason == WIFI_REASON_ASSOC_LEAVE) return;
    lastReason_ = reason;
}

void onGotIp(arduino_event_id_t, arduino_event_info_t) {
    lastReason_ = 0;
    Serial.printf("WiFi: connected to %s, IP %s\n", ssid_, WiFi.localIP().toString().c_str());
}

void onScanDone(arduino_event_id_t, arduino_event_info_t) { scanDone_ = true; }

void startAttempt() {
    lastAttemptMs_ = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid_, pass_[0] ? pass_ : nullptr);
}
}  // namespace

namespace net {

void begin() {
    // We store the credentials ourselves; stop the SDK writing its own copy to flash.
    WiFi.persistent(false);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(onDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(onGotIp, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(onScanDone, ARDUINO_EVENT_WIFI_SCAN_DONE);

    Preferences prefs;
    prefs.begin(PREFS_NS, false);  // read-only would log an error before the first save
    if (prefs.isKey("ssid")) {
        prefs.getString("ssid", ssid_, sizeof(ssid_));
        prefs.getString("pass", pass_, sizeof(pass_));
    }
    prefs.end();

    if (!configured()) return;  // leave the radio off until a network is chosen
    Serial.printf("WiFi: connecting to %s\n", ssid_);
    startAttempt();
}

void loop() {
    static bool scanned = false;
    if (WiFi.isConnected()) {
        // SNTP runs in the background and resyncs on its own (hourly), also
        // after a reconnect, so starting it once is enough.
        if (!sntpStarted_) {
            configTzTime(cfg::TIMEZONE, NTP_SERVER1, NTP_SERVER2);
            sntpStarted_ = true;
        }
        if (!timeLogged_ && timeValid()) {
            timeLogged_ = true;
            struct tm t;
            const time_t now = time(nullptr);
            localtime_r(&now, &t);
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
            Serial.printf("Time: synced, %s\n", buf);
        }
        return;
    }
    if (!configured()) return;
    // A scan and a connection attempt can't share the radio; Settings drops
    // the attempt to scan, so pick it up again as soon as the scan is done.
    if (scanning()) {
        scanned = true;
        return;
    }
    if (scanned || (int32_t)(millis() - (lastAttemptMs_ + RETRY_MS)) >= 0) {
        scanned = false;
        startAttempt();
    }
}

bool startScan() {
    if (scanning()) return true;
    // An unfinished connection attempt makes the scan fail, so drop it;
    // loop() starts it again when the scan is done.
    if (!WiFi.isConnected()) WiFi.disconnect();
    scanDone_ = false;
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
        Serial.println("WiFi: scan failed to start");
        return false;
    }
    scanActive_ = true;
    scanStartMs_ = millis();
    return true;
}

bool scanning() {
    if (scanActive_ && (scanDone_ || millis() - scanStartMs_ >= SCAN_TIMEOUT_MS)) scanActive_ = false;
    return scanActive_;
}

void connect(const char* ssid, const char* password) {
    strlcpy(ssid_, ssid, sizeof(ssid_));
    strlcpy(pass_, password ? password : "", sizeof(pass_));

    Preferences prefs;
    prefs.begin(PREFS_NS, false);
    prefs.putString("ssid", ssid_);
    prefs.putString("pass", pass_);
    prefs.end();

    Serial.printf("WiFi: connecting to %s\n", ssid_);
    lastReason_ = 0;
    WiFi.disconnect();
    startAttempt();
}

void forget() {
    ssid_[0] = pass_[0] = 0;
    lastReason_ = 0;

    Preferences prefs;
    prefs.begin(PREFS_NS, false);
    prefs.clear();
    prefs.end();

    WiFi.disconnect(true);  // and switch the radio off
    Serial.println("WiFi: network forgotten");
}

bool configured() { return ssid_[0] != 0; }

bool connected() { return WiFi.isConnected(); }

State state() {
    if (!configured()) return State::Off;
    if (WiFi.isConnected()) return State::Connected;
    switch (lastReason_) {
        case WIFI_REASON_NO_AP_FOUND:
            return State::NotFound;
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return State::WrongPassword;
        default:
            return State::Connecting;
    }
}

const char* ssid() { return ssid_; }
const char* password() { return pass_; }

bool timeValid() { return time(nullptr) >= VALID_TIME; }

}  // namespace net
