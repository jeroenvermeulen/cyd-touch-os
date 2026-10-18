#include "Settings.h"

#include <Arduino.h>
#include <WiFi.h>

#include <utility>

#include "Config.h"

namespace {
constexpr uint16_t GOOD = 0x0400;  // dark green
constexpr uint16_t BAD = 0xA000;   // dark red
constexpr uint32_t POLL_MS = 500;

// Main screen
constexpr int16_t WELL_X = 10, WELL_Y = ui::contentTop() + 24, WELL_W = 300, WELL_H = 70;
constexpr int16_t LINE_H = 20;
constexpr int16_t VALUE_X = WELL_X + 90;

const Button BTN_CHOOSE{10, 132, 145, 38, "Choose Network"};
const Button BTN_FORGET{165, 132, 145, 38, "Forget"};
const Button BTN_MENU{10, 188, 300, 38, "< Main Menu"};

// Network list
constexpr int16_t NETS_X = 10, NETS_Y = ui::contentTop() + 5, NETS_W = 300;
constexpr int16_t ROW_H = 30;
constexpr int16_t NETS_H = 5 * ROW_H + 2 * ui::BEVEL;

const Button BTN_BACK{10, 188, 80, 40, "Back"};
const Button BTN_RESCAN{98, 188, 110, 40, "Rescan"};
const Button BTN_UP{216, 188, 44, 40, ""};
const Button BTN_DOWN{266, 188, 44, 40, ""};

void drawArrowButton(TFT_eSPI& g, const Button& b, bool up, bool enabled) {
    b.draw(g, false, enabled);
    const int16_t cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    const uint16_t c = enabled ? color::FACE_TEXT : color::EDGE_SHADOW;
    if (up) g.fillTriangle(cx, cy - 5, cx - 7, cy + 4, cx + 7, cy + 4, c);
    else g.fillTriangle(cx, cy + 5, cx - 7, cy - 4, cx + 7, cy - 4, c);
}

// Four bars, filled up to the signal strength, bottom-right at (x, y).
void drawSignal(TFT_eSPI& g, int16_t x, int16_t y, int8_t rssi) {
    const int level = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        const int16_t h = 4 + 3 * i;
        const int16_t bx = x - (4 - i) * 6;
        if (i < level) g.fillRect(bx, y - h, 4, h, color::FACE_TEXT);
        else g.drawRect(bx, y - h, 4, h, color::EDGE_SHADOW);
    }
}

void drawLock(TFT_eSPI& g, int16_t x, int16_t cy) {
    g.drawRect(x + 2, cy - 6, 5, 5, color::FACE_TEXT);  // shackle
    g.fillRect(x, cy - 2, 9, 7, color::FACE_TEXT);
}

// Copies src into dst, cut short with ".." if it's wider than maxW.
void fitText(TFT_eSPI& g, const char* src, char* dst, size_t cap, int16_t maxW) {
    strlcpy(dst, src, cap);
    size_t n = strlen(dst);
    if (g.textWidth(dst, 2) <= maxW) return;
    while (n > 0) {
        dst[--n] = 0;
        char tmp[40];
        snprintf(tmp, sizeof(tmp), "%s..", dst);
        if (g.textWidth(tmp, 2) <= maxW) {
            strlcpy(dst, tmp, cap);
            return;
        }
    }
}

