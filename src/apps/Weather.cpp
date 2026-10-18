#include "apps/Weather.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "Config.h"
#include "Net.h"

namespace {
constexpr uint32_t REFRESH_MS = 15 * 60 * 1000;  // Open-Meteo updates "current" every 15 minutes
constexpr uint32_t RETRY_MS = 60 * 1000;

// Current conditions in a sunken well on the left, details and buttons on
// the right, one well per day along the bottom.
constexpr int16_t TOP = ui::contentTop() + 4;
constexpr int16_t NOW_X = 6, NOW_Y = TOP, NOW_W = 150, NOW_H = 110;
constexpr int16_t SIDE_X = NOW_X + NOW_W + 6;
constexpr int16_t SIDE_W = cfg::SCREEN_W - SIDE_X - 6;
constexpr int16_t BTN_H = 26, BTN_W = (SIDE_W - 4) / 2;
constexpr int16_t BTN_Y = NOW_Y + NOW_H - BTN_H;
constexpr int16_t INFO_H = BTN_Y - TOP - 4;
constexpr int16_t DAY_Y = NOW_Y + NOW_H + 6;
constexpr int16_t DAY_H = cfg::SCREEN_H - DAY_Y - 6;
constexpr int16_t DAY_GAP = 4;
constexpr int16_t DAY_W = (cfg::SCREEN_W - 12 - 3 * DAY_GAP) / 4;

constexpr uint16_t SUN = 0xFE60;
constexpr uint16_t MOON = 0xF7BB;
constexpr uint16_t CLOUD = 0xFFFF;
constexpr uint16_t CLOUD_GREY = 0xA534;
constexpr uint16_t CLOUD_EDGE = 0x6B4D;
constexpr uint16_t RAIN = 0x4C9F;
constexpr uint16_t SNOW = 0xFFFF;
constexpr uint16_t BOLT = 0xFFE0;
constexpr uint16_t FOG = 0xAD55;
constexpr uint16_t SKY_DAY = 0x867D;   // menu icon background
constexpr uint16_t SKY_NIGHT = 0x18C9;

const Button BTN_REFRESH{SIDE_X, BTN_Y, BTN_W, BTN_H, "Refresh"};
const Button BTN_BACK{SIDE_X + SIDE_W - BTN_W, BTN_Y, BTN_W, BTN_H, "< Back"};

bool elapsed(uint32_t since, uint32_t ms) { return (int32_t)(millis() - (since + ms)) >= 0; }

// Sakamoto's method; 0 = Sunday.
uint8_t weekdayOf(int y, int m, int d) {
    static const uint8_t T[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + T[m - 1] + d) % 7;
}

const char* describe(uint8_t code) {
    switch (code) {
        case 0: return "Clear sky";
        case 1: return "Mainly clear";
        case 2: return "Partly cloudy";
        case 3: return "Overcast";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66: case 67: return "Freezing rain";
        case 71: return "Light snow";
        case 73: return "Snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80: return "Light showers";
        case 81: return "Showers";
        case 82: return "Heavy showers";
        case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Thunder & hail";
        default: return "Unknown";
    }
}

const char* compass(uint16_t deg) {
    static const char* const POINTS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return POINTS[((deg + 22) / 45) % 8];
}

// --- Weather pictures, drawn from shapes and scaled to a box of side s ---

enum class Sky : uint8_t { Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Snow, Thunder };

Sky skyFor(uint8_t code) {
    if (code == 0) return Sky::Clear;
    if (code <= 2) return Sky::PartlyCloudy;
    if (code == 3) return Sky::Cloudy;
    if (code == 45 || code == 48) return Sky::Fog;
    if (code >= 51 && code <= 57) return Sky::Drizzle;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
    if (code >= 95) return Sky::Thunder;
    return Sky::Rain;
}

void drawSun(TFT_eSPI& g, float cx, float cy, float r, uint16_t bg) {
    for (int i = 0; i < 8; i++) {
        const float a = i * 45 * DEG_TO_RAD;
        g.drawWideLine(cx + cosf(a) * r * 1.35f, cy + sinf(a) * r * 1.35f, cx + cosf(a) * r * 1.8f,
                       cy + sinf(a) * r * 1.8f, max(1.0f, r / 5), SUN, bg);
    }
    g.fillSmoothCircle(cx, cy, r, SUN, bg);
}

// Crescent: a disc with a bg-coloured disc cut out of its top right.
void drawMoon(TFT_eSPI& g, float cx, float cy, float r, uint16_t bg) {
    g.fillSmoothCircle(cx, cy, r, MOON, bg);
    g.fillSmoothCircle(cx + r * 0.45f, cy - r * 0.3f, r * 0.8f, bg, MOON);
}

// Cloud of width w whose flat bottom is centred on (cx, cy). Plain (not
// anti-aliased) shapes so the overlapping parts merge without seams; the
// outline is the same shapes one pixel bigger.
void drawCloud(TFT_eSPI& g, float cx, float cy, float w, uint16_t fill) {
    const int16_t h = w * 0.32f;
    const int16_t x = cx - w / 2, y = cy - h / 2;
    const int16_t lx = cx - w * 0.17f, ly = cy - h * 0.35f, lr = w * 0.2f;
    const int16_t rx = cx + w * 0.1f, ry = cy - h * 0.55f, rr = w * 0.27f;
    for (int grow = 1; grow >= 0; grow--) {
        const uint16_t c = grow ? CLOUD_EDGE : fill;
        g.fillRoundRect(x - grow, y - grow, w + 2 * grow, h + 2 * grow, h / 2 + grow, c);
        g.fillCircle(lx, ly, lr + grow, c);
        g.fillCircle(rx, ry, rr + grow, c);
    }
}

void drawWeather(TFT_eSPI& g, int16_t cx, int16_t cy, int16_t s, uint8_t code, bool isDay, uint16_t bg) {
    const float stroke = max(1.0f, s / 22.0f);
    const Sky sky = skyFor(code);
    switch (sky) {
        case Sky::Clear:
            if (isDay) drawSun(g, cx, cy, s * 0.22f, bg);
            else drawMoon(g, cx, cy, s * 0.3f, bg);
            return;
        case Sky::PartlyCloudy:
            if (isDay) drawSun(g, cx - s * 0.15f, cy - s * 0.13f, s * 0.16f, bg);
            else drawMoon(g, cx - s * 0.12f, cy - s * 0.14f, s * 0.22f, bg);
            drawCloud(g, cx + s * 0.06f, cy + s * 0.16f, s * 0.7f, CLOUD);
            return;
        case Sky::Cloudy:
            drawCloud(g, cx - s * 0.12f, cy, s * 0.6f, CLOUD);
            drawCloud(g, cx + s * 0.05f, cy + s * 0.17f, s * 0.8f, CLOUD_GREY);
            return;
        case Sky::Fog:
            drawCloud(g, cx, cy - s * 0.02f, s * 0.7f, CLOUD_GREY);
            g.drawWideLine(cx - s * 0.35f, cy + s * 0.2f, cx + s * 0.3f, cy + s * 0.2f, stroke * 1.5f, FOG, bg);
            g.drawWideLine(cx - s * 0.25f, cy + s * 0.33f, cx + s * 0.38f, cy + s * 0.33f, stroke * 1.5f, FOG, bg);
            return;
        default:
            break;
    }

    // Something falls out of a cloud; the bolt starts behind it.
    const float base = cy - s * 0.08f;
    if (sky == Sky::Thunder) {
        g.fillTriangle(cx + s * 0.08f, base, cx - s * 0.12f, base + s * 0.3f, cx + s * 0.04f, base + s * 0.3f, BOLT);
        g.fillTriangle(cx - s * 0.04f, base + s * 0.25f, cx + s * 0.12f, base + s * 0.25f, cx - s * 0.08f,
                       base + s * 0.55f, BOLT);
    }
    drawCloud(g, cx, base, s * 0.8f, sky == Sky::Thunder ? CLOUD_GREY : CLOUD);
    for (int i = -1; i <= 1; i++) {
        const float x = cx + i * s * 0.22f, top = base + s * 0.18f;
        if (sky == Sky::Snow) {
            g.fillSmoothCircle(x, top + s * 0.04f, stroke * 1.4f, SNOW, bg);
            if (i < 1) g.fillSmoothCircle(x + s * 0.11f, top + s * 0.2f, stroke * 1.4f, SNOW, bg);
        } else if (sky == Sky::Drizzle) {
            g.drawWideLine(x + s * 0.03f, top, x - s * 0.01f, top + s * 0.08f, stroke, RAIN, bg);
            g.drawWideLine(x - s * 0.03f, top + s * 0.16f, x - s * 0.07f, top + s * 0.24f, stroke, RAIN, bg);
        } else if (sky == Sky::Rain || i != 0) {
            g.drawWideLine(x + s * 0.06f, top, x - s * 0.06f, top + s * 0.26f, stroke, RAIN, bg);
        }
    }
}

// The built-in fonts have no degree sign, so temperatures get a small ring
// after the number. Returns the width of "<t>°".
int16_t degreeRadius(uint8_t font) { return font == 6 ? 4 : font == 4 ? 3 : 2; }

int16_t tempWidth(TFT_eSPI& g, const char* t, uint8_t font) {
    return g.textWidth(t, font) + 2 + 2 * degreeRadius(font) + 1;
}

void drawTemp(TFT_eSPI& g, const char* t, int16_t x, int16_t y, uint8_t font, uint16_t fg, uint16_t bg) {
    g.setTextDatum(TL_DATUM);
    g.setTextColor(fg, bg);
    const int16_t w = g.drawString(t, x, y, font);
    const int16_t r = degreeRadius(font);
    const int16_t top = font == 6 ? 8 : font == 4 ? 4 : 3;  // where the digits start in the glyph box
    g.drawCircle(x + w + 2 + r, y + top + r, r, fg);
    if (font != 2) g.drawCircle(x + w + 2 + r, y + top + r, r - 1, fg);
}

void formatTemp(char* buf, size_t len, float t) { snprintf(buf, len, "%d", (int)lroundf(t)); }

// Rain drop in front of the chance of rain.
void drawDrop(TFT_eSPI& g, int16_t x, int16_t y) {
    g.fillTriangle(x + 3, y, x, y + 5, x + 6, y + 5, RAIN);
    g.fillCircle(x + 3, y + 6, 3, RAIN);
}
}  // namespace

