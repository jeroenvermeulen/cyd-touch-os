#pragma once

#include "App.h"
#include "UI.h"

class CrossyRoad : public App {
public:
    const char* name() const override { return "Crossy Road"; }
    void drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) override;
    void begin(TFT_eSPI& tft) override;
    void update(const TouchEvent& ev) override;
    bool finished() const override { return exit_; }
    bool suspend() override;
    void restore() override;

private:
    enum class State { Ready, Playing, Dying, Paused, GameOver };
    enum class LaneType : uint8_t { Grass, Road, River, Rail };
    enum class Dir : uint8_t { Up, Down, Left, Right };
    enum class Death : uint8_t { Squashed, Drowned, SweptAway, TooSlow };

    static constexpr int RING = 32;       // lanes kept in memory
    static constexpr int MAX_MOVERS = 4;  // cars or logs per lane

    // A car, truck or log moving along a lane.
    struct Mover {
        float x;
        int16_t w;
        uint16_t color;
    };

    struct Lane {
        LaneType type;
        int8_t dir;       // +1 moves right, -1 moves left
        float speed;      // pixels per ms
        uint16_t trees;   // grass: bitmask of blocked columns
        uint8_t count;
        Mover movers[MAX_MOVERS];
        bool trainActive;  // rail only
        float trainX;
        uint32_t nextTrainAt;
    };

    // Game flow
    void newGame();
    void step(uint32_t dt);
    void die(Death how);
    void showPause();
    void drawPauseDialog();
    void showGameOver();

    // World
    Lane& lane(int n) { return lanes_[n % RING]; }
    void ensureLanes();
    void generateLane(int n);
    void updateLanes(uint32_t dt);
    int lowestVisibleLane() const { return (int)cam_ / 20; }
    int16_t laneY(int n) const;

    // Chicken
    void tryHop(Dir dir);
    void updateChicken(uint32_t dt);
    void checkCollisions();
    float chickenScreenY() const;

    // Rendering
    void render();
    void drawLane(int n, int16_t oy);
    void drawChickenLayer(int16_t y0);
    void drawHud(int16_t y0);
    static void drawChicken(TFT_eSPI& g, int16_t x, int16_t y, Dir facing);

    TFT_eSPI* tft_ = nullptr;
    TFT_eSprite* strip_ = nullptr;
    bool exit_ = false;
    State state_ = State::Ready;
    State resumeState_ = State::Ready;  // state to return to after pausing
    uint32_t lastMs_ = 0;
    uint32_t now_ = 0;  // game clock (ms); stops while paused

    Lane lanes_[RING];
    int generated_ = -1;  // highest lane generated so far
    LaneType runType_ = LaneType::Grass;
    int runLeft_ = 0;
    float cam_ = 0;  // pixels scrolled up from the start

    int cLane_ = 0;
    float cx_ = 0;  // chicken's left edge in pixels
    Dir facing_ = Dir::Up;
    bool hopping_ = false;
    uint32_t hopT_ = 0;
    int hopFromLane_ = 0;
    float hopFromX_ = 0;
    float hopToX_ = 0;
    bool hasPending_ = false;
    Dir pending_ = Dir::Up;

    Death death_ = Death::Squashed;
    uint32_t deathAt_ = 0;
    int score_ = 0;
    int best_ = 0;
};
