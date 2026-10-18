#include "apps/PacMan.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

#include "Config.h"

namespace {
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t WALL = rgb(33, 33, 222);
constexpr uint16_t WALL_FLASH = rgb(255, 255, 255);
constexpr uint16_t DOT = rgb(255, 184, 174);
constexpr uint16_t DOOR = rgb(255, 184, 255);
constexpr uint16_t PAC = rgb(255, 255, 0);
constexpr uint16_t FRIGHT = rgb(33, 33, 255);
constexpr uint16_t FRIGHT_FACE = rgb(255, 184, 174);
constexpr uint16_t FLASH_BODY = rgb(255, 255, 255);
constexpr uint16_t FLASH_FACE = rgb(255, 0, 0);
constexpr uint16_t EYE_WHITE = rgb(255, 255, 255);
constexpr uint16_t PUPIL = rgb(33, 33, 222);
constexpr uint16_t GHOST_COLORS[] = {rgb(255, 0, 0), rgb(255, 184, 255), rgb(0, 255, 255), rgb(255, 184, 82)};

// The arcade maze. '#' wall, '.' dot, 'o' power pellet, '-' ghost-house door.
const char* const MAZE[] = {
    "############################",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#o####.#####.##.#####.####o#",
    "#.####.#####.##.#####.####.#",
    "#..........................#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#......##....##....##......#",
    "######.##### ## #####.######",
    "     #.##### ## #####.#     ",
    "     #.##          ##.#     ",
    "     #.## ###--### ##.#     ",
    "######.## #      # ##.######",
    "      .   #      #   .      ",
    "######.## #      # ##.######",
    "     #.## ######## ##.#     ",
    "     #.##          ##.#     ",
    "     #.## ######## ##.#     ",
    "######.## ######## ##.######",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#.####.#####.##.#####.####.#",
    "#o..##.......  .......##..o#",
    "###.##.##.########.##.##.###",
    "###.##.##.########.##.##.###",
    "#......##....##....##......#",
    "#.##########.##.##########.#",
    "#.##########.##.##########.#",
    "#..........................#",
    "############################",
};

enum : uint8_t { T_EMPTY, T_WALL, T_DOT, T_POWER, T_DOOR };
// Up/Left/Down/Right is also the ghosts' tie-break order.
enum : uint8_t { UP, LEFT, DOWN, RIGHT, NONE };
constexpr int8_t DX[] = {0, -1, 0, 1, 0};
constexpr int8_t DY[] = {-1, 0, 1, 0, 0};
uint8_t reverseDir(uint8_t d) { return d == NONE ? NONE : (d + 2) & 3; }

// 28x31 tiles of 7 px fill the screen height; the panel sits on the right.
constexpr int16_t TILE = 7;
constexpr int16_t MAZE_W = 28 * TILE;
constexpr int16_t MAZE_H = 31 * TILE;
constexpr int16_t MAZE_X = 6;
constexpr int16_t MAZE_Y = (cfg::SCREEN_H - MAZE_H) / 2;
constexpr int16_t PANEL_X = 208;
constexpr int16_t SPR = 24;     // compositing sprite size
constexpr int16_t ACTOR_R = 6;  // half-size of an actor's bounding box

constexpr uint32_t TICK_MS = 16;
constexpr int TICKS_PER_SEC = 1000 / TICK_MS;
constexpr int MAX_CATCHUP = 4;
constexpr uint16_t SUB = 256;  // progress units per tile

// Speeds in progress units per tick, roughly the arcade percentages.
struct Speeds {
    uint16_t pac, pacFright, ghost, ghostFright, tunnel;
};
constexpr Speeds SPEEDS[] = {{30, 34, 28, 19, 15}, {34, 36, 32, 21, 17}, {38, 38, 36, 23, 19}};
constexpr uint16_t EYES_SPEED = 76;

// Scatter/chase alternation in seconds; the last chase lasts forever.
constexpr uint16_t PHASE_SECS[] = {7, 20, 7, 20, 5, 20, 5, 0};
constexpr int LAST_PHASE = sizeof(PHASE_SECS) / sizeof(PHASE_SECS[0]) - 1;

constexpr int EXIT_X = 13;  // tile above the ghost-house door (13 and 14)
constexpr int EXIT_Y = 11;
constexpr int HOUSE_Y = 14;
constexpr int8_t SCATTER[4][2] = {{25, -3}, {2, -3}, {27, 31}, {0, 31}};
constexpr int8_t GHOST_START[4][2] = {{14, 11}, {14, 14}, {12, 14}, {16, 14}};
constexpr uint16_t RELEASE_TICKS[4] = {0, 30, 250, 500};
constexpr int8_t POWER_TILES[4][2] = {{1, 3}, {26, 3}, {1, 23}, {26, 23}};

constexpr int READY_ROW = 17;
constexpr uint16_t READY_FIRST_TICKS = 190;
constexpr uint16_t READY_TICKS = 125;
constexpr uint16_t DEATH_FREEZE = 60;
constexpr uint16_t DEATH_ANIM = 75;
constexpr uint16_t DEATH_END = DEATH_FREEZE + DEATH_ANIM + 40;
constexpr uint16_t CLEAR_FREEZE = 60;
constexpr uint16_t CLEAR_FLASH = 12;
constexpr uint16_t CLEAR_FLASHES = 8;
constexpr uint16_t CLEAR_END = CLEAR_FREEZE + CLEAR_FLASH * CLEAR_FLASHES + 30;
constexpr uint32_t FRIGHT_FLASH_TICKS = 2 * TICKS_PER_SEC;
constexpr int BONUS_LIFE_AT = 10000;
constexpr int16_t SWIPE_PX = 10;

const float MOUTH[] = {0.0f, 0.25f, 0.5f, 0.75f, 0.75f, 0.5f, 0.25f, 0.0f};  // half-angle, radians

// D-pad in the panel
constexpr int16_t PAD_CX = 262;
constexpr int16_t PAD_CY = 182;
constexpr int16_t PAD_B = 34;
constexpr int16_t PAD_REACH = PAD_B * 3 / 2 + 1;

const Button BTN_RESUME{75, 140, 80, 36, "Resume"};
const Button BTN_RETRY{75, 140, 80, 36, "Retry"};
const Button BTN_MENU{165, 140, 80, 36, "Menu"};

const char* const PREFS_NS = "pacman";

bool inPauseZone(const TouchEvent& ev) { return ev.x >= cfg::SCREEN_W - 40 && ev.y < 28; }
bool inPad(int16_t x, int16_t y) { return x >= PANEL_X && y >= PAD_CY - PAD_REACH; }

uint8_t padDir(int16_t x, int16_t y) {
    const int16_t rx = x - PAD_CX, ry = y - PAD_CY;
    const int16_t ax = abs(rx), ay = abs(ry);
    const int16_t m = max(ax, ay);
    if (m > PAD_REACH || m < 8) return NONE;
    if (ax > ay) return rx < 0 ? LEFT : RIGHT;
    return ry < 0 ? UP : DOWN;
}

// A disc with a wedge cut out towards dir, drawn as horizontal runs so the
// background (dots, walls) shows through the mouth.
void drawPacShape(TFT_eSPI& g, int16_t cx, int16_t cy, int16_t r, uint8_t dir, float mouth, uint16_t col) {
    const float c = cosf(mouth);
    const int16_t dx = DX[dir], dy = DY[dir];
    const int r2 = r * r + r;
    for (int16_t y = -r; y <= r; y++) {
        int16_t start = INT16_MIN;
        for (int16_t x = -r; x <= r + 1; x++) {
            bool in = x <= r && x * x + y * y <= r2;
            if (in && mouth > 0 && (x || y)) in = x * dx + y * dy <= c * sqrtf(x * x + y * y);
            if (in && start == INT16_MIN) {
                start = x;
            } else if (!in && start != INT16_MIN) {
                g.drawFastHLine(cx + start, cy + y, x - start, col);
                start = INT16_MIN;
            }
        }
    }
}

// 11x12 ghost centred on (cx, cy). Skirt pixels are drawn, not cut, so it
// works on any background.
void drawGhostShape(TFT_eSPI& g, int16_t cx, int16_t cy, uint16_t body, uint8_t look, bool wiggle,
                    bool eyesOnly, bool frightFace, uint16_t faceCol) {
    if (!eyesOnly) {
        g.fillCircle(cx, cy - 1, 5, body);
        g.fillRect(cx - 5, cy - 1, 11, 6, body);
        if (wiggle) {
            g.fillRect(cx - 5, cy + 5, 2, 1, body);
            g.fillRect(cx - 1, cy + 5, 3, 1, body);
            g.fillRect(cx + 4, cy + 5, 2, 1, body);
        } else {
            g.fillRect(cx - 4, cy + 5, 3, 1, body);
            g.fillRect(cx + 2, cy + 5, 3, 1, body);
        }
    }
    if (frightFace) {
        g.fillRect(cx - 3, cy - 3, 2, 2, faceCol);
        g.fillRect(cx + 2, cy - 3, 2, 2, faceCol);
        for (int i = -4; i <= 4; i++) g.drawPixel(cx + i, cy + 2 - (i & 1), faceCol);
        return;
    }
    const int8_t ex = DX[look], ey = DY[look];
    for (int16_t side : {-4, 1}) {
        const int16_t wx = cx + side + ex;
        const int16_t wy = cy - 3 + ey;
        g.fillRect(wx, wy, 3, 4, EYE_WHITE);
        g.fillRect(wx + (ex < 0 ? 0 : 1), wy + (ey < 0 ? 0 : ey > 0 ? 2 : 1), 2, 2, PUPIL);
    }
}
}  // namespace