// ---------------------------------------------------------------------------
// App interface

void Weather::drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) {
    // The last downloaded weather, or a sun behind a cloud before that.
    uint8_t code = 2;
    bool isDay = true;
    if (version_ > 0) {
        const Report r = snapshot();
        code = r.code;
        isDay = r.isDay;
    }
    const uint16_t sky = isDay ? SKY_DAY : SKY_NIGHT;
    tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 6, sky);
    drawWeather(tft, x + w / 2, y + h / 2, min(w, h) - 8, code, isDay, sky);
}

void Weather::begin(TFT_eSPI& tft) {
    tft_ = &tft;
    exit_ = false;
    shownVersion_ = 0;
    shownStatus_ = -1;
    shownBusy_ = -1;
    shownRefresh_ = -1;

    char title[48];
    snprintf(title, sizeof(title), "Weather - %s", cfg::WEATHER_PLACE);
    ui::drawHeader(tft, title);
    ui::drawPanel(tft, NOW_X, NOW_Y, NOW_W, NOW_H, ui::Bevel::Sunken, color::BG);
    ui::drawPanel(tft, SIDE_X, TOP, SIDE_W, INFO_H, ui::Bevel::Sunken, color::ICON_BG);
    for (int i = 0; i < DAYS; i++) {
        ui::drawPanel(tft, 6 + i * (DAY_W + DAY_GAP), DAY_Y, DAY_W, DAY_H, ui::Bevel::Sunken, color::BG);
    }
    BTN_BACK.draw(tft);
    drawRefreshButton();

    if (version_ > 0) drawReport();
    else drawStatus();
}

