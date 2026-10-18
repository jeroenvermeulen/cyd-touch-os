#include "apps/CrossyRoad.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

#include "Config.h"

namespace {
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t GRASS_A = rgb(112, 200, 80);
constexpr uint16_t GRASS_B = rgb(100, 186, 72);
constexpr uint16_t TREE = rgb(30, 120, 50);
constexpr uint16_t TRUNK = rgb(110, 70, 40);
constexpr uint16_t ROAD = rgb(70, 70, 80);
constexpr uint16_t ROAD_LINE = rgb(200, 200, 200);
constexpr uint16_t WATER = rgb(60, 140, 230);
constexpr uint16_t WATER_LIGHT = rgb(140, 200, 250);
constexpr uint16_t LOG = rgb(140, 90, 50);
constexpr uint16_t LOG_DARK = rgb(100, 60, 30);
constexpr uint16_t GRAVEL = rgb(120, 110, 100);
constexpr uint16_t SLEEPER = rgb(90, 60, 40);
constexpr uint16_t RAIL = rgb(190, 190, 200);
constexpr uint16_t TRAIN = rgb(200, 40, 40);
constexpr uint16_t SIGNAL_OFF = rgb(70, 10, 10);
constexpr uint16_t SIGNAL_ON = rgb(255, 40, 40);
constexpr uint16_t GLASS = rgb(150, 210, 240);
constexpr uint16_t TIRE = rgb(20, 20, 20);
constexpr uint16_t TRUCK_BOX = rgb(230, 230, 230);
constexpr uint16_t CHICKEN = rgb(255, 255, 255);
constexpr uint16_t COMB = rgb(230, 40, 40);
constexpr uint16_t BEAK = rgb(250, 160, 30);
constexpr uint16_t SHADOW = rgb(40, 60, 40);
constexpr uint16_t CAR_COLORS[] = {rgb(230, 60, 60), rgb(250, 200, 40), rgb(60, 120, 230),
                                   rgb(160, 80, 200), rgb(250, 130, 40)};

constexpr int16_t TILE = 20;
constexpr int COLS = cfg::SCREEN_W / TILE;  // 16
constexpr int ROWS = cfg::SCREEN_H / TILE;  // 12
constexpr int START_LANE = 2;
constexpr int START_COL = COLS / 2;
constexpr int SAFE_LANES = 5;  // grass-only lanes at the start
constexpr int CAM_LEAD = 3;    // lanes kept below the chicken

constexpr int16_t WRAP_MARGIN = 100;  // must be >= the widest car or log
constexpr float SPAN = cfg::SCREEN_W + WRAP_MARGIN;

constexpr uint32_t HOP_MS = 110;
constexpr uint32_t DEATH_MS = 900;
constexpr uint32_t TRAIN_WARN_MS = 1200;
constexpr int16_t TRAIN_LEN = 200;
constexpr float TRAIN_SPEED = 0.45f;

// Touch zones
constexpr int16_t SIDE_ZONE = 70;
constexpr int16_t BACK_ZONE_Y = 190;

const Button BTN_RESUME{75, 140, 80, 36, "Resume"};
const Button BTN_RETRY{75, 140, 80, 36, "Retry"};
const Button BTN_MENU{165, 140, 80, 36, "Menu"};

const char* const PREFS_NS = "crossy";
}  // namespace

// ---------------------------------------------------------------------------
// App interface

void CrossyRoad::drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) {
    const int16_t roadY = y + h / 2 - 8;
    tft.fillRect(x, y, w, h, GRASS_A);
    tft.fillRect(x, roadY, w, 20, ROAD);
    for (int16_t dx = 6; dx < w - 10; dx += 22) tft.fillRect(x + dx, roadY + 9, 10, 2, ROAD_LINE);
    tft.fillRoundRect(x + 14, roadY + 3, 28, 14, 3, CAR_COLORS[0]);
    tft.fillRect(x + 34, roadY + 5, 4, 10, GLASS);
    tft.fillRoundRect(x + w - 30, y + 1, 16, 13, 5, TREE);
    drawChicken(tft, x + w / 2 + 8, y + h - 21, Dir::Up);
}