// ---------------------------------------------------------------------------
// App interface

void PacMan::drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) {
    // A tiny maze: a corridor all the way round a block, with Blinky chasing
    // Pac-Man along the top at their in-game size, and a power pellet.
    tft.fillRoundRect(x, y, w, h, 5, color::BG);
    tft.drawRoundRect(x, y, w, h, 5, WALL);
    tft.drawRoundRect(x + 3, y + 3, w - 6, h - 6, 3, WALL);
    tft.drawRoundRect(x + 20, y + 20, w - 40, h - 40, 3, WALL);

    const int16_t left = x + 12, right = x + w - 12, top = y + 12, bottom = y + h - 12;
    auto dot = [&](int16_t dx, int16_t dy) { tft.fillRect(dx - 1, dy - 1, 2, 2, DOT); };
    for (int16_t dx = x + 44; dx <= right; dx += 8) dot(dx, top);
    for (int16_t dy = top + 10; dy < bottom; dy += 10) {
        dot(left, dy);
        dot(right, dy);
    }
    for (int16_t dx = left + 10; dx <= right; dx += 10) dot(dx, bottom);
    tft.fillCircle(left, bottom, 2, DOT);  // power pellet

    drawGhostShape(tft, x + 17, top, GHOST_COLORS[0], RIGHT, false, false, false, 0);
    drawPacShape(tft, x + 34, top, 5, RIGHT, 0.6f, PAC);
}

