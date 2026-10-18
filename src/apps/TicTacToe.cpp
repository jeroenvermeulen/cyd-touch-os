#include "games/TicTacToe.h"

#include <Arduino.h>

#include "Config.h"

namespace {
// Board layout: 3x3 cells on the left, info panel on the right.
constexpr int16_t CELL = 70;
constexpr int16_t BOARD_X = 10;
constexpr int16_t BOARD_Y = 15;
constexpr int16_t BOARD_SIZE = CELL * 3;
constexpr int16_t PANEL_X = 230;
constexpr int16_t PANEL_W = 82;
constexpr int16_t PANEL_CX = PANEL_X + PANEL_W / 2;
constexpr int16_t MARK_WELL = 40;  // dark square behind the turn/winner mark
constexpr int16_t MARK_CY = 32;

constexpr uint32_t CPU_DELAY_MS = 450;  // so the CPU's move doesn't feel instant

constexpr int8_t LINES[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8},  // rows
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8},  // columns
    {0, 4, 8}, {2, 4, 6},             // diagonals
};

const Button BTN_EASY{40, 55, 240, 38, "1 Player - Easy"};
const Button BTN_HARD{40, 100, 240, 38, "1 Player - Hard"};
const Button BTN_TWO{40, 145, 240, 38, "2 Players"};
const Button BTN_MENU{40, 192, 240, 38, "< Main Menu"};

const Button BTN_NEW{PANEL_X, 158, PANEL_W, 34, "New"};
const Button BTN_BACK{PANEL_X, 200, PANEL_W, 34, "Back"};

int16_t cellCenterX(int cell) { return BOARD_X + (cell % 3) * CELL + CELL / 2; }
int16_t cellCenterY(int cell) { return BOARD_Y + (cell / 3) * CELL + CELL / 2; }
}  // namespace

// ---------------------------------------------------------------------------
// Game interface

void TicTacToe::drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) {
    const int16_t s = min(w, h) - 4;  // icon size
    const int16_t c = s / 3;
    const int16_t ox = x + (w - s) / 2;
    const int16_t oy = y + (h - s) / 2;
    for (int i = 1; i < 3; i++) {
        tft.drawFastVLine(ox + i * c, oy, s, color::EDGE_SHADOW);
        tft.drawFastHLine(ox, oy + i * c, s, color::EDGE_SHADOW);
    }
    tft_ = &tft;
    drawMark('X', ox + c / 2, oy + c / 2, c / 3, color::ICON_BG);
    drawMark('O', ox + c + c / 2, oy + c + c / 2, c / 3, color::ICON_BG);
    drawMark('X', ox + 2 * c + c / 2, oy + c / 2, c / 3, color::ICON_BG);
}

void TicTacToe::begin(TFT_eSPI& tft) {
    tft_ = &tft;
    exit_ = false;
    showModeSelect();
}

void TicTacToe::update(const TouchEvent& ev) {
    if (screen_ == Screen::ModeSelect) {
        updateModeSelect(ev);
    } else {
        updatePlaying(ev);
    }
}

// ---------------------------------------------------------------------------
// Mode select

void TicTacToe::showModeSelect() {
    screen_ = Screen::ModeSelect;
    ui::drawHeader(*tft_, "Tic-Tac-Toe");
    BTN_EASY.draw(*tft_);
    BTN_HARD.draw(*tft_);
    BTN_TWO.draw(*tft_);
    BTN_MENU.draw(*tft_);
}

void TicTacToe::updateModeSelect(const TouchEvent& ev) {
    if (!ev.tapped) return;
    if (BTN_EASY.contains(ev.x, ev.y)) startMatch(Mode::CpuEasy);
    else if (BTN_HARD.contains(ev.x, ev.y)) startMatch(Mode::CpuHard);
    else if (BTN_TWO.contains(ev.x, ev.y)) startMatch(Mode::TwoPlayer);
    else if (BTN_MENU.contains(ev.x, ev.y)) exit_ = true;
}

// ---------------------------------------------------------------------------
// Playing

void TicTacToe::startMatch(Mode mode) {
    mode_ = mode;
    screen_ = Screen::Playing;
    scoreX_ = scoreO_ = draws_ = 0;
    starter_ = 'O';  // newRound() flips this, so X starts the first round
    randomSeed(esp_random());
    newRound();
}

