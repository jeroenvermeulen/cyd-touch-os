#pragma once

#include <atomic>

#include "App.h"
#include "UI.h"

// Current weather and a short forecast for cfg::WEATHER_PLACE, from
// open-meteo.com. Downloads run in a background task so update() never
// waits on the network.
class Weather : public App {
public:
    const char* name() const override { return "Weather"; }
    void drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) override;
    uint32_t iconVersion() const override { return version_; }  // changes with every download
    void begin(TFT_eSPI& tft) override;
    void update(const TouchEvent& ev) override;
    bool finished() const override { return exit_; }

private:
    static constexpr int DAYS = 4;  // today and the next three

    struct Day {
        uint8_t weekday = 0;  // 0 = Sunday
        uint8_t code = 0;     // WMO weather code
        uint8_t rainChance = 0;
        float max = 0, min = 0;
    };
    struct Report {
        float temp = 0, feelsLike = 0, wind = 0;
        uint8_t humidity = 0, code = 0;
        uint16_t windDir = 0;
        bool isDay = true;
        char asOf[6] = "";  // "HH:MM", local time at the location
        Day days[DAYS];
    };
    enum class Fetch : uint8_t { Idle, Busy, Failed };

    void startFetch();
    static void fetchTask(void* self);
    static bool download(Report& out);  // runs in the fetch task
    Report snapshot() const;

    int statusCode() const;
    void drawStatus();
    void drawReport();
    void drawCurrent(const Report& r);
    void drawDetails(const Report& r);
    void drawDays(const Report& r);
    void drawRefreshButton();

    TFT_eSPI* tft_ = nullptr;
    bool exit_ = false;

    // Written by the fetch task, under lock_.
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    Report report_;
    uint32_t fetchedMs_ = 0;
    std::atomic<uint32_t> version_{0};  // 0 = nothing downloaded yet
    std::atomic<Fetch> fetch_{Fetch::Idle};

    bool retryWait_ = false;  // last download failed; wait before trying again
    uint32_t failedMs_ = 0;
    uint32_t shownVersion_ = 0;
    int shownStatus_ = -1;
    int shownBusy_ = -1;     // whether "Updated" shows "..."
    int shownRefresh_ = -1;  // whether Refresh is drawn enabled
};