void PacMan::begin(TFT_eSPI& tft) {
    tft_ = &tft;
    exit_ = false;

    if (!spr_) {
        spr_ = new TFT_eSprite(&tft);
        sprOk_ = spr_->createSprite(SPR, SPR) != nullptr;
        if (!sprOk_) Serial.println("PacMan: sprite alloc failed");
    }

    Preferences prefs;
    prefs.begin(PREFS_NS, true);
    best_ = prefs.getInt("best", 0);
    prefs.end();

    newGame();
}

void PacMan::update(const TouchEvent& ev) {
    const uint32_t now = millis();

    switch (state_) {
        case State::Paused:
            if (!ev.tapped) return;
            if (BTN_RESUME.contains(ev.x, ev.y)) {
                state_ = pausedFrom_;
                drawAll();
                nextTickAt_ = now + TICK_MS;
            } else if (BTN_MENU.contains(ev.x, ev.y)) {
                saveBest();
                exit_ = true;
            }
            return;

        case State::GameOver:
            if (!ev.tapped) return;
            if (BTN_RETRY.contains(ev.x, ev.y)) newGame();
            else if (BTN_MENU.contains(ev.x, ev.y)) exit_ = true;
            return;

        default:
            break;
    }

    if (ev.tapped && inPauseZone(ev)) {
        showPause();
        return;
    }
    steer(ev);

    if ((int32_t)(now - nextTickAt_) < 0) return;
    for (int n = 0; n < MAX_CATCHUP && (int32_t)(now - nextTickAt_) >= 0; n++) {
        nextTickAt_ += TICK_MS;
        tick();
        if (state_ == State::GameOver) return;
    }
    // Drop any backlog rather than fast-forwarding.
    if ((int32_t)(now - nextTickAt_) >= 0) nextTickAt_ = now + TICK_MS;
    render();
}

// The screensaver pauses the game.
bool PacMan::suspend() {
    if (state_ != State::Paused && state_ != State::GameOver) showPause();
    return true;
}

// Back from the screensaver: the game as it was, under its dialog.
void PacMan::restore() {
    drawAll();
    if (state_ == State::Paused) drawPauseDialog();
    else showGameOver();
}

// ---------------------------------------------------------------------------
// Game flow

void PacMan::newGame() {
    randomSeed(esp_random());
    score_ = 0;
    lives_ = 2;
    level_ = 1;
    bonusLife_ = false;
    frame_ = 0;
    wallColor_ = WALL;
    loadMaze();
    resetRound();
    startReady(READY_FIRST_TICKS);
    drawAll();
    nextTickAt_ = millis() + TICK_MS;
}

void PacMan::nextLevel() {
    level_++;
    wallColor_ = WALL;
    loadMaze();
    resetRound();
    startReady(READY_TICKS);
    drawAll();
}

void PacMan::loadMaze() {
    dotsLeft_ = 0;
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            uint8_t t = T_EMPTY;
            switch (MAZE[y][x]) {
                case '#': t = T_WALL; break;
                case '.': t = T_DOT; break;
                case 'o': t = T_POWER; break;
                case '-': t = T_DOOR; break;
            }
            tiles_[y][x] = t;
            if (t == T_DOT || t == T_POWER) dotsLeft_++;
        }
    }

    // Wall outlines run through the middle of wall tiles that border open
    // space. Two neighbouring wall tiles are joined only along an open side,
    // so thick walls get an outline instead of a ladder.
    auto solid = [&](int x, int y) {
        return x >= 0 && x < COLS && y >= 0 && y < ROWS && (tiles_[y][x] == T_WALL || tiles_[y][x] == T_DOOR);
    };
    auto openTile = [&](int x, int y) { return x >= 0 && x < COLS && y >= 0 && y < ROWS && !solid(x, y); };
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            walls_[y][x] = 0;
            if (tiles_[y][x] != T_WALL) continue;
            for (uint8_t d = 0; d < 4; d++) {
                const int nx = x + DX[d], ny = y + DY[d];
                if (!solid(nx, ny)) continue;
                const int px = DY[d] ? 1 : 0, py = DX[d] ? 1 : 0;
                if (openTile(x + px, y + py) || openTile(nx + px, ny + py) || openTile(x - px, y - py) ||
                    openTile(nx - px, ny - py))
                    walls_[y][x] |= 1 << d;
            }
        }
    }
}

