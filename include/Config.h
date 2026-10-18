#pragma once

#include <stdint.h>

namespace cfg {

// Landscape screen size (rotation 1)
constexpr int16_t SCREEN_W = 320;
constexpr int16_t SCREEN_H = 240;

// XPT2046 touch controller pins (separate SPI bus on the CYD)
constexpr int TOUCH_IRQ = 36;
constexpr int TOUCH_MOSI = 32;
constexpr int TOUCH_MISO = 39;
constexpr int TOUCH_SCLK = 25;
constexpr int TOUCH_CS = 33;

// Raw touch calibration. Open the serial monitor and tap the screen corners
// to see raw values if taps land in the wrong place, then adjust these.
constexpr int TOUCH_X_MIN = 200;
constexpr int TOUCH_X_MAX = 3700;
constexpr int TOUCH_Y_MIN = 240;
constexpr int TOUCH_Y_MAX = 3800;

// Local time zone as a POSIX TZ string (here Europe/Amsterdam, with summer
// time). The clock is set over the internet (NTP) once Wi-Fi connects.
constexpr const char* TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";

// Location for the Weather app (forecast from open-meteo.com, no API key).
constexpr const char* WEATHER_PLACE = "Retie, Belgium";
constexpr float WEATHER_LAT = 51.27f;
constexpr float WEATHER_LON = 5.08f;

// On-board RGB LED (active low)
constexpr int LED_R = 4;
constexpr int LED_G = 16;
constexpr int LED_B = 17;

}  // namespace cfg

// Shared colour palette (RGB565)
namespace color {
constexpr uint16_t BG = 0x0841;         // near black
constexpr uint16_t PANEL = 0x18E3;      // dark grey
constexpr uint16_t BORDER = 0x4208;     // mid grey
constexpr uint16_t TEXT = 0xFFFF;       // white
constexpr uint16_t TEXT_DIM = 0x9CD3;   // light grey
constexpr uint16_t ACCENT = 0x07FF;     // cyan
constexpr uint16_t HIGHLIGHT = 0xFFE0;  // yellow
constexpr uint16_t PLAYER_X = 0x061F;   // sky blue
constexpr uint16_t PLAYER_O = 0xFB08;   // coral

// 90s desktop look (Windows 95 style) for menus, buttons, dialogs and HUDs
constexpr uint16_t FACE = 0xC618;         // silver window/button face
constexpr uint16_t FACE_TEXT = 0x0000;    // black text on FACE
constexpr uint16_t EDGE_LIGHT = 0xFFFF;   // bevel highlight
constexpr uint16_t EDGE_SOFT = 0xDEFB;    // inner bevel highlight, scrollbar track
constexpr uint16_t EDGE_SHADOW = 0x8410;  // inner bevel shadow, greyed-out text
constexpr uint16_t EDGE_DARK = 0x0000;    // outer bevel shadow
constexpr uint16_t TITLE = 0x0010;        // navy title bar and selection
constexpr uint16_t TITLE_TEXT = 0xFFFF;
constexpr uint16_t DESKTOP = 0x0410;      // teal
constexpr uint16_t ICON_BG = 0xFFFF;      // white window behind the menu icons
}  // namespace color