void Weather::update(const TouchEvent& ev) {
    if (ev.tapped) {
        if (BTN_BACK.contains(ev.x, ev.y)) {
            exit_ = true;
            return;
        }
        if (BTN_REFRESH.contains(ev.x, ev.y) && net::connected() && fetch_ == Fetch::Idle) {
            retryWait_ = false;
            startFetch();
        }
    }

    if (fetch_ == Fetch::Failed) {
        fetch_ = Fetch::Idle;
        retryWait_ = true;
        failedMs_ = millis();
    }
    if (net::connected() && fetch_ == Fetch::Idle) {
        portENTER_CRITICAL(&lock_);
        const uint32_t fetched = fetchedMs_;
        portEXIT_CRITICAL(&lock_);
        const bool due = retryWait_ ? elapsed(failedMs_, RETRY_MS) : version_ == 0 || elapsed(fetched, REFRESH_MS);
        if (due) startFetch();
    }

    if (version_ != shownVersion_) {
        retryWait_ = false;
        drawReport();
    } else if (version_ == 0 && statusCode() != shownStatus_) {
        drawStatus();
    } else if (version_ > 0 && (fetch_ == Fetch::Busy) != shownBusy_) {
        drawDetails(snapshot());
    }
    if ((net::connected() && fetch_ == Fetch::Idle) != shownRefresh_) drawRefreshButton();
}

// ---------------------------------------------------------------------------
// Downloading