void PacMan::resetRound() {
    pac_.tx = 14;  // halfway between tiles 13 and 14, like the arcade
    pac_.ty = 23;
    pac_.dir = LEFT;
    pac_.p = SUB / 2;
    pacMoving_ = true;
    want_ = NONE;

    for (int i = 0; i < GHOSTS; i++) {
        Ghost& g = ghosts_[i];
        g.tx = GHOST_START[i][0];
        g.ty = GHOST_START[i][1];
        g.dir = LEFT;
        g.p = SUB / 2;
        g.mode = i == 0 ? Mode::Active : Mode::Waiting;
        g.frightened = false;
        g.releaseTick = RELEASE_TICKS[i] / (level_ > 1 ? 2 : 1);
    }

    phase_ = 0;
    phaseTicks_ = PHASE_SECS[0] * TICKS_PER_SEC;
    frightTicks_ = 0;
    ghostsEaten_ = 0;
    roundTick_ = 0;
}

void PacMan::startReady(uint16_t ticks) {
    state_ = State::Ready;
    stateTicks_ = ticks;
}

void PacMan::tick() {
    frame_++;
    switch (state_) {
        case State::Ready:
            if (stateTicks_ == 0 || --stateTicks_ == 0) {
                eraseReadyText();
                state_ = State::Playing;
            }
            break;

        case State::Playing:
            playTick();
            break;

        case State::Dying:
            if (++stateTicks_ < DEATH_END) break;
            if (lives_ == 0) {
                showGameOver();
                break;
            }
            lives_--;
            drawLives();
            resetRound();
            startReady(READY_TICKS);
            drawPad();
            renderActors();
            drawReadyText();
            break;

        case State::LevelClear: {
            ++stateTicks_;
            const int t = stateTicks_ - CLEAR_FREEZE;
            if (t >= 0 && t < CLEAR_FLASH * CLEAR_FLASHES && t % CLEAR_FLASH == 0) {
                wallColor_ = wallColor_ == WALL ? WALL_FLASH : WALL;
                drawWalls();
            }
            if (stateTicks_ >= CLEAR_END) nextLevel();
            break;
        }

        default:
            break;
    }
}

void PacMan::playTick() {
    roundTick_++;

    // The scatter/chase clock stops while the ghosts are frightened.
    if (frightTicks_ > 0) {
        if (--frightTicks_ == 0) {
            for (Ghost& g : ghosts_) g.frightened = false;
        }
    } else if (phaseTicks_ > 0 && --phaseTicks_ == 0 && phase_ < LAST_PHASE) {
        phase_++;
        phaseTicks_ = PHASE_SECS[phase_] * TICKS_PER_SEC;
        for (Ghost& g : ghosts_) {
            if (g.mode == Mode::Active) reverseGhost(g);
        }
    }

    for (Ghost& g : ghosts_) {
        if (g.mode == Mode::Waiting && roundTick_ >= g.releaseTick) g.mode = Mode::Leaving;
    }

    movePac();
    if (state_ != State::Playing) return;
    checkCollisions();
    if (state_ != State::Playing) return;
    for (int i = 0; i < GHOSTS; i++) moveGhost(ghosts_[i], i);
    checkCollisions();
}

void PacMan::movePac() {
    if (want_ != NONE) {
        if (want_ == reverseDir(pac_.dir) && pac_.p > 0) {
            reverse(pac_);
        } else if (pac_.p == 0 && open(pac_.tx, pac_.ty, want_, false)) {
            pac_.dir = want_;
            pacMoving_ = true;
        }
    }
    if (!pacMoving_) return;

    pac_.p += pacSpeed();
    while (pac_.p >= SUB) {
        pac_.p -= SUB;
        advanceTile(pac_);
        eat(pac_.tx, pac_.ty);
        if (state_ != State::Playing) return;
        if (want_ != NONE && open(pac_.tx, pac_.ty, want_, false)) {
            pac_.dir = want_;
        } else if (!open(pac_.tx, pac_.ty, pac_.dir, false)) {
            pac_.p = 0;
            pacMoving_ = false;
        }
    }
}

void PacMan::moveGhost(Ghost& g, int i) {
    if (g.mode == Mode::Waiting) return;
    g.p += ghostSpeed(g);
    while (g.p >= SUB) {
        g.p -= SUB;
        advanceTile(g);
        ghostArrived(g, i);
    }
}

void PacMan::ghostArrived(Ghost& g, int i) {
    bool allowReverse = false;
    switch (g.mode) {
        case Mode::Leaving:
            if (g.ty == EXIT_Y) g.mode = Mode::Active;
            break;
        case Mode::Eyes:
            if (g.ty == EXIT_Y && (g.tx == EXIT_X || g.tx == EXIT_X + 1)) g.mode = Mode::Entering;
            break;
        case Mode::Entering:
            // Back inside the house: revive and head straight out again.
            if (g.ty >= HOUSE_Y - 1) {
                g.mode = Mode::Leaving;
                allowReverse = true;
            }
            break;
        default:
            break;
    }
    chooseGhostDir(g, i, allowReverse);
}

