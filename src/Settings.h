#pragma once

#include "Game.h"
#include "Keyboard.h"
#include "Net.h"
#include "UI.h"

// Configuration screen, launched from the menu like a game. For now it sets
// up Wi-Fi: scan, pick a network, type its password; net:: then connects to
// it at every startup.
class Settings : public Game {
public:
    const char* name() const override { return "Settings"; }
    void drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) override;
    void begin(TFT_eSPI& tft) override;
    void update(const TouchEvent& ev) override;
    bool finished() const override { return exit_; }

private:
    enum class Screen : uint8_t { Main, Networks, Password };

    struct Network {
        char ssid[33];
        int8_t rssi;
        uint8_t minPassLen;  // 0 for an open network
    };
    static constexpr int MAX_NETWORKS = 20;
    static constexpr int VISIBLE_ROWS = 5;

    // Main screen: current network and status
    void showMain();
    void updateMain(const TouchEvent& ev);
    void drawStatus();

    // Network list
    void showNetworks(bool rescan);
    void updateNetworks(const TouchEvent& ev);
    void startScan();
    void collectScan();
    void drawList();
    void drawListButtons();
    void choose(int i);

    TFT_eSPI* tft_ = nullptr;
    Screen screen_ = Screen::Main;
    bool exit_ = false;

    // Last status drawn, so the main screen redraws only on a change.
    net::State shownState_ = net::State::Off;
    uint32_t shownIp_ = 0;
    uint32_t nextPollMs_ = 0;

    Network nets_[MAX_NETWORKS];
    int netCount_ = 0;
    int listTop_ = 0;
    bool scanning_ = false;
    const char* listMsg_ = nullptr;  // shown instead of the list (scanning, none found)

    Keyboard keyboard_;
    int chosen_ = -1;
    char password_[64] = "";
    char title_[48] = "";
};