void Weather::startFetch() {
    fetch_ = Fetch::Busy;
    if (xTaskCreate(fetchTask, "weather", 8192, this, 1, nullptr) != pdPASS) {
        Serial.println("Weather: couldn't start the download task");
        fetch_ = Fetch::Failed;
    }
}

void Weather::fetchTask(void* arg) {
    auto* self = static_cast<Weather*>(arg);
    Report r;
    if (download(r)) {
        portENTER_CRITICAL(&self->lock_);
        self->report_ = r;
        self->fetchedMs_ = millis();
        portEXIT_CRITICAL(&self->lock_);
        self->version_++;
        self->fetch_ = Fetch::Idle;
    } else {
        self->fetch_ = Fetch::Failed;
    }
    vTaskDelete(nullptr);
}

bool Weather::download(Report& out) {
    // Plain HTTP: TLS would need another ~40 KB of heap for a public forecast.
    char url[400];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,"
             "wind_speed_10m,wind_direction_10m,is_day"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&timezone=auto&forecast_days=%d",
             cfg::WEATHER_LAT, cfg::WEATHER_LON, DAYS);

    WiFiClient client;
    HTTPClient http;
    http.useHTTP10(true);  // no chunked encoding, so ArduinoJson can read the stream as is
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
    if (!http.begin(client, url)) return false;
    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("Weather: HTTP %d\n", status);
        http.end();
        return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) {
        Serial.printf("Weather: bad JSON (%s)\n", err.c_str());
        return false;
    }

    JsonObject cur = doc["current"];
    JsonObject daily = doc["daily"];
    JsonArray dates = daily["time"];
    if (cur.isNull() || dates.size() < DAYS) {
        Serial.println("Weather: unexpected response");
        return false;
    }
    out.temp = cur["temperature_2m"];
    out.feelsLike = cur["apparent_temperature"];
    out.humidity = cur["relative_humidity_2m"];
    out.code = cur["weather_code"];
    out.wind = cur["wind_speed_10m"];
    out.windDir = cur["wind_direction_10m"];
    out.isDay = cur["is_day"] | 1;
    const char* time = cur["time"] | "";  // "2026-10-18T14:30"
    strlcpy(out.asOf, strlen(time) >= 16 ? time + 11 : "", sizeof(out.asOf));

    for (int i = 0; i < DAYS; i++) {
        Day& d = out.days[i];
        int y = 2000, m = 1, dd = 1;
        sscanf(dates[i] | "", "%d-%d-%d", &y, &m, &dd);
        d.weekday = weekdayOf(y, m, dd);
        d.code = daily["weather_code"][i];
        d.max = daily["temperature_2m_max"][i];
        d.min = daily["temperature_2m_min"][i];
        d.rainChance = daily["precipitation_probability_max"][i] | 0;
    }
    return true;
}

Weather::Report Weather::snapshot() const {
    portENTER_CRITICAL(&lock_);
    const Report r = report_;
    portEXIT_CRITICAL(&lock_);
    return r;
}

// ---------------------------------------------------------------------------
// Drawing

int Weather::statusCode() const {
    if (!net::configured()) return 0;
    if (!net::connected()) return 1;
    return fetch_ == Fetch::Busy ? 2 : retryWait_ ? 3 : 2;
}

void Weather::drawStatus() {
    static const char* const MESSAGES[4][2] = {
        {"Set up Wi-Fi", "in Settings."},
        {"Waiting for", "Wi-Fi..."},
        {"Getting the", "weather..."},
        {"Couldn't get the", "weather. Retrying."},
    };
    shownStatus_ = statusCode();
    tft_->fillRect(NOW_X + ui::BEVEL, NOW_Y + ui::BEVEL, NOW_W - 2 * ui::BEVEL, NOW_H - 2 * ui::BEVEL, color::BG);
    tft_->setTextDatum(TC_DATUM);
    tft_->setTextColor(color::TEXT, color::BG);
    for (int i = 0; i < 2; i++) {
        tft_->drawString(MESSAGES[shownStatus_][i], NOW_X + NOW_W / 2, NOW_Y + NOW_H / 2 - 18 + i * 18, 2);
    }
}

void Weather::drawReport() {
    shownVersion_ = version_;
    shownStatus_ = -1;
    const Report r = snapshot();
    drawCurrent(r);
    drawDetails(r);
    drawDays(r);
}