// Arcade ghost steering: at each tile pick the open direction (never straight
// back) that gets closest to the target; frightened ghosts pick at random.
void PacMan::chooseGhostDir(Ghost& g, int i, bool allowReverse) {
    int targetX, targetY;
    ghostTarget(g, i, targetX, targetY);
    const bool doorOk = g.mode == Mode::Leaving || g.mode == Mode::Entering;
    const bool randomly = g.frightened && g.mode == Mode::Active;
    const uint8_t back = reverseDir(g.dir);

    uint8_t options[4];
    int n = 0;
    uint8_t best = NONE;
    int32_t bestDist = INT32_MAX;
    for (uint8_t d = 0; d < 4; d++) {
        if (d == back && !allowReverse) continue;
        if (!open(g.tx, g.ty, d, doorOk)) continue;
        options[n++] = d;
        const int32_t dx = g.tx + DX[d] - targetX;
        const int32_t dy = g.ty + DY[d] - targetY;
        const int32_t dist = dx * dx + dy * dy;
        if (dist < bestDist) {
            bestDist = dist;
            best = d;
        }
    }
    if (n == 0) g.dir = back;  // dead end
    else g.dir = randomly ? options[random(n)] : best;
}

void PacMan::ghostTarget(const Ghost& g, int i, int& x, int& y) const {
    switch (g.mode) {
        case Mode::Leaving:
        case Mode::Eyes:
            x = EXIT_X;
            y = EXIT_Y;
            return;
        case Mode::Entering:
            x = EXIT_X;
            y = HOUSE_Y;
            return;
        default:
            break;
    }

    x = SCATTER[i][0];
    y = SCATTER[i][1];
    if (phase_ % 2 == 0) return;

    const int px = pac_.tx + (pac_.p >= SUB / 2 ? DX[pac_.dir] : 0);
    const int py = pac_.ty + (pac_.p >= SUB / 2 ? DY[pac_.dir] : 0);
    switch (i) {
        case 0:  // Blinky: straight at Pac-Man
            x = px;
            y = py;
            break;
        case 1:  // Pinky: four tiles ahead
            x = px + 4 * DX[pac_.dir];
            y = py + 4 * DY[pac_.dir];
            break;
        case 2: {  // Inky: two tiles ahead, mirrored around Blinky
            const int ax = px + 2 * DX[pac_.dir];
            const int ay = py + 2 * DY[pac_.dir];
            x = 2 * ax - ghosts_[0].tx;
            y = 2 * ay - ghosts_[0].ty;
            break;
        }
        case 3: {  // Clyde: chases from afar, retreats when close
            const int dx = g.tx - px, dy = g.ty - py;
            if (dx * dx + dy * dy > 64) {
                x = px;
                y = py;
            }
            break;
        }
    }
}

void PacMan::reverseGhost(Ghost& g) {
    const uint8_t back = reverseDir(g.dir);
    if (g.p > 0) reverse(g);
    else if (open(g.tx, g.ty, back, false)) g.dir = back;
}

void PacMan::eat(int tx, int ty) {
    uint8_t& t = tiles_[ty][tx];
    if (t == T_DOT) {
        t = T_EMPTY;
        addScore(10);
    } else if (t == T_POWER) {
        t = T_EMPTY;
        addScore(50);
        startFright();
    } else {
        return;
    }
    if (--dotsLeft_ == 0) {
        state_ = State::LevelClear;
        stateTicks_ = 0;
    }
}

void PacMan::startFright() {
    frightTicks_ = max(1, 7 - level_) * TICKS_PER_SEC;
    ghostsEaten_ = 0;
    for (Ghost& g : ghosts_) {
        if (g.mode == Mode::Eyes || g.mode == Mode::Entering) continue;
        g.frightened = true;
        if (g.mode == Mode::Active) reverseGhost(g);
    }
}

void PacMan::checkCollisions() {
    int16_t px, py;
    actorPos(GHOSTS, px, py);
    for (int i = 0; i < GHOSTS; i++) {
        Ghost& g = ghosts_[i];
        if (g.mode != Mode::Active && g.mode != Mode::Leaving) continue;
        int16_t gx, gy;
        actorPos(i, gx, gy);
        if (abs(gx - px) + abs(gy - py) >= 6) continue;
        if (g.frightened) {
            g.frightened = false;
            g.mode = Mode::Eyes;
            addScore(200 << min(ghostsEaten_, 3));
            ghostsEaten_++;
        } else {
            state_ = State::Dying;
            stateTicks_ = 0;
            return;
        }
    }
}

void PacMan::addScore(int points) {
    score_ += points;
    if (!bonusLife_ && score_ >= BONUS_LIFE_AT) {
        bonusLife_ = true;
        lives_++;
        drawLives();
    }
    drawScore();
    if (score_ > best_) {
        best_ = score_;
        drawBest();
    }
}

void PacMan::saveBest() {
    if (score_ == 0 || score_ < best_) return;
    Preferences prefs;
    prefs.begin(PREFS_NS, false);
    prefs.putInt("best", best_);
    prefs.end();
}