// The Windows 95 Control Panel icon: a folder holding a hammer and a
// screwdriver, as pixel art drawn at 2x. '.' is transparent; the other
// letters are looked up in iconColor().
constexpr int16_t ICON_SCALE = 2;
const char* const ICON[] = {
    "...DDDDDDDDD................",
    "..DWYYYYYYYYD...............",
    ".DWYYYYYYYYYYDDDDDDDDDDDDD..",
    "DWWWWWWWWWWWWWWWWWWWWWWWWWk.",
    "DWYYYYYYYYYYYYYYYYDDYYYYYOk.",
    "DWYYYYYYYYYYYYYYYDWSkYYYYOk.",
    "DWYYYDDDDDDDDDDYYDWSkYYYYOk.",
    "DWYYDWWWWWWWWWWkYDSDkYYYYOk.",
    "DWYYDSSSSSSSSSSkYYWDkYYYYOk.",
    "DWYYYkkkkSDkkkkYYYWDkYYYYOk.",
    "DWYYYYYYYSDkYYYYYYWDkYYYYOk.",
    "DWYYYYYYYSDkYYYYYYWDkYYYYOk.",
    "DWYYYYYYYSDkYYYYYYWDkYYYYOk.",
    "DWYYYYYYYSDkYYYYYYWDkYYYYOk.",
    "DWYYYYYYbcBbkYYYYSSDDkYYYOk.",
    "DWYYYYYYbcBbkYYYYrmRrkYYYOk.",
    "DWYYYYYYbcBbkYYYYrmRrkYYYOk.",
    "DWYYYYYYbcBbkYYYYrmRrkYYYOk.",
    "DWYYYYYYbbbbkYYYYrrrrkYYYOk.",
    "DOOOOOOOOOOOOOOOOOOOOOOOOOk.",
    ".kkkkkkkkkkkkkkkkkkkkkkkkkk.",
};

uint16_t iconColor(char c) {
    switch (c) {
        case 'k': return color::EDGE_DARK;
        case 'D': return color::EDGE_SHADOW;
        case 'S': return color::FACE;
        case 'W': return color::EDGE_LIGHT;
        case 'Y': return 0xFFF0;  // pale folder yellow
        case 'O': return 0x8400;  // dark yellow folder edge
        case 'b': return 0x0010;  // navy
        case 'B': return 0x001F;  // blue
        case 'c': return 0x07FF;  // cyan
        case 'r': return 0x8000;  // dark red
        case 'R': return 0xF800;  // red
        default: return 0xF81F;   // 'm', magenta
    }
}
}  // namespace

// ---------------------------------------------------------------------------
// Game interface

// One fillRect per run of same-coloured pixels.
void Settings::drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) {
    constexpr int ROWS = sizeof(ICON) / sizeof(ICON[0]);
    const int16_t cols = strlen(ICON[0]);
    const int16_t ox = x + (w - cols * ICON_SCALE) / 2;
    const int16_t oy = y + (h - ROWS * ICON_SCALE) / 2;
    for (int r = 0; r < ROWS; r++) {
        const char* row = ICON[r];
        for (int16_t c = 0; c < cols;) {
            int16_t end = c + 1;
            while (end < cols && row[end] == row[c]) end++;
            if (row[c] != '.')
                tft.fillRect(ox + c * ICON_SCALE, oy + r * ICON_SCALE, (end - c) * ICON_SCALE, ICON_SCALE,
                             iconColor(row[c]));
            c = end;
        }
    }
}

void Settings::begin(TFT_eSPI& tft) {
    tft_ = &tft;
    exit_ = false;
    showMain();
}