void CrossyRoad::begin(TFT_eSPI& tft) {
    tft_ = &tft;
    exit_ = false;
    if (!strip_) {
        strip_ = new TFT_eSprite(&tft);
        strip_->setColorDepth(16);
        if (!strip_->createSprite(cfg::SCREEN_W, TILE)) Serial.println("CrossyRoad: sprite alloc failed");
    }

    Preferences prefs;
    prefs.begin(PREFS_NS, true);
    best_ = prefs.getInt("best", 0);
    prefs.end();

    newGame();
}

void CrossyRoad::update(const TouchEvent& ev) {
    const uint32_t ms = millis();
    const uint32_t dt = min<uint32_t>(ms - lastMs_, 50);
    lastMs_ = ms;

    switch (state_) {
        case State::Paused:
            if (!ev.tapped) return;
            if (BTN_RESUME.contains(ev.x, ev.y)) state_ = resumeState_;
            else if (BTN_MENU.contains(ev.x, ev.y)) exit_ = true;
            return;

        case State::GameOver:
            if (!ev.tapped) return;
            if (BTN_RETRY.contains(ev.x, ev.y)) newGame();
            else if (BTN_MENU.contains(ev.x, ev.y)) exit_ = true;
            return;

        case State::Ready:
        case State::Playing:
            if (ev.tapped) {
                if (ev.x >= cfg::SCREEN_W - 40 && ev.y < 30) {
                    showPause();
                    return;
                }
                Dir dir = Dir::Up;
                if (ev.x < SIDE_ZONE) dir = Dir::Left;
                else if (ev.x >= cfg::SCREEN_W - SIDE_ZONE) dir = Dir::Right;
                else if (ev.y >= BACK_ZONE_Y) dir = Dir::Down;

                if (hopping_) {
                    hasPending_ = true;
                    pending_ = dir;
                } else {
                    tryHop(dir);
                }
                if (state_ == State::Ready && hopping_) state_ = State::Playing;
            }
            break;

        case State::Dying:
            break;
    }

    step(dt);
    render();
    if (state_ == State::Dying && now_ - deathAt_ >= DEATH_MS) showGameOver();
}

// The screensaver pauses the game; it waits for an animation to finish.
bool CrossyRoad::suspend() {
    if (state_ == State::Dying) return false;  // let it finish first
    if (state_ != State::Paused && state_ != State::GameOver) showPause();
    return true;
}

// Back from the screensaver: the game as it was, under its dialog.
void CrossyRoad::restore() {
    render();
    if (state_ == State::Paused) drawPauseDialog();
    else showGameOver();
}

// ---------------------------------------------------------------------------
// Game flow

void CrossyRoad::newGame() {
    randomSeed(esp_random());
    state_ = State::Ready;
    now_ = 0;
    lastMs_ = millis();
    cam_ = 0;
    generated_ = -1;
    runType_ = LaneType::Grass;
    runLeft_ = 0;

    cLane_ = START_LANE;
    cx_ = START_COL * TILE;
    facing_ = Dir::Up;
    hopping_ = false;
    hasPending_ = false;
    score_ = 0;

    ensureLanes();
    render();
}

void CrossyRoad::step(uint32_t dt) {
    now_ += dt;
    updateLanes(dt);
    if (state_ != State::Playing) return;

    updateChicken(dt);
    if (state_ != State::Playing) return;
    checkCollisions();
    if (state_ != State::Playing) return;

    // Camera follows the chicken and also creeps forward on its own.
    const float autoScroll = min(0.02f, 0.006f + score_ * 0.0001f);
    cam_ += autoScroll * dt;
    const float target = max(0, (cLane_ - CAM_LEAD) * TILE);
    if (cam_ < target) cam_ += (target - cam_) * min(1.0f, dt * 0.008f);
    ensureLanes();

    if (laneY(cLane_) >= cfg::SCREEN_H) die(Death::TooSlow);
}

void CrossyRoad::die(Death how) {
    state_ = State::Dying;
    death_ = how;
    deathAt_ = now_;
    hopping_ = false;
    hasPending_ = false;

    if (score_ > best_) {
        best_ = score_;
        Preferences prefs;
        prefs.begin(PREFS_NS, false);
        prefs.putInt("best", best_);
        prefs.end();
    }
}

void CrossyRoad::showPause() {
    resumeState_ = state_;
    state_ = State::Paused;
    drawPauseDialog();
}