void TicTacToe::newRound() {
    memset(board_, EMPTY, sizeof(board_));
    starter_ = (starter_ == 'X') ? 'O' : 'X';  // alternate who goes first
    turn_ = starter_;
    over_ = false;
    winLine_ = -1;
    if (isCpuTurn()) cpuMoveAt_ = millis() + CPU_DELAY_MS;

    tft_->fillScreen(color::BG);
    ui::drawPanel(*tft_, PANEL_X - 6, 0, cfg::SCREEN_W - PANEL_X + 6, cfg::SCREEN_H);
    drawBoard();
    drawStatus();
    drawScores();
    BTN_NEW.draw(*tft_);
    BTN_BACK.draw(*tft_);
}

void TicTacToe::updatePlaying(const TouchEvent& ev) {
    if (ev.tapped) {
        if (BTN_BACK.contains(ev.x, ev.y)) {
            showModeSelect();
            return;
        }
        if (BTN_NEW.contains(ev.x, ev.y)) {
            newRound();
            return;
        }
        const bool onBoard = ev.x >= BOARD_X && ev.x < BOARD_X + BOARD_SIZE && ev.y >= BOARD_Y &&
                             ev.y < BOARD_Y + BOARD_SIZE;
        if (onBoard) {
            if (over_) {
                newRound();  // tap the board after a game ends to play again
                return;
            }
            const int cell = ((ev.y - BOARD_Y) / CELL) * 3 + (ev.x - BOARD_X) / CELL;
            if (!isCpuTurn() && board_[cell] == EMPTY) place(cell);
        }
    }

    if (!over_ && isCpuTurn() && (int32_t)(millis() - cpuMoveAt_) >= 0) {
        place(chooseCpuMove());
    }
}

void TicTacToe::place(int cell) {
    board_[cell] = turn_;
    drawMark(turn_, cellCenterX(cell), cellCenterY(cell), 24);

    winLine_ = findWinLine(board_);
    if (winLine_ >= 0) {
        over_ = true;
        (turn_ == 'X' ? scoreX_ : scoreO_)++;
        drawWinLine();
        drawScores();
    } else if (isFull(board_)) {
        over_ = true;
        draws_++;
        drawScores();
    } else {
        turn_ = (turn_ == 'X') ? 'O' : 'X';
        if (isCpuTurn()) cpuMoveAt_ = millis() + CPU_DELAY_MS;
    }
    drawStatus();
}

int TicTacToe::chooseCpuMove() {
    int freeCells[9];
    int freeCount = 0;
    for (int i = 0; i < 9; i++) {
        if (board_[i] == EMPTY) freeCells[freeCount++] = i;
    }

    // Easy: mostly random moves, so it can be beaten.
    if (mode_ == Mode::CpuEasy && random(100) < 65) return freeCells[random(freeCount)];

    // Hard: perfect play via minimax. Random choice among equally good moves keeps it varied.
    int bestScore = -1000;
    int best[9];
    int bestCount = 0;
    for (int i = 0; i < freeCount; i++) {
        const int cell = freeCells[i];
        board_[cell] = CPU;
        const int score = minimax(board_, HUMAN, 1, -1000, 1000);
        board_[cell] = EMPTY;
        if (score > bestScore) {
            bestScore = score;
            bestCount = 0;
        }
        if (score == bestScore) best[bestCount++] = cell;
    }
    return best[random(bestCount)];
}

// ---------------------------------------------------------------------------
// Rules

int TicTacToe::findWinLine(const char* b) {
    for (int i = 0; i < 8; i++) {
        const char a = b[LINES[i][0]];
        if (a != EMPTY && a == b[LINES[i][1]] && a == b[LINES[i][2]]) return i;
    }
    return -1;
}

bool TicTacToe::isFull(const char* b) {
    for (int i = 0; i < 9; i++) {
        if (b[i] == EMPTY) return false;
    }
    return true;
}

// Scores from the CPU's point of view; quicker wins and slower losses score better.
int TicTacToe::minimax(char* b, char player, int depth, int alpha, int beta) {
    const int line = findWinLine(b);
    if (line >= 0) return b[LINES[line][0]] == CPU ? 10 - depth : depth - 10;
    if (isFull(b)) return 0;

    const bool maximizing = (player == CPU);
    int best = maximizing ? -1000 : 1000;
    for (int i = 0; i < 9; i++) {
        if (b[i] != EMPTY) continue;
        b[i] = player;
        const int score = minimax(b, maximizing ? HUMAN : CPU, depth + 1, alpha, beta);
        b[i] = EMPTY;
        if (maximizing) {
            best = max(best, score);
            alpha = max(alpha, score);
        } else {
            best = min(best, score);
            beta = min(beta, score);
        }
        if (beta <= alpha) break;
    }
    return best;
}