// Swipe anywhere to steer (the anchor follows the finger, so one long drag
// can make several turns), or hold the D-pad. The last direction is kept
// until Pac-Man can take it.
void PacMan::steer(const TouchEvent& ev) {
    if (ev.tapped) {
        padTouch_ = inPad(ev.x, ev.y);
        swipeX_ = ev.x;
        swipeY_ = ev.y;
    }
    if (!ev.touching) return;

    if (padTouch_) {
        const uint8_t d = padDir(ev.x, ev.y);
        if (d != NONE) setWant(d);
        return;
    }
    const int16_t dx = ev.x - swipeX_, dy = ev.y - swipeY_;
    if (abs(dx) < SWIPE_PX && abs(dy) < SWIPE_PX) return;
    setWant(abs(dx) > abs(dy) ? (dx < 0 ? LEFT : RIGHT) : (dy < 0 ? UP : DOWN));
    swipeX_ = ev.x;
    swipeY_ = ev.y;
}

void PacMan::setWant(uint8_t dir) {
    if (dir == want_) return;
    want_ = dir;
    drawPad();
}

void PacMan::showPause() {
    pausedFrom_ = state_;
    state_ = State::Paused;
    drawPauseDialog();
}

void PacMan::drawPauseDialog() {
    ui::drawDialog(*tft_, "Paused");
    char buf[32];
    snprintf(buf, sizeof(buf), "Score %d   Best %d", score_, best_);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->drawString(buf, cfg::SCREEN_W / 2, 100, 2);
    BTN_RESUME.draw(*tft_);
    BTN_MENU.draw(*tft_);
}

void PacMan::showGameOver() {
    saveBest();
    state_ = State::GameOver;
    ui::drawDialog(*tft_, "Game Over");
    char buf[32];
    snprintf(buf, sizeof(buf), "Score %d   Best %d", score_, best_);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->drawString(buf, cfg::SCREEN_W / 2, 95, 2);
    snprintf(buf, sizeof(buf), "Level %d", level_);
    tft_->setTextColor(color::EDGE_SHADOW, color::FACE);
    tft_->drawString(buf, cfg::SCREEN_W / 2, 115, 2);
    BTN_RETRY.draw(*tft_);
    BTN_MENU.draw(*tft_);
}

// ---------------------------------------------------------------------------
// Movement helpers

bool PacMan::open(int tx, int ty, uint8_t dir, bool doorOk) const {
    int nx = tx + DX[dir];
    const int ny = ty + DY[dir];
    if (ny < 0 || ny >= ROWS) return false;
    if (nx < 0) nx = COLS - 1;
    else if (nx >= COLS) nx = 0;
    const uint8_t t = tiles_[ny][nx];
    if (t == T_WALL) return false;
    if (t == T_DOOR) return doorOk;
    return true;
}

void PacMan::advanceTile(Actor& a) const {
    a.tx += DX[a.dir];
    a.ty += DY[a.dir];
    if (a.tx < 0) a.tx = COLS - 1;  // side tunnel
    else if (a.tx >= COLS) a.tx = 0;
}

// Turn around mid-step: walk from the target tile back to where we came from.
void PacMan::reverse(Actor& a) const {
    advanceTile(a);
    a.p = SUB - a.p;
    a.dir = reverseDir(a.dir);
}

void PacMan::actorPos(int i, int16_t& x, int16_t& y) const {
    const Actor& a = actor(i);
    const int16_t step = a.p * TILE / SUB;
    x = a.tx * TILE + TILE / 2 + DX[a.dir] * step;
    y = a.ty * TILE + TILE / 2 + DY[a.dir] * step;
}

bool PacMan::actorVisible(int i) const {
    if (i == GHOSTS) return state_ != State::Dying || stateTicks_ < DEATH_FREEZE + DEATH_ANIM;
    if (state_ == State::Dying) return stateTicks_ < DEATH_FREEZE;
    if (state_ == State::LevelClear) return stateTicks_ < CLEAR_FREEZE;
    return true;
}

uint16_t PacMan::pacSpeed() const {
    const Speeds& s = SPEEDS[level_ == 1 ? 0 : level_ <= 4 ? 1 : 2];
    return frightTicks_ ? s.pacFright : s.pac;
}

uint16_t PacMan::ghostSpeed(const Ghost& g) const {
    const Speeds& s = SPEEDS[level_ == 1 ? 0 : level_ <= 4 ? 1 : 2];
    if (g.mode == Mode::Eyes || g.mode == Mode::Entering) return EYES_SPEED;
    if (g.ty == 14 && (g.tx <= 5 || g.tx >= 22)) return s.tunnel;
    return g.frightened ? s.ghostFright : s.ghost;
}

// ---------------------------------------------------------------------------
// Rendering: each tick only the boxes around the actors are recomposed in a
// small sprite (maze tiles + every actor overlapping it), then pushed.

void PacMan::drawAll() {
    tft_->fillScreen(color::BG);
    ui::drawPanel(*tft_, PANEL_X - 4, 0, cfg::SCREEN_W - PANEL_X + 4, cfg::SCREEN_H);
    drawMaze();
    drawPanel();
    for (bool& d : drawn_) d = false;
    renderActors();
    if (state_ == State::Ready) drawReadyText();
}