void CrossyRoad::drawPauseDialog() {
    ui::drawDialog(*tft_, "Paused");
    char buf[32];
    snprintf(buf, sizeof(buf), "Score %d   Best %d", score_, best_);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->drawString(buf, cfg::SCREEN_W / 2, 100, 2);
    BTN_RESUME.draw(*tft_);
    BTN_MENU.draw(*tft_);
}

void CrossyRoad::showGameOver() {
    state_ = State::GameOver;
    ui::drawDialog(*tft_, "Game Over");

    const char* reason = "Squashed!";
    if (death_ == Death::Drowned) reason = "Splash!";
    else if (death_ == Death::SweptAway) reason = "Swept away!";
    else if (death_ == Death::TooSlow) reason = "Too slow!";
    tft_->setTextColor(color::TITLE, color::FACE);
    tft_->drawString(reason, cfg::SCREEN_W / 2, 90, 2);

    char buf[32];
    snprintf(buf, sizeof(buf), "Score %d   Best %d", score_, best_);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->drawString(buf, cfg::SCREEN_W / 2, 112, 2);

    BTN_RETRY.draw(*tft_);
    BTN_MENU.draw(*tft_);
}

// ---------------------------------------------------------------------------
// World

int16_t CrossyRoad::laneY(int n) const {
    return cfg::SCREEN_H - (n + 1) * TILE + (int)cam_;
}

void CrossyRoad::ensureLanes() {
    const int need = max(lowestVisibleLane() + ROWS + 2, cLane_ + 2);
    while (generated_ < need) generateLane(++generated_);
}

void CrossyRoad::generateLane(int n) {
    Lane& ln = lane(n);
    ln = Lane{};

    if (n < SAFE_LANES) {
        ln.type = LaneType::Grass;
        if (n != START_LANE) ln.trees = (1 << 0) | (1 << (COLS - 1));
        return;
    }

    if (runLeft_ <= 0) {
        LaneType t;
        const long r = random(100);
        if (r < 25) t = LaneType::Grass;
        else if (r < 60) t = LaneType::Road;
        else if (r < 82) t = LaneType::River;
        else t = LaneType::Rail;
        if (t == runType_) t = LaneType::Grass;  // give the player a breather between runs

        runType_ = t;
        switch (t) {
            case LaneType::Grass: runLeft_ = random(1, 3); break;
            case LaneType::Road: runLeft_ = random(1, 5); break;
            case LaneType::River: runLeft_ = random(1, 4); break;
            case LaneType::Rail: runLeft_ = random(1, 3); break;
        }
    }
    runLeft_--;
    ln.type = runType_;

    const float difficulty = min(1.0f, n / 200.0f);
    switch (ln.type) {
        case LaneType::Grass:
            // At most 4 trees, so neighbouring grass lanes always share free columns.
            for (long i = random(0, 5); i > 0; i--) ln.trees |= 1 << random(COLS);
            break;

        case LaneType::Road: {
            ln.dir = random(2) ? 1 : -1;
            ln.speed = (0.03f + random(30) / 1000.0f) * (1 + difficulty);
            const bool trucks = random(100) < 30;
            ln.count = trucks ? 2 : 2 + random(2);
            const float spacing = SPAN / ln.count;
            for (int i = 0; i < ln.count; i++) {
                Mover& m = ln.movers[i];
                m.w = trucks ? 46 : 26;
                m.color = CAR_COLORS[random(sizeof(CAR_COLORS) / sizeof(CAR_COLORS[0]))];
                m.x = -WRAP_MARGIN + i * spacing + random(max(1L, (long)(spacing - m.w - 40)));
            }
            break;
        }

        case LaneType::River: {
            ln.dir = (n & 1) ? 1 : -1;
            ln.speed = (0.02f + random(20) / 1000.0f) * (1 + 0.5f * difficulty);
            ln.count = 3;
            const float spacing = SPAN / ln.count;
            for (int i = 0; i < ln.count; i++) {
                Mover& m = ln.movers[i];
                m.w = TILE * (2 + random(difficulty < 0.5f ? 3 : 2));  // logs get shorter
                m.color = LOG;
                m.x = -WRAP_MARGIN + i * spacing + random(max(1L, (long)(spacing - m.w - 20)));
            }
            break;
        }

        case LaneType::Rail:
            ln.dir = random(2) ? 1 : -1;
            ln.nextTrainAt = now_ + random(2000, 6000);
            break;
    }
}

