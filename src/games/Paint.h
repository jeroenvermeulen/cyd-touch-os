#pragma once

#include "Game.h"
#include "UI.h"

// Finger painting with 16 colours and 4 brush sizes. Pictures are saved to
// flash (LittleFS) in one of 6 slots.
class Paint : public Game {
public:
    const char* name() const override { return "Paint"; }
    void drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) override;
    void begin(TFT_eSPI& tft) override;
    void update(const TouchEvent& ev) override;
    bool finished() const override { return exit_; }

private:
    enum class Screen { Canvas, Files, Confirm };
    enum class Pending { None, Save, Delete, Clear };  // action waiting for confirmation

    static constexpr int16_t CANVAS_W = 272;
    static constexpr int16_t CANVAS_H = 240;
    static constexpr int NUM_COLORS = 16;
    static constexpr int NUM_SIZES = 4;
    static constexpr int NUM_SLOTS = 6;

    // Canvas screen
    void showCanvas();
    void updateCanvas(const TouchEvent& ev);
    void drawToolbar();
    void stroke(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
    void dab(int16_t x, int16_t y);

    // Files screen
    void showFiles(const char* status = nullptr);
    void updateFiles(const TouchEvent& ev);
    void drawSlot(int slot);
    void drawFileButtons();
    void drawStatus(const char* msg);

    // Confirm dialog
    void askConfirm(Pending action, const char* question);
    void updateConfirm(const TouchEvent& ev);

    // Storage
    static void slotPath(int slot, char* buf, size_t len);
    bool slotUsed(int slot) const;
    bool saveSlot(int slot);
    bool loadSlot(int slot);
    void clearCanvas();

    TFT_eSPI* tft_ = nullptr;
    TFT_eSprite* canvas_ = nullptr;  // 4-bit backing store of the picture
    bool fsReady_ = false;
    bool exit_ = false;
    Screen screen_ = Screen::Canvas;
    Pending pending_ = Pending::None;

    uint8_t color_ = 1;  // palette index (black)
    uint8_t size_ = 1;   // index into the brush sizes
    int16_t lastX_ = 0, lastY_ = 0;
    bool stroking_ = false;

    int selected_ = 0;            // selected slot on the files screen
    bool used_[NUM_SLOTS] = {};  // which slots hold a picture
};
