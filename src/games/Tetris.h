#pragma once

#include "Game.h"
#include "UI.h"

class Tetris : public Game {
public:
    const char* name() const override { return "Tetris"; }
    void drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) override;
    void begin(TFT_eSPI& tft) override;
    void update(const TouchEvent& ev) override;
    bool finished() const override { return exit_; }

private:
    enum class State { Ready, Playing, Clearing, Dying, Paused, GameOver };
    enum class Hold : uint8_t { None, Left, Right, Down };

    static constexpr int COLS = 10;
    static constexpr int ROWS = 20;
    static constexpr int PIECES = 7;

    // Game flow
    void newGame();
    void playInput(const TouchEvent& ev, uint32_t now);
    void spawn();
    void lock();
    void finishClear();
    void topOut();
    void refillBag();
    uint8_t nextFromBag();

    // Piece movement; all return false when blocked.
    bool fits(int type, int rot, int x, int y) const;
    bool tryMove(int dx, int dy);
    bool tryRotate();
    bool softDrop();
    int ghostY() const;
    uint32_t gravityMs() const;

    void showPause();
    void showGameOver();

    // Rendering: shown_ mirrors what is on screen so render() only redraws
    // cells that changed.
    void drawScreen();
    void render();
    void drawCell(int col, int row, uint8_t v);
    void drawStats();
    void drawNext();
    void drawControls();
    void drawControl(const Button& b, bool pressed);
    static const Button* holdButton(Hold h);
    void setHold(Hold h);  // also redraws the buttons whose pressed state changed

    TFT_eSPI* tft_ = nullptr;
    bool exit_ = false;
    State state_ = State::Ready;

    uint8_t board_[ROWS][COLS];  // 0 = empty, else piece type + 1
    uint8_t shown_[ROWS][COLS];
    uint8_t bag_[PIECES];
    int bagPos_ = PIECES;

    bool active_ = false;  // a falling piece is on the board
    int type_ = 0, rot_ = 0, px_ = 0, py_ = 0;
    int next_ = 0;

    uint32_t fallAt_ = 0;
    bool resting_ = false;  // piece sits on something and locks at lockAt_
    uint32_t lockAt_ = 0;
    int lockResets_ = 0;
    uint32_t clearAt_ = 0;
    uint32_t deathAt_ = 0;
    uint32_t clearRows_ = 0;  // bitmask of full rows being cleared

    Hold hold_ = Hold::None;
    uint32_t repeatAt_ = 0;

    int score_ = 0;
    int lines_ = 0;
    int level_ = 0;
    int best_ = 0;
};
