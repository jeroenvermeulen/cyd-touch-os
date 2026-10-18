#pragma once

#include <stdint.h>

// Wi-Fi connection shared by the whole firmware. The credentials picked in
// Settings are kept in NVS; begin() connects with them in the background at
// startup and loop() keeps retrying while the network is unreachable.
//
// Apps that need the network check net::connected() and use the normal
// WiFi/HTTPClient APIs from there.
namespace net {

enum class State : uint8_t {
    Off,            // no network configured
    Connecting,
    Connected,
    NotFound,       // the access point isn't in range
    WrongPassword,
};

void begin();  // call once from setup()
void loop();   // call every loop(); never blocks

void connect(const char* ssid, const char* password);  // saves them and connects
void forget();                                          // clears them and turns Wi-Fi off

// Background scan for Settings; read the results with WiFi.SSID(i) etc. once
// scanning() is false, up to WiFi.scanComplete() (negative if it failed).
bool startScan();
bool scanning();

bool configured();
bool connected();
State state();
const char* ssid();      // "" when not configured
const char* password();  // "" when not configured or open

// True once the clock has been set over the internet (NTP). From then on
// time()/localtime_r() give the local time (cfg::TIMEZONE).
bool timeValid();

}  // namespace net