void PacMan::drawMaze() {
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) drawTile(*tft_, x, y, MAZE_X, MAZE_Y);
    }
}

// Recolours the outlines in place; nothing else touches those pixels.
void PacMan::drawWalls() {
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (tiles_[y][x] == T_WALL) drawTile(*tft_, x, y, MAZE_X, MAZE_Y);
        }
    }
}

void PacMan::drawTile(TFT_eSPI& g, int tx, int ty, int16_t ox, int16_t oy) const {
    const int16_t x0 = ox + tx * TILE, y0 = oy + ty * TILE;
    const int16_t cx = x0 + TILE / 2, cy = y0 + TILE / 2;
    switch (tiles_[ty][tx]) {
        case T_DOT:
            g.fillRect(cx, cy, 2, 2, DOT);
            break;
        case T_POWER:
            if (blinkOn_) g.fillCircle(cx, cy, 3, DOT);
            break;
        case T_DOOR:
            g.fillRect(x0, cy - 1, TILE, 2, DOOR);
            break;
        case T_WALL: {
            const uint8_t m = walls_[ty][tx];
            if (m & (1 << UP)) g.drawFastVLine(cx, y0, TILE / 2 + 1, wallColor_);
            if (m & (1 << DOWN)) g.drawFastVLine(cx, cy, TILE - TILE / 2, wallColor_);
            if (m & (1 << LEFT)) g.drawFastHLine(x0, cy, TILE / 2 + 1, wallColor_);
            if (m & (1 << RIGHT)) g.drawFastHLine(cx, cy, TILE - TILE / 2, wallColor_);
            break;
        }
    }
}

void PacMan::drawActor(TFT_eSPI& g, int i, int16_t ox, int16_t oy) const {
    int16_t x, y;
    actorPos(i, x, y);
    x += ox;
    y += oy;

    if (i == GHOSTS) {
        uint8_t dir = pac_.dir;
        float mouth = pacMoving_ ? MOUTH[(frame_ / 2) % 8] : 0.6f;
        if (state_ == State::Ready || state_ == State::LevelClear) {
            mouth = 0;
        } else if (state_ == State::Dying && stateTicks_ >= DEATH_FREEZE) {
            // Opens up from the top until nothing is left.
            dir = UP;
            mouth = 0.2f + (PI - 0.2f) * (stateTicks_ - DEATH_FREEZE) / DEATH_ANIM;
        }
        drawPacShape(g, x, y, 5, dir, mouth, PAC);
        return;
    }

    const Ghost& gh = ghosts_[i];
    const bool wiggle = (frame_ / 8) & 1;
    const bool eyes = gh.mode == Mode::Eyes || gh.mode == Mode::Entering;
    if (gh.frightened && !eyes) {
        const bool flash = frightTicks_ < FRIGHT_FLASH_TICKS && ((frightTicks_ / 8) & 1);
        drawGhostShape(g, x, y, flash ? FLASH_BODY : FRIGHT, 0, wiggle, false, true,
                       flash ? FLASH_FACE : FRIGHT_FACE);
    } else {
        drawGhostShape(g, x, y, GHOST_COLORS[i], gh.dir, wiggle, eyes, false, 0);
    }
}

// Draws maze rect (x, y, w, h) with g's origin at maze pixel (-ox, -oy).
void PacMan::drawRegion(TFT_eSPI& g, int16_t x, int16_t y, int16_t w, int16_t h, int16_t ox, int16_t oy) const {
    for (int ty = y / TILE; ty <= (y + h - 1) / TILE; ty++) {
        for (int tx = x / TILE; tx <= (x + w - 1) / TILE; tx++) drawTile(g, tx, ty, ox, oy);
    }
    for (int i = 0; i < ACTORS; i++) {
        if (!actorVisible(i)) continue;
        int16_t ax, ay;
        actorPos(i, ax, ay);
        if (ax + ACTOR_R < x || ax - ACTOR_R >= x + w || ay + ACTOR_R < y || ay - ACTOR_R >= y + h) continue;
        drawActor(g, i, ox, oy);
    }
}

void PacMan::composeRect(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    w = min<int16_t>(w, MAZE_W - x);
    h = min<int16_t>(h, MAZE_H - y);
    if (w <= 0 || h <= 0) return;

    if (!sprOk_) {  // fallback: draw straight to the screen (flickers)
        tft_->fillRect(MAZE_X + x, MAZE_Y + y, w, h, color::BG);
        drawRegion(*tft_, x, y, w, h, MAZE_X, MAZE_Y);
        return;
    }
    for (int16_t cy = y; cy < y + h; cy += SPR) {
        for (int16_t cx = x; cx < x + w; cx += SPR) {
            const int16_t cw = min<int16_t>(SPR, x + w - cx);
            const int16_t ch = min<int16_t>(SPR, y + h - cy);
            spr_->fillRect(0, 0, cw, ch, color::BG);
            drawRegion(*spr_, cx, cy, cw, ch, -cx, -cy);
            spr_->pushSprite(MAZE_X + cx, MAZE_Y + cy, 0, 0, cw, ch);
        }
    }
}

