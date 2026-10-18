#pragma once

#include "App.h"
#include "UI.h"

class TicTacToe : public App {
public:
    const char* name() const override { return "Tic-Tac-Toe"; }
    void drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) override;
    void begin(TFT_eSPI& tft) override;
    void update(const TouchEvent& ev) override;
    bool finished() const override { return exit_; }

private:
    enum class Screen { ModeSelect, Playing };
    enum class Mode { CpuEasy, CpuHard, TwoPlayer };

    static constexpr char EMPTY = ' ';
    static constexpr char HUMAN = 'X';  // in CPU modes the player is always X
    static constexpr char CPU = 'O';

    // Mode select screen
    void showModeSelect();
    void updateModeSelect(const TouchEvent& ev);

    // Game screen
    void startMatch(Mode mode);
    void newRound();
    void updatePlaying(const TouchEvent& ev);
    void place(int cell);
    bool isCpuTurn() const { return mode_ != Mode::TwoPlayer && turn_ == CPU; }
    int chooseCpuMove();

    // Drawing
    void drawBoard();
    void drawMark(char mark, int16_t cx, int16_t cy, int16_t r, uint16_t bg = color::BG);
    void drawStatus();
    void drawScores();
    void drawWinLine();

    // Rules
    static int findWinLine(const char* b);  // index into LINES, or -1
    static bool isFull(const char* b);
    static int minimax(char* b, char player, int depth, int alpha, int beta);

    TFT_eSPI* tft_ = nullptr;
    Screen screen_ = Screen::ModeSelect;
    Mode mode_ = Mode::TwoPlayer;
    bool exit_ = false;

    char board_[9];
    char turn_ = 'X';
    char starter_ = 'X';
    bool over_ = false;
    int winLine_ = -1;
    uint32_t cpuMoveAt_ = 0;

    int scoreX_ = 0;
    int scoreO_ = 0;
    int draws_ = 0;
};
