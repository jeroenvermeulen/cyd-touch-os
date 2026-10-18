#pragma once

#include "App.h"
#include "UI.h"

class Snake : public App {
public:
    const char* name() const override { return "Snake"; }
    void drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) override;
    void begin(TFT_eSPI& tft) override;
    void update(const TouchEvent& ev) override;
    bool finished() const override { return exit_; }
    bool suspend() override;
    void restore() override;

private:
    enum class State { Ready, Playing, Dying, Paused, GameOver };
    enum class Dir : uint8_t { Up, Down, Left, Right };

    static constexpr int COLS = 32;
    static constexpr int ROWS = 22;
    static constexpr int CELLS = COLS * ROWS;
    static constexpr int QUEUE = 2;  // turns buffered between steps

    // Game flow
    void newGame();
    void step();
    void queueTurn(int16_t x, int16_t y);
    void die();
    void placeFood();
    void showPause();
    void drawPauseDialog();
    void showGameOver();

    // Snake body: ring buffer of cell indices (y * COLS + x), plus an
    // occupancy bitmap so collision checks don't have to walk the body.
    uint16_t segment(int i) const { return body_[(head_ - i + CELLS) % CELLS]; }
    bool occupied(uint16_t c) const { return occ_[c >> 3] & (1 << (c & 7)); }
    void setOccupied(uint16_t c, bool on);

    // Rendering
    void drawField();
    void drawHud();
    void drawHint();
    void eraseCell(uint16_t c);
    void drawBody(uint16_t c);
    void drawHead(uint16_t headColor);
    void drawFood();

    TFT_eSPI* tft_ = nullptr;
    bool exit_ = false;
    State state_ = State::Ready;
    uint32_t nextStepAt_ = 0;
    uint32_t stepMs_ = 0;
    uint32_t deathAt_ = 0;

    uint16_t body_[CELLS];
    uint8_t occ_[(CELLS + 7) / 8];
    int head_ = 0;
    int len_ = 0;
    int grow_ = 0;
    Dir dir_ = Dir::Right;
    Dir queue_[QUEUE];
    int queued_ = 0;

    uint16_t food_ = 0;
    bool won_ = false;
    int score_ = 0;
    int best_ = 0;
};