void PacMan::render() {
    const bool blink = (frame_ / 12) % 2 == 0;
    if (blink != blinkOn_) {
        blinkOn_ = blink;
        for (const auto& t : POWER_TILES) {
            if (tiles_[t[1]][t[0]] == T_POWER) composeRect(t[0] * TILE, t[1] * TILE, TILE, TILE);
        }
    }
    renderActors();
}

// Redraws each actor's old and new box together, so moves don't flicker.
void PacMan::renderActors() {
    for (int i = 0; i < ACTORS; i++) {
        const bool vis = actorVisible(i);
        if (!vis && !drawn_[i]) continue;
        int16_t x, y;
        actorPos(i, x, y);

        int16_t l = INT16_MAX, t = INT16_MAX, r = INT16_MIN, b = INT16_MIN;
        auto add = [&](int16_t cx, int16_t cy) {
            l = min<int16_t>(l, cx - ACTOR_R);
            t = min<int16_t>(t, cy - ACTOR_R);
            r = max<int16_t>(r, cx + ACTOR_R);
            b = max<int16_t>(b, cy + ACTOR_R);
        };
        if (drawn_[i]) add(drawnX_[i], drawnY_[i]);
        if (vis) add(x, y);
        drawn_[i] = vis;
        drawnX_[i] = x;
        drawnY_[i] = y;
        composeRect(l, t, r - l + 1, b - t + 1);
    }
}

void PacMan::drawReadyText() {
    tft_->setTextDatum(MC_DATUM);
    tft_->setTextColor(PAC);
    tft_->drawString("READY!", MAZE_X + MAZE_W / 2, MAZE_Y + READY_ROW * TILE + TILE / 2, 2);
}

void PacMan::eraseReadyText() {
    composeRect(MAZE_W / 2 - 30, READY_ROW * TILE - 6, 60, TILE + 12);
}

void PacMan::drawPanel() {
    tft_->setTextDatum(TL_DATUM);
    tft_->setTextColor(color::EDGE_SHADOW, color::FACE);
    tft_->drawString("Score", PANEL_X, 4, 2);
    drawScore();
    drawBest();
    drawLevel();
    drawLives();
    drawPad();
    ui::drawPauseButton(*tft_, cfg::SCREEN_W - ui::PAUSE_W - 4, 3);
}

void PacMan::drawScore() {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", score_);
    tft_->setTextDatum(TL_DATUM);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->setTextPadding(cfg::SCREEN_W - PANEL_X - 4);
    tft_->drawString(buf, PANEL_X, 22, 4);
    tft_->setTextPadding(0);
}

void PacMan::drawBest() {
    char buf[20];
    snprintf(buf, sizeof(buf), "Best %d", best_);
    tft_->setTextDatum(TL_DATUM);
    tft_->setTextColor(color::EDGE_SHADOW, color::FACE);
    tft_->setTextPadding(cfg::SCREEN_W - PANEL_X - 4);
    tft_->drawString(buf, PANEL_X, 54, 2);
    tft_->setTextPadding(0);
}

void PacMan::drawLevel() {
    char buf[16];
    snprintf(buf, sizeof(buf), "Level %d", level_);
    tft_->setTextDatum(TL_DATUM);
    tft_->setTextColor(color::TITLE, color::FACE);
    tft_->setTextPadding(cfg::SCREEN_W - PANEL_X - 4);
    tft_->drawString(buf, PANEL_X, 72, 2);
    tft_->setTextPadding(0);
}

void PacMan::drawLives() {
    // Spare lives sit in a dark well so the yellow stands out.
    ui::drawPanel(*tft_, PANEL_X, 92, cfg::SCREEN_W - PANEL_X - 4, 21, ui::Bevel::Sunken, color::BG);
    for (int i = 0; i < min(lives_, 7); i++) drawPacShape(*tft_, PANEL_X + 6 + i * 15, 102, 5, LEFT, 0.6f, PAC);
}

void PacMan::drawPad() {
    for (uint8_t d = 0; d < 4; d++) {
        const int16_t cx = PAD_CX + DX[d] * PAD_B;
        const int16_t cy = PAD_CY + DY[d] * PAD_B;
        const bool on = d == want_;
        // The current direction stays pushed in.
        ui::drawPanel(*tft_, cx - PAD_B / 2 + 1, cy - PAD_B / 2 + 1, PAD_B - 2, PAD_B - 2,
                      on ? ui::Bevel::Sunken : ui::Bevel::Raised);
        // Arrow: tip towards d, base across it.
        const int16_t ax = cx + (on ? 1 : 0), ay = cy + (on ? 1 : 0);
        const int16_t px = DY[d], py = DX[d];
        tft_->fillTriangle(ax + DX[d] * 7, ay + DY[d] * 7, ax - DX[d] * 5 + px * 7, ay - DY[d] * 5 + py * 7,
                           ax - DX[d] * 5 - px * 7, ay - DY[d] * 5 - py * 7, on ? color::TITLE : color::FACE_TEXT);
    }
}