// ---------------------------------------------------------------------------
// Drawing

void TicTacToe::drawBoard() {
    for (int i = 1; i < 3; i++) {
        tft_->fillRoundRect(BOARD_X + i * CELL - 2, BOARD_Y + 4, 4, BOARD_SIZE - 8, 2, color::BORDER);
        tft_->fillRoundRect(BOARD_X + 4, BOARD_Y + i * CELL - 2, BOARD_SIZE - 8, 4, 2, color::BORDER);
    }
}

// bg is what's behind the mark: the smooth edges blend into it.
void TicTacToe::drawMark(char mark, int16_t cx, int16_t cy, int16_t r, uint16_t bg) {
    const float thickness = max(2, r / 3);
    if (mark == 'X') {
        tft_->drawWideLine(cx - r, cy - r, cx + r, cy + r, thickness, color::PLAYER_X, bg);
        tft_->drawWideLine(cx - r, cy + r, cx + r, cy - r, thickness, color::PLAYER_X, bg);
    } else {
        tft_->fillSmoothCircle(cx, cy, r, color::PLAYER_O, bg);
        tft_->fillSmoothCircle(cx, cy, r - thickness, bg, color::PLAYER_O);
    }
}

void TicTacToe::drawWinLine() {
    const int a = LINES[winLine_][0];
    const int c = LINES[winLine_][2];
    tft_->drawWideLine(cellCenterX(a), cellCenterY(a), cellCenterX(c), cellCenterY(c), 7,
                       color::HIGHLIGHT, color::BG);
}

void TicTacToe::drawStatus() {
    tft_->fillRect(PANEL_X, 6, PANEL_W, 89, color::FACE);
    tft_->setTextDatum(TC_DATUM);

    const char* text;
    uint16_t textColor = color::FACE_TEXT;
    char mark = 0;
    if (!over_) {
        mark = turn_;
        if (mode_ == Mode::TwoPlayer) text = "to move";
        else text = isCpuTurn() ? "thinking..." : "Your turn";
    } else if (winLine_ >= 0) {
        mark = board_[LINES[winLine_][0]];
        textColor = color::TITLE;
        if (mode_ == Mode::TwoPlayer) text = "wins!";
        else text = (mark == HUMAN) ? "You win!" : "CPU wins";
    } else {
        textColor = color::TITLE;
        tft_->setTextColor(textColor, color::FACE);
        tft_->drawString("Draw", PANEL_CX, 20, 4);
        text = "";
    }
    if (mark) {
        ui::drawPanel(*tft_, PANEL_CX - MARK_WELL / 2, MARK_CY - MARK_WELL / 2, MARK_WELL, MARK_WELL,
                      ui::Bevel::Sunken, color::BG);
        drawMark(mark, PANEL_CX, MARK_CY, 13);
    }
    tft_->setTextColor(textColor, color::FACE);
    tft_->drawString(text, PANEL_CX, 58, 2);
    if (over_) {
        tft_->setTextColor(color::EDGE_SHADOW, color::FACE);
        tft_->drawString("tap board", PANEL_CX, 78, 1);
    }
}

void TicTacToe::drawScores() {
    const bool cpu = mode_ != Mode::TwoPlayer;
    char buf[3][16];
    snprintf(buf[0], sizeof(buf[0]), "%s %d", cpu ? "You" : "X", scoreX_);
    snprintf(buf[1], sizeof(buf[1]), "%s %d", cpu ? "CPU" : "O", scoreO_);
    snprintf(buf[2], sizeof(buf[2]), "Draw %d", draws_);
    const uint16_t colors[3] = {color::PLAYER_X, color::PLAYER_O, color::TEXT_DIM};

    // A dark well keeps the player colours readable on the grey panel.
    ui::drawPanel(*tft_, PANEL_X, 98, PANEL_W, 58, ui::Bevel::Sunken, color::BG);
    tft_->setTextDatum(TL_DATUM);
    for (int i = 0; i < 3; i++) {
        tft_->setTextColor(colors[i], color::BG);
        tft_->drawString(buf[i], PANEL_X + 8, 101 + i * 18, 2);
    }
}