void Weather::drawCurrent(const Report& r) {
    tft_->fillRect(NOW_X + ui::BEVEL, NOW_Y + ui::BEVEL, NOW_W - 2 * ui::BEVEL, NOW_H - 2 * ui::BEVEL, color::BG);
    drawWeather(*tft_, NOW_X + 40, NOW_Y + 44, 64, r.code, r.isDay, color::BG);

    char t[8];
    formatTemp(t, sizeof(t), r.temp);
    constexpr int16_t TEMP_X = NOW_X + 78, TEMP_W = NOW_W - 78 - 6;
    const uint8_t font = tempWidth(*tft_, t, 6) <= TEMP_W ? 6 : 4;
    const int16_t w = tempWidth(*tft_, t, font);
    drawTemp(*tft_, t, TEMP_X + (TEMP_W - w) / 2, NOW_Y + (font == 6 ? 18 : 30), font, color::TEXT, color::BG);

    tft_->setTextDatum(TC_DATUM);
    tft_->setTextColor(color::TEXT, color::BG);
    tft_->drawString(describe(r.code), NOW_X + NOW_W / 2, NOW_Y + NOW_H - 26, 2);
}

void Weather::drawDetails(const Report& r) {
    shownBusy_ = fetch_ == Fetch::Busy;
    constexpr int16_t L = SIDE_X + 6, R = SIDE_X + SIDE_W - 6;
    tft_->fillRect(SIDE_X + ui::BEVEL, TOP + ui::BEVEL, SIDE_W - 2 * ui::BEVEL, INFO_H - 2 * ui::BEVEL,
                   color::ICON_BG);

    char feels[8], humidity[8], wind[16];
    formatTemp(feels, sizeof(feels), r.feelsLike);
    snprintf(humidity, sizeof(humidity), "%u%%", r.humidity);
    snprintf(wind, sizeof(wind), "%d km/h %s", (int)lroundf(r.wind), compass(r.windDir));
    const char* labels[4] = {"Feels like", "Humidity", "Wind", "Updated"};
    const char* values[4] = {nullptr, humidity, wind, shownBusy_ ? "..." : r.asOf};

    for (int i = 0; i < 4; i++) {
        const int16_t y = TOP + 6 + i * 18;
        tft_->setTextColor(color::FACE_TEXT, color::ICON_BG);
        tft_->setTextDatum(TL_DATUM);
        tft_->drawString(labels[i], L, y, 2);
        if (values[i]) {
            tft_->setTextDatum(TR_DATUM);
            tft_->drawString(values[i], R, y, 2);
        } else {
            drawTemp(*tft_, feels, R - tempWidth(*tft_, feels, 2), y, 2, color::FACE_TEXT, color::ICON_BG);
        }
    }
}

void Weather::drawDays(const Report& r) {
    static const char* const NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (int i = 0; i < DAYS; i++) {
        const Day& d = r.days[i];
        const int16_t x = 6 + i * (DAY_W + DAY_GAP), cx = x + DAY_W / 2;
        tft_->fillRect(x + ui::BEVEL, DAY_Y + ui::BEVEL, DAY_W - 2 * ui::BEVEL, DAY_H - 2 * ui::BEVEL, color::BG);

        tft_->setTextDatum(TC_DATUM);
        tft_->setTextColor(color::TEXT, color::BG);
        tft_->drawString(i == 0 ? "Today" : NAMES[d.weekday], cx, DAY_Y + 5, 2);
        drawWeather(*tft_, cx, DAY_Y + 40, 36, d.code, true, color::BG);

        // High in white, low in grey.
        char hi[8], lo[8];
        formatTemp(hi, sizeof(hi), d.max);
        formatTemp(lo, sizeof(lo), d.min);
        const int16_t hw = tempWidth(*tft_, hi, 2), lw = tempWidth(*tft_, lo, 2);
        const int16_t tx = cx - (hw + 6 + lw) / 2;
        drawTemp(*tft_, hi, tx, DAY_Y + 60, 2, color::TEXT, color::BG);
        drawTemp(*tft_, lo, tx + hw + 6, DAY_Y + 60, 2, color::TEXT_DIM, color::BG);

        char rain[6];
        snprintf(rain, sizeof(rain), "%u%%", d.rainChance);
        const int16_t rw = 7 + 3 + tft_->textWidth(rain, 2);
        drawDrop(*tft_, cx - rw / 2, DAY_Y + 77);
        tft_->setTextDatum(TL_DATUM);
        tft_->setTextColor(color::TEXT_DIM, color::BG);
        tft_->drawString(rain, cx - rw / 2 + 10, DAY_Y + 75, 2);
    }
}

void Weather::drawRefreshButton() {
    shownRefresh_ = net::connected() && fetch_ == Fetch::Idle;
    BTN_REFRESH.draw(*tft_, false, shownRefresh_);
}
