#pragma once

#include "App.h"
#include "UI.h"

class PacMan : public App {
public:
    const char* name() const override { return "Pac-Man"; }
    void drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) override;
    void begin(TFT_eSPI& tft) override;
    void update(const TouchEvent& ev) override;
    bool finished() const override { return exit_; }
    bool suspend() override;
    void restore() override;

private:
    enum class State { Ready, Playing, Dying, LevelClear, Paused, GameOver };
    enum class Mode : uint8_t { Waiting, Leaving, Active, Eyes, Entering };

    static constexpr int COLS = 28;
    static constexpr int ROWS = 31;
    static constexpr int GHOSTS = 4;
    static constexpr int ACTORS = GHOSTS + 1;  // ghosts first, Pac-Man last

    // Grid movement: an actor walks from tile (tx, ty) to the next tile in
    // direction dir; p is the progress along that step (0..255).
    struct Actor {
        int8_t tx, ty;
        uint8_t dir;
        uint16_t p;
    };
    struct Ghost : Actor {
        Mode mode;
        bool frightened;
        uint16_t releaseTick;
    };

    // Game flow
    void newGame();
    void nextLevel();
    void loadMaze();
    void resetRound();
    void startReady(uint16_t ticks);
    void tick();
    void playTick();
    void movePac();
    void moveGhost(Ghost& g, int i);
    void ghostArrived(Ghost& g, int i);
    void chooseGhostDir(Ghost& g, int i, bool allowReverse);
    void ghostTarget(const Ghost& g, int i, int& x, int& y) const;
    void reverseGhost(Ghost& g);
    void eat(int tx, int ty);
    void startFright();
    void checkCollisions();
    void addScore(int points);
    void saveBest();
    void steer(const TouchEvent& ev);
    void setWant(uint8_t dir);
    void showPause();
    void drawPauseDialog();
    void showGameOver();

    // Movement helpers
    bool open(int tx, int ty, uint8_t dir, bool doorOk) const;
    void advanceTile(Actor& a) const;
    void reverse(Actor& a) const;
    const Actor& actor(int i) const { return i < GHOSTS ? ghosts_[i] : pac_; }
    void actorPos(int i, int16_t& x, int16_t& y) const;
    bool actorVisible(int i) const;
    uint16_t pacSpeed() const;
    uint16_t ghostSpeed(const Ghost& g) const;

    // Rendering
    void drawAll();
    void drawMaze();
    void drawWalls();
    void drawTile(TFT_eSPI& g, int tx, int ty, int16_t ox, int16_t oy) const;
    void drawActor(TFT_eSPI& g, int i, int16_t ox, int16_t oy) const;
    void drawRegion(TFT_eSPI& g, int16_t x, int16_t y, int16_t w, int16_t h, int16_t ox, int16_t oy) const;
    void composeRect(int16_t x, int16_t y, int16_t w, int16_t h);
    void render();
    void renderActors();
    void drawReadyText();
    void eraseReadyText();
    void drawPanel();
    void drawScore();
    void drawBest();
    void drawLevel();
    void drawLives();
    void drawPad();

    TFT_eSPI* tft_ = nullptr;
    TFT_eSprite* spr_ = nullptr;  // small compositing buffer for flicker-free redraws
    bool sprOk_ = false;
    bool exit_ = false;
    State state_ = State::Ready;
    State pausedFrom_ = State::Ready;
    uint32_t nextTickAt_ = 0;
    uint32_t frame_ = 0;      // ticks since the game started; drives animations
    uint32_t roundTick_ = 0;  // ticks since the round started; drives ghost release
    uint16_t stateTicks_ = 0;

    uint8_t tiles_[ROWS][COLS];
    uint8_t walls_[ROWS][COLS];  // wall outline segments per tile (bit per direction)
    int dotsLeft_ = 0;
    uint16_t wallColor_ = 0;
    bool blinkOn_ = true;

    Actor pac_;
    bool pacMoving_ = true;
    uint8_t want_ = 0;  // direction the player asked for, kept until it can be taken
    Ghost ghosts_[GHOSTS];

    int phase_ = 0;  // index into the scatter/chase schedule; even = scatter
    uint32_t phaseTicks_ = 0;
    uint32_t frightTicks_ = 0;
    int ghostsEaten_ = 0;  // during the current fright, for 200/400/800/1600

    bool drawn_[ACTORS] = {};
    int16_t drawnX_[ACTORS] = {};
    int16_t drawnY_[ACTORS] = {};

    bool padTouch_ = false;
    int16_t swipeX_ = 0;
    int16_t swipeY_ = 0;

    int score_ = 0;
    int best_ = 0;
    int lives_ = 0;  // spare lives
    int level_ = 1;
    bool bonusLife_ = false;
};