void CrossyRoad::updateLanes(uint32_t dt) {
    for (int n = max(0, lowestVisibleLane() - 1); n <= generated_; n++) {
        Lane& ln = lane(n);
        if (ln.type == LaneType::Road || ln.type == LaneType::River) {
            for (int i = 0; i < ln.count; i++) {
                Mover& m = ln.movers[i];
                m.x += ln.dir * ln.speed * dt;
                if (m.x >= cfg::SCREEN_W) m.x -= SPAN;
                else if (m.x < -WRAP_MARGIN) m.x += SPAN;
            }
        } else if (ln.type == LaneType::Rail) {
            if (ln.trainActive) {
                ln.trainX += ln.dir * TRAIN_SPEED * dt;
                if ((ln.dir > 0 && ln.trainX > cfg::SCREEN_W) || (ln.dir < 0 && ln.trainX + TRAIN_LEN < 0)) {
                    ln.trainActive = false;
                    ln.nextTrainAt = now_ + random(3000, 7000);
                }
            } else if (now_ >= ln.nextTrainAt) {
                ln.trainActive = true;
                ln.trainX = ln.dir > 0 ? -TRAIN_LEN : cfg::SCREEN_W;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Chicken

void CrossyRoad::tryHop(Dir dir) {
    const int toLane = cLane_ + (dir == Dir::Up ? 1 : 0) - (dir == Dir::Down ? 1 : 0);
    if (toLane < 0 || toLane > generated_) return;
    facing_ = dir;

    const float dx = dir == Dir::Left ? -TILE : dir == Dir::Right ? TILE : 0;
    const Lane& target = lane(toLane);
    // Riding a log keeps the exact x position; everywhere else snaps to the grid.
    float toX = (target.type == LaneType::River) ? cx_ + dx : roundf(cx_ / TILE) * TILE + dx;
    const float maxX = (COLS - 1) * TILE;
    if (toX < 0 || toX > maxX) {
        if (dx != 0) return;
        toX = constrain(toX, 0.0f, maxX);
    }
    if (target.type == LaneType::Grass && (target.trees & (1 << (int)roundf(toX / TILE)))) return;

    hopping_ = true;
    hopT_ = 0;
    hopFromLane_ = cLane_;
    hopFromX_ = cx_;
    hopToX_ = toX;
    cLane_ = toLane;
    score_ = max(score_, cLane_ - START_LANE);
}

void CrossyRoad::updateChicken(uint32_t dt) {
    if (hopping_) {
        hopT_ += dt;
        if (hopT_ >= HOP_MS) {
            hopping_ = false;
            cx_ = hopToX_;
        } else {
            cx_ = hopFromX_ + (hopToX_ - hopFromX_) * hopT_ / HOP_MS;
        }
    }
    if (hopping_) return;

    const Lane& ln = lane(cLane_);
    if (ln.type == LaneType::River) {
        const float center = cx_ + TILE / 2;
        bool onLog = false;
        for (int i = 0; i < ln.count && !onLog; i++) {
            onLog = center >= ln.movers[i].x && center <= ln.movers[i].x + ln.movers[i].w;
        }
        if (!onLog) {
            die(Death::Drowned);
            return;
        }
        cx_ += ln.dir * ln.speed * dt;
        if (cx_ < -TILE / 2 || cx_ > cfg::SCREEN_W - TILE / 2) {
            die(Death::SweptAway);
            return;
        }
    }

    if (hasPending_) {
        hasPending_ = false;
        tryHop(pending_);
    }
}

void CrossyRoad::checkCollisions() {
    const Lane& ln = lane(cLane_);
    const float l = cx_ + 4;
    const float r = cx_ + TILE - 4;

    if (ln.type == LaneType::Road) {
        for (int i = 0; i < ln.count; i++) {
            const Mover& m = ln.movers[i];
            if (r > m.x && l < m.x + m.w) {
                die(Death::Squashed);
                return;
            }
        }
    } else if (ln.type == LaneType::Rail && ln.trainActive) {
        if (r > ln.trainX && l < ln.trainX + TRAIN_LEN) die(Death::Squashed);
    }
}

float CrossyRoad::chickenScreenY() const {
    if (!hopping_) return laneY(cLane_);
    const float t = (float)hopT_ / HOP_MS;
    const float from = laneY(hopFromLane_);
    return from + (laneY(cLane_) - from) * t - sinf(t * PI) * 6;
}

// ---------------------------------------------------------------------------
// Rendering: the screen is drawn as 12 horizontal strips through one small
// sprite, which avoids flicker without needing a full-screen buffer.

void CrossyRoad::render() {
    if (!strip_ || !strip_->created()) return;
    const int lowest = lowestVisibleLane();

    for (int16_t y0 = 0; y0 < cfg::SCREEN_H; y0 += TILE) {
        for (int n = lowest; n <= lowest + ROWS; n++) {
            const int16_t sy = laneY(n);
            if (sy >= y0 + TILE || sy + TILE <= y0) continue;
            drawLane(n, sy - y0);
        }
        drawChickenLayer(y0);
        drawHud(y0);
        strip_->pushSprite(0, y0);
    }
}

void CrossyRoad::drawLane(int n, int16_t oy) {
    TFT_eSprite& g = *strip_;
    const Lane& ln = lane(n);
    const int16_t W = cfg::SCREEN_W;

    switch (ln.type) {
        case LaneType::Grass:
            g.fillRect(0, oy, W, TILE, (n & 1) ? GRASS_A : GRASS_B);
            for (int c = 0; c < COLS; c++) {
                if (!(ln.trees & (1 << c))) continue;
                g.fillRect(c * TILE + 8, oy + 12, 4, 6, TRUNK);
                g.fillRoundRect(c * TILE + 2, oy + 1, 16, 13, 5, TREE);
            }
            break;

        case LaneType::Road:
            g.fillRect(0, oy, W, TILE, ROAD);
            if (n > 0 && lane(n - 1).type == LaneType::Road) {
                for (int16_t x = 4; x < W; x += 24) g.fillRect(x, oy + TILE - 1, 12, 2, ROAD_LINE);
            }
            for (int i = 0; i < ln.count; i++) {
                const Mover& m = ln.movers[i];
                const int16_t x = (int16_t)m.x;
                g.fillRect(x + 3, oy + 2, 6, 16, TIRE);
                g.fillRect(x + m.w - 9, oy + 2, 6, 16, TIRE);
                g.fillRoundRect(x, oy + 4, m.w, 12, 3, m.color);
                if (m.w > 30) {
                    // Truck: white cargo box behind the cab
                    g.fillRoundRect(ln.dir > 0 ? x : x + 16, oy + 3, m.w - 16, 14, 2, TRUCK_BOX);
                }
                g.fillRect(ln.dir > 0 ? x + m.w - 10 : x + 6, oy + 6, 4, 8, GLASS);
            }
            break;

        case LaneType::River:
            g.fillRect(0, oy, W, TILE, WATER);
            for (int k = 0; k < 5; k++) {
                g.drawFastHLine((n * 67 + k * 83) % 300, oy + 4 + ((n + k) % 3) * 5, 10, WATER_LIGHT);
            }
            for (int i = 0; i < ln.count; i++) {
                const Mover& m = ln.movers[i];
                const int16_t x = (int16_t)m.x;
                g.fillRoundRect(x, oy + 3, m.w, 14, 5, LOG);
                g.drawFastHLine(x + 5, oy + 7, m.w - 10, LOG_DARK);
                g.drawFastHLine(x + 8, oy + 12, m.w - 16, LOG_DARK);
            }
            break;

        case LaneType::Rail: {
            g.fillRect(0, oy, W, TILE, GRAVEL);
            for (int16_t x = 3; x < W; x += 10) g.fillRect(x, oy + 3, 4, 14, SLEEPER);
            g.drawFastHLine(0, oy + 6, W, RAIL);
            g.drawFastHLine(0, oy + 13, W, RAIL);

            const bool warning = ln.trainActive || now_ + TRAIN_WARN_MS >= ln.nextTrainAt;
            const bool lit = warning && ((now_ / 150) & 1);
            g.fillRoundRect(1, oy + 3, 12, 14, 3, TIRE);
            g.fillCircle(7, oy + 10, 4, lit ? SIGNAL_ON : SIGNAL_OFF);

            if (ln.trainActive) {
                const int16_t x = (int16_t)ln.trainX;
                g.fillRoundRect(x, oy + 2, TRAIN_LEN, 16, 4, TRAIN);
                for (int i = 0; i < TRAIN_LEN / 40; i++) {
                    g.fillRect(x + i * 40 + 8, oy + 5, 24, 5, GLASS);
                    if (i > 0) g.drawFastVLine(x + i * 40, oy + 2, 16, TIRE);
                }
            }
            break;
        }
    }
}

void CrossyRoad::drawChicken(TFT_eSPI& g, int16_t x, int16_t y, Dir facing) {
    g.fillRect(x + 6, y + 17, 2, 3, BEAK);  // feet
    g.fillRect(x + 12, y + 17, 2, 3, BEAK);
    g.fillRoundRect(x + 3, y + 4, 14, 14, 4, CHICKEN);
    g.fillRect(x + 8, y + 1, 4, 4, COMB);
    switch (facing) {
        case Dir::Left:
            g.fillRect(x, y + 8, 3, 3, BEAK);
            g.fillRect(x + 5, y + 7, 2, 2, TIRE);
            break;
        case Dir::Right:
            g.fillRect(x + 17, y + 8, 3, 3, BEAK);
            g.fillRect(x + 13, y + 7, 2, 2, TIRE);
            break;
        case Dir::Down:
            g.fillRect(x + 8, y + 10, 4, 3, BEAK);
            g.fillRect(x + 6, y + 7, 2, 2, TIRE);
            g.fillRect(x + 12, y + 7, 2, 2, TIRE);
            break;
        case Dir::Up:
            g.fillRect(x + 7, y + 15, 6, 2, ROAD_LINE);  // tail feathers
            break;
    }
}

void CrossyRoad::drawChickenLayer(int16_t y0) {
    const int16_t x = (int16_t)cx_;
    const int16_t y = (int16_t)chickenScreenY();
    const int16_t ground = laneY(cLane_);
    if (y + TILE + 6 < y0 || y - 4 >= y0 + TILE) return;

    TFT_eSprite& g = *strip_;
    const int16_t oy = y - y0;

    if (state_ == State::Dying || state_ == State::GameOver) {
        if (death_ == Death::Squashed) {
            g.fillRect(x + 1, oy + 12, 18, 5, CHICKEN);
            g.fillRect(x + 8, oy + 11, 4, 2, COMB);
        } else if (death_ == Death::Drowned || death_ == Death::SweptAway) {
            const int16_t r = min<int16_t>(14, 3 + (now_ - deathAt_) / 40);
            g.drawCircle(x + 10, oy + 10, r, WATER_LIGHT);
            if (r > 6) g.drawCircle(x + 10, oy + 10, r - 5, CHICKEN);
        }
        return;
    }

    if (hopping_) g.fillEllipse(x + 10, ground - y0 + 17, 7, 2, SHADOW);
    drawChicken(g, x, oy, facing_);
}

void CrossyRoad::drawHud(int16_t y0) {
    TFT_eSprite& g = *strip_;

    if (y0 == 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Score %d", score_);
        g.setTextDatum(TL_DATUM);
        g.setTextColor(TIRE);
        g.drawString(buf, 7, 3, 2);
        g.setTextColor(CHICKEN);
        g.drawString(buf, 6, 2, 2);

        ui::drawPauseButton(g, cfg::SCREEN_W - ui::PAUSE_W - 4, 2);
    }

    if (state_ != State::Ready) return;

    // Control hints before the first hop
    const int16_t oy = -y0;
    g.fillTriangle(6, 120 + oy, 22, 108 + oy, 22, 132 + oy, CHICKEN);
    g.fillTriangle(313, 120 + oy, 297, 108 + oy, 297, 132 + oy, CHICKEN);
    g.fillTriangle(160, 234 + oy, 148, 220 + oy, 172, 220 + oy, CHICKEN);
    if (y0 >= 40 && y0 <= 100) {
        g.setTextDatum(MC_DATUM);
        g.setTextColor(TIRE);
        g.drawString("Tap to hop!", 161, 61 + oy, 4);
        g.setTextColor(CHICKEN);
        g.drawString("Tap to hop!", 160, 60 + oy, 4);
        g.drawString("edges: sideways   bottom: back", 160, 88 + oy, 2);
    }
}
