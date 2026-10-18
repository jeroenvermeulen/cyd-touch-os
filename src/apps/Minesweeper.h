#pragma once

#include "App.h"
#include "UI.h"

class Minesweeper : public App {
public:
    const char* name() const override { return "Minesweeper"; }
    void drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) override;
    void begin(TFT_eSPI& tft) override;
    void update(const TouchEvent& ev) override;
    bool finished() const override { return exit_; }
    bool suspend() override;
    void restore() override;

private:
    // Ready: no mines placed yet (the first dig places them, away from it).
    // Ended: the board stays visible for a moment before the Over dialog.
    enum class State { Ready, Playing, Ended, Paused, Over };
    enum class Cover : uint8_t { Hidden, Flagged, Open };
    enum class Face : uint8_t { Smile, Wow, Dead, Cool };

    static constexpr int COLS = 14;
    static constexpr int ROWS = 12;
    static constexpr int CELLS = COLS * ROWS;
    static constexpr int MINES = 25;

    // Game flow
    void newGame();
    void boardInput(const TouchEvent& ev, uint32_t now);
    void act(int i, bool dig);  // tap or long-press result on cell i
    void placeMines(int safe);
    void reveal(int i);
    void chord(int i);
    void toggleFlag(int i);
    void lose(int i);
    void win();
    int elapsedSecs() const;
    int cellAt(int16_t x, int16_t y) const;  // -1 when off the board

    void showPause();
    void drawPauseDialog();
    void showOver();

    // Rendering
    void drawScreen();
    void drawCell(int i, bool pressed = false);
    void drawCounter();
    void drawTimer();
    void drawFace(Face f);
    void drawModes();

    TFT_eSPI* tft_ = nullptr;
    bool exit_ = false;
    State state_ = State::Ready;
    State pausedFrom_ = State::Ready;

    bool mine_[CELLS];
    uint8_t adj_[CELLS];  // neighbouring mines
    Cover cover_[CELLS];
    int opened_ = 0;
    int flags_ = 0;
    bool won_ = false;
    int hit_ = -1;  // the mine that went off

    bool flagMode_ = false;
    int press_ = -1;  // cell under the finger
    uint32_t pressAt_ = 0;
    bool longDone_ = false;  // the long-press already acted on this touch

    uint32_t startMs_ = 0;
    uint32_t pausedAt_ = 0;
    uint32_t endAt_ = 0;
    int finalSecs_ = 0;
    int shownSecs_ = -1;
    int best_ = 0;  // best winning time in seconds, 0 = none yet
};