void Settings::update(const TouchEvent& ev) {
    switch (screen_) {
        case Screen::Main:
            updateMain(ev);
            break;
        case Screen::Networks:
            updateNetworks(ev);
            break;
        case Screen::Password:
            switch (keyboard_.update(ev)) {
                case Keyboard::Result::Done:
                    net::connect(nets_[chosen_].ssid, password_);
                    showMain();
                    break;
                case Keyboard::Result::Cancelled:
                    showNetworks(false);
                    break;
                case Keyboard::Result::None:
                    break;
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// Main screen

void Settings::showMain() {
    screen_ = Screen::Main;
    ui::drawHeader(*tft_, "Settings");
    tft_->setTextDatum(TL_DATUM);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->drawString("Wi-Fi", WELL_X, ui::contentTop() + 6, 2);
    ui::drawPanel(*tft_, WELL_X, WELL_Y, WELL_W, WELL_H, ui::Bevel::Sunken, color::ICON_BG);
    drawStatus();
    BTN_CHOOSE.draw(*tft_);
    BTN_FORGET.draw(*tft_, false, net::configured());
    BTN_MENU.draw(*tft_);
}

void Settings::updateMain(const TouchEvent& ev) {
    // Follow the connection as it comes up or fails.
    if ((int32_t)(millis() - nextPollMs_) >= 0) {
        nextPollMs_ = millis() + POLL_MS;
        if (net::state() != shownState_ || (uint32_t)WiFi.localIP() != shownIp_) drawStatus();
    }

    if (!ev.tapped) return;
    if (BTN_CHOOSE.contains(ev.x, ev.y)) {
        showNetworks(true);
    } else if (BTN_FORGET.contains(ev.x, ev.y) && net::configured()) {
        net::forget();
        showMain();
    } else if (BTN_MENU.contains(ev.x, ev.y)) {
        exit_ = true;
    }
}

void Settings::drawStatus() {
    shownState_ = net::state();
    shownIp_ = net::connected() ? (uint32_t)WiFi.localIP() : 0;

    const char* status = "Not set up";
    uint16_t statusColor = color::FACE_TEXT;
    switch (shownState_) {
        case net::State::Off: break;
        case net::State::Connecting: status = "Connecting..."; break;
        case net::State::Connected:
            status = "Connected";
            statusColor = GOOD;
            break;
        case net::State::NotFound:
            status = "Network not found";
            statusColor = BAD;
            break;
        case net::State::WrongPassword:
            status = "Wrong password?";
            statusColor = BAD;
            break;
    }

    char ssid[40];
    fitText(*tft_, net::configured() ? net::ssid() : "(none)", ssid, sizeof(ssid),
            WELL_X + WELL_W - VALUE_X - 8);
    const String ip = shownIp_ ? WiFi.localIP().toString() : String("-");

    const int16_t x0 = WELL_X + ui::BEVEL, y0 = WELL_Y + ui::BEVEL;
    tft_->fillRect(x0, y0, WELL_W - 2 * ui::BEVEL, WELL_H - 2 * ui::BEVEL, color::ICON_BG);
    tft_->setTextDatum(TL_DATUM);
    const char* const labels[] = {"Network:", "Status:", "IP address:"};
    const char* const values[] = {ssid, status, ip.c_str()};
    for (int i = 0; i < 3; i++) {
        const int16_t ly = WELL_Y + 6 + i * LINE_H;
        tft_->setTextColor(color::EDGE_SHADOW);
        tft_->drawString(labels[i], WELL_X + 8, ly, 2);
        tft_->setTextColor(i == 1 ? statusColor : color::FACE_TEXT);
        tft_->drawString(values[i], VALUE_X, ly, 2);
    }
}

// ---------------------------------------------------------------------------
// Network list

void Settings::showNetworks(bool rescan) {
    screen_ = Screen::Networks;
    ui::drawHeader(*tft_, "Choose Network");
    if (rescan) startScan();
    drawList();
    BTN_BACK.draw(*tft_);
    drawListButtons();
}

void Settings::startScan() {
    scanning_ = true;
    netCount_ = listTop_ = 0;
    listMsg_ = "Scanning...";
    if (!net::startScan()) {
        scanning_ = false;
        listMsg_ = "Scan failed";
    }
}

// Copy the scan results, one entry per name (mesh and multi-band networks
// show up several times) keeping the strongest, sorted strongest first.
void Settings::collectScan() {
    const int16_t n = WiFi.scanComplete();
    scanning_ = false;
    netCount_ = 0;
    for (int16_t i = 0; i < n; i++) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;  // hidden network
        const int8_t rssi = WiFi.RSSI(i);
        const wifi_auth_mode_t auth = WiFi.encryptionType(i);

        int slot = -1;
        for (int j = 0; j < netCount_ && slot < 0; j++)
            if (ssid == nets_[j].ssid) slot = j;
        if (slot >= 0 && nets_[slot].rssi >= rssi) continue;
        if (slot < 0) {
            if (netCount_ < MAX_NETWORKS) slot = netCount_++;
            else {
                slot = 0;  // full: replace the weakest, if this one is stronger
                for (int j = 1; j < netCount_; j++)
                    if (nets_[j].rssi < nets_[slot].rssi) slot = j;
                if (nets_[slot].rssi >= rssi) continue;
            }
        }
        Network& nw = nets_[slot];
        strlcpy(nw.ssid, ssid.c_str(), sizeof(nw.ssid));
        nw.rssi = rssi;
        nw.minPassLen = auth == WIFI_AUTH_OPEN ? 0 : auth == WIFI_AUTH_WEP ? 5 : 8;
    }
    WiFi.scanDelete();

    for (int i = 1; i < netCount_; i++)
        for (int j = i; j > 0 && nets_[j].rssi > nets_[j - 1].rssi; j--) std::swap(nets_[j], nets_[j - 1]);

    if (n < 0) listMsg_ = "Scan failed";
    else listMsg_ = netCount_ ? nullptr : "No networks found";
}

void Settings::updateNetworks(const TouchEvent& ev) {
    if (scanning_ && !net::scanning()) {
        collectScan();
        drawList();
        drawListButtons();
    }

    if (!ev.tapped) return;
    if (BTN_BACK.contains(ev.x, ev.y)) {
        showMain();
    } else if (BTN_RESCAN.contains(ev.x, ev.y) && !scanning_) {
        startScan();
        drawList();
        drawListButtons();
    } else if (BTN_UP.contains(ev.x, ev.y) && listTop_ > 0) {
        listTop_ = max(0, listTop_ - VISIBLE_ROWS);
        drawList();
        drawListButtons();
    } else if (BTN_DOWN.contains(ev.x, ev.y) && listTop_ + VISIBLE_ROWS < netCount_) {
        listTop_ += VISIBLE_ROWS;
        drawList();
        drawListButtons();
    } else if (!listMsg_ && ev.x >= NETS_X && ev.x < NETS_X + NETS_W && ev.y >= NETS_Y + ui::BEVEL) {
        const int i = listTop_ + (ev.y - NETS_Y - ui::BEVEL) / ROW_H;
        if (i < netCount_ && i < listTop_ + VISIBLE_ROWS) choose(i);
    }
}

void Settings::drawList() {
    ui::drawPanel(*tft_, NETS_X, NETS_Y, NETS_W, NETS_H, ui::Bevel::Sunken, color::ICON_BG);
    if (listMsg_) {
        tft_->setTextDatum(MC_DATUM);
        tft_->setTextColor(color::EDGE_SHADOW);
        tft_->drawString(listMsg_, NETS_X + NETS_W / 2, NETS_Y + NETS_H / 2, 2);
        return;
    }

    tft_->setTextDatum(ML_DATUM);
    tft_->setTextColor(color::FACE_TEXT);
    const int16_t right = NETS_X + NETS_W - 8;
    for (int r = 0; r < VISIBLE_ROWS && listTop_ + r < netCount_; r++) {
        const Network& nw = nets_[listTop_ + r];
        const int16_t top = NETS_Y + ui::BEVEL + r * ROW_H;
        const int16_t cy = top + ROW_H / 2;
        if (r > 0) tft_->drawFastHLine(NETS_X + 6, top, NETS_W - 12, color::EDGE_SOFT);

        char name[40];
        fitText(*tft_, nw.ssid, name, sizeof(name), NETS_W - 70);
        tft_->drawString(name, NETS_X + 10, cy, 2);
        if (strcmp(nw.ssid, net::ssid()) == 0)  // the saved network in bold
            tft_->drawString(name, NETS_X + 11, cy, 2);

        drawSignal(*tft_, right, cy + 6, nw.rssi);
        if (nw.minPassLen) drawLock(*tft_, right - 40, cy);
    }
}

void Settings::drawListButtons() {
    BTN_RESCAN.draw(*tft_, false, !scanning_);
    drawArrowButton(*tft_, BTN_UP, true, listTop_ > 0);
    drawArrowButton(*tft_, BTN_DOWN, false, listTop_ + VISIBLE_ROWS < netCount_);
}

void Settings::choose(int i) {
    const Network& nw = nets_[i];
    if (!nw.minPassLen) {
        net::connect(nw.ssid, "");
        showMain();
        return;
    }

    // Start from the saved password when re-picking the same network.
    chosen_ = i;
    strlcpy(password_, strcmp(nw.ssid, net::ssid()) == 0 ? net::password() : "", sizeof(password_));
    snprintf(title_, sizeof(title_), "Password for %s", nw.ssid);
    if (tft_->textWidth(title_, 2) > cfg::SCREEN_W - 20) strlcpy(title_, "Wi-Fi password", sizeof(title_));
    screen_ = Screen::Password;
    keyboard_.begin(*tft_, title_, password_, sizeof(password_), nw.minPassLen);
}
