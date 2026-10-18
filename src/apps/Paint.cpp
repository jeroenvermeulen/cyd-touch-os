#include "apps/Paint.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "Config.h"

namespace {
// 16-colour palette (RGB565). Index 0 is the paper colour, so it doubles as the eraser.
const uint16_t PALETTE[16] = {
    0xFFFF,  // white
    0x0000,  // black
    0x8410,  // grey
    0xC618,  // light grey
    0xF800,  // red
    0xFD20,  // orange
    0xFFE0,  // yellow
    0x9A60,  // brown
    0x07E0,  // green
    0x03E0,  // dark green
    0x07FF,  // cyan
    0x001F,  // blue
    0x000F,  // navy
    0x780F,  // purple
    0xF81F,  // magenta
    0xFE19,  // pink
};
constexpr uint8_t PAPER = 0;

constexpr uint8_t BRUSH_RADIUS[4] = {1, 2, 4, 7};

// Menu icon: the Windows 3.1 Paintbrush palette, one char per pixel, drawn at 2x.
// '.' is left as the white icon background.
constexpr int16_t ICON_COLS = 32;
constexpr int16_t ICON_ROWS = 29;
constexpr int16_t ICON_SCALE = 2;
const char* const ICON[ICON_ROWS] = {
    ".......kkkkkkkk.................",
    ".....kkYYYYYYYYkk...............",
    "....kYYYYYYYYYYYok...........s..",
    "...kYYYYYYYYYYYYYok..........ks.",
    "..kYYYYYYMMMMMYYYYok.........kk.",
    ".kYYYYYYYMMMMMMYYYYk........dsk.",
    ".kYYYYYYYYMMMMMMYYYok......dsdk.",
    ".kYYYYYYYYYYMMMYYYYok......kdkk.",
    ".kYYYYYYYYYYYYYYYYYok.....kkkk..",
    "kYYYYGGGYYYYYYYYYYYYok...k.k....",
    "kYYYGGGGGGYYYYYYYYYYYok.k.k.....",
    "kYYYGGGGGGGYYYYYYYYYYYok.k......",
    "kYYYGGGGGGGYYYYYYYYooYkBkk......",
    "kYYYGGGGGGYYYYYYYYokkkBkYYk.....",
    "kYYYGGGGGYYYYYYYYok.kBkkYYok....",
    "kYYYYYYYYYYYYYYYYokkBk.kYYYok...",
    "kYYYYYYYRRRYYYYYYokBB..kYYYok...",
    "kYYYYYYRRRRRYYYYYYok..kYYYYYok..",
    ".kYYYYRRRRRRYYYYYYYkkkYYYYYYok..",
    ".kYYYYRRRRRRRYYYYYYYYYYYYYYYok..",
    ".kYYYYYRRRRRRYYYYYYYYYYYYYYYok..",
    "..kYYYYYRRRYYYYYYBBYYYYYYYYYok..",
    "..kYYYYYYYYYYYYBBBBBBYYYYYYYok..",
    "...kYYYYYYYYYYBBBBBBBBYYYYYook..",
    "....kYYYYYYYYYBBBBBBBYYYYYYok...",
    ".....kYYYYYYYYYBBBBYYYYYYYok....",
    ".......kkoYYYYYYYYYYYYoookk.....",
    ".......kkkkkooooooooookkk.......",
    "......kk....kkkkkkkkkk..........",
};

uint16_t iconColor(char c) {
    switch (c) {
        case 'Y': return 0xFF6C;  // soft yellow
        case 's': return 0xC618;  // silver
        case 'o': return 0x8400;  // olive
        case 'd': return 0x8410;  // grey
        case 'M': return 0xF81F;  // magenta
        case 'R': return 0xF800;  // red
        case 'G': return 0x07E0;  // green
        case 'B': return 0x001F;  // blue
        default: return 0x0000;   // 'k' outline
    }
}

// A finger can't really move this far between two polls, so a bigger jump is
// treated as noise from the resistive panel and not drawn.
constexpr int16_t MAX_JUMP = 50;

// Toolbar on the right of the canvas: 2x8 colour swatches, then three buttons.
constexpr int16_t TOOL_X = 272;
constexpr int16_t SWATCH_W = 24;
constexpr int16_t SWATCH_H = 20;
const Button BTN_SIZE{TOOL_X + 2, 162, 44, 24, ""};
const Button BTN_FILE{TOOL_X + 2, 188, 44, 24, "File"};
const Button BTN_EXIT{TOOL_X + 2, 214, 44, 24, "Exit"};

// Files screen: 3x2 slot tiles with thumbnails, buttons along the bottom.
constexpr int16_t SLOT_W = 96;
constexpr int16_t SLOT_H = 70;
constexpr int16_t SLOT_GAP = 8;
constexpr int16_t SLOT_TOP = 44;
constexpr int16_t SLOT_LEFT = (cfg::SCREEN_W - 3 * SLOT_W - 2 * SLOT_GAP) / 2;
constexpr int THUMB_SCALE = 4;
constexpr int16_t THUMB_W = 272 / THUMB_SCALE;
constexpr int16_t THUMB_H = 240 / THUMB_SCALE;

constexpr int16_t FBTN_Y = 202;
constexpr int16_t FBTN_W = 58;
constexpr int16_t FBTN_H = 34;
constexpr int16_t fbtnX(int i) { return 7 + i * (FBTN_W + 4); }
const Button BTN_BACK{fbtnX(4), FBTN_Y, FBTN_W, FBTN_H, "Back"};

// Confirm dialog
constexpr int16_t DLG_X = 40, DLG_Y = 60, DLG_W = 240, DLG_H = 120;
const Button BTN_YES{60, 125, 90, 36, "Yes"};
const Button BTN_NO{170, 125, 90, 36, "No"};

// File format: 4-byte magic, width, height, then the raw 4-bit pixels
// (two pixels per byte, the left one in the high nibble).
constexpr char MAGIC[4] = {'P', 'N', 'T', '1'};
constexpr size_t HEADER_SIZE = 8;
constexpr size_t ROW_BYTES = 272 / 2;
constexpr size_t IMAGE_BYTES = ROW_BYTES * 240;

struct Header {
    char magic[4];
    uint16_t w;
    uint16_t h;
};
static_assert(sizeof(Header) == HEADER_SIZE, "unexpected header padding");

bool readHeader(fs::File& f) {
    Header hdr;
    return f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr) &&
           memcmp(hdr.magic, MAGIC, sizeof(MAGIC)) == 0 && hdr.w == 272 && hdr.h == 240;
}

void slotRect(int slot, int16_t& x, int16_t& y) {
    x = SLOT_LEFT + (slot % 3) * (SLOT_W + SLOT_GAP);
    y = SLOT_TOP + (slot / 3) * (SLOT_H + SLOT_GAP);
}

void swatchRect(int i, int16_t& x, int16_t& y) {
    x = TOOL_X + (i % 2) * SWATCH_W;
    y = (i / 2) * SWATCH_H;
}
}  // namespace

// ---------------------------------------------------------------------------
// App interface

void Paint::drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) {
    const int16_t ox = x + (w - ICON_COLS * ICON_SCALE) / 2;
    const int16_t oy = y + (h - ICON_ROWS * ICON_SCALE) / 2;
    for (int16_t row = 0; row < ICON_ROWS; ++row) {
        for (int16_t col = 0; col < ICON_COLS; ++col) {
            const char c = ICON[row][col];
            if (c == '.') continue;
            tft.fillRect(ox + col * ICON_SCALE, oy + row * ICON_SCALE, ICON_SCALE, ICON_SCALE,
                         iconColor(c));
        }
    }
}

void Paint::begin(TFT_eSPI& tft) {
    tft_ = &tft;
    exit_ = false;

    if (!fsReady_) {
        fsReady_ = LittleFS.begin(true);  // formats the partition on first use
        if (!fsReady_) Serial.println("Paint: LittleFS mount failed");
    }

    // The picture is kept between visits, so only allocate (and clear) it once.
    if (!canvas_) {
        canvas_ = new TFT_eSprite(&tft);
        canvas_->setColorDepth(4);
        if (!canvas_->createSprite(CANVAS_W, CANVAS_H)) {
            Serial.println("Paint: canvas alloc failed");
            delete canvas_;
            canvas_ = nullptr;
            exit_ = true;
            return;
        }
        canvas_->createPalette(PALETTE);
        clearCanvas();
    }

    showCanvas();
}

void Paint::update(const TouchEvent& ev) {
    switch (screen_) {
        case Screen::Canvas: updateCanvas(ev); break;
        case Screen::Files: updateFiles(ev); break;
        case Screen::Confirm: updateConfirm(ev); break;
    }
}

// ---------------------------------------------------------------------------
// Canvas screen

void Paint::showCanvas() {
    screen_ = Screen::Canvas;
    stroking_ = false;
    canvas_->pushSprite(0, 0);
    drawToolbar();
}

void Paint::drawToolbar() {
    tft_->fillRect(TOOL_X, 0, cfg::SCREEN_W - TOOL_X, cfg::SCREEN_H, color::FACE);

    // Each swatch is a sunken well; the selected one gets a thick black frame.
    for (int i = 0; i < NUM_COLORS; i++) {
        int16_t x, y;
        swatchRect(i, x, y);
        if (i == color_) {
            tft_->drawRect(x, y, SWATCH_W, SWATCH_H, color::FACE_TEXT);
            tft_->drawRect(x + 1, y + 1, SWATCH_W - 2, SWATCH_H - 2, color::FACE_TEXT);
        } else {
            ui::drawBevel(*tft_, x, y, SWATCH_W, SWATCH_H, ui::Bevel::Sunken);
        }
        tft_->fillRect(x + 2, y + 2, SWATCH_W - 4, SWATCH_H - 4, PALETTE[i]);
    }

    // The size button shows the current brush in the current colour, outlined
    // so that white and grey still show up on the button face.
    BTN_SIZE.draw(*tft_);
    const int16_t cx = BTN_SIZE.x + BTN_SIZE.w / 2;
    const int16_t cy = BTN_SIZE.y + BTN_SIZE.h / 2;
    tft_->fillCircle(cx, cy, BRUSH_RADIUS[size_] + 1, color::FACE_TEXT);
    tft_->fillCircle(cx, cy, BRUSH_RADIUS[size_], PALETTE[color_]);

    BTN_FILE.draw(*tft_);
    BTN_EXIT.draw(*tft_);
}

void Paint::updateCanvas(const TouchEvent& ev) {
    if (!ev.touching) return;

    if (ev.x < CANVAS_W) {
        // A new touch starts a stroke; later points of the same touch extend it.
        if (ev.tapped || !stroking_) {
            dab(ev.x, ev.y);
            stroking_ = true;
        } else if (abs(ev.x - lastX_) <= MAX_JUMP && abs(ev.y - lastY_) <= MAX_JUMP) {
            stroke(lastX_, lastY_, ev.x, ev.y);
        }
        lastX_ = ev.x;
        lastY_ = ev.y;
        return;
    }

    // Sliding off the canvas onto the toolbar ends the stroke.
    stroking_ = false;
    if (!ev.tapped) return;

    for (int i = 0; i < NUM_COLORS; i++) {
        int16_t x, y;
        swatchRect(i, x, y);
        if (ev.x >= x && ev.x < x + SWATCH_W && ev.y >= y && ev.y < y + SWATCH_H) {
            color_ = i;
            drawToolbar();
            return;
        }
    }
    if (BTN_SIZE.contains(ev.x, ev.y)) {
        size_ = (size_ + 1) % NUM_SIZES;
        drawToolbar();
    } else if (BTN_FILE.contains(ev.x, ev.y)) {
        showFiles();
    } else if (BTN_EXIT.contains(ev.x, ev.y)) {
        exit_ = true;
    }
}

// Paint a round brush mark on both the screen and the backing canvas.
void Paint::dab(int16_t x, int16_t y) {
    const int16_t r = BRUSH_RADIUS[size_];
    canvas_->fillCircle(x, y, r, color_);
    tft_->setViewport(0, 0, CANVAS_W, CANVAS_H);  // keep big brushes off the toolbar
    tft_->fillCircle(x, y, r, PALETTE[color_]);
    tft_->resetViewport();
}

void Paint::stroke(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    const int16_t r = BRUSH_RADIUS[size_];
    const int dist = max(abs(x1 - x0), abs(y1 - y0));
    const int stepLen = max(1, r / 2);
    for (int s = stepLen; s < dist; s += stepLen) {
        dab(x0 + (x1 - x0) * s / dist, y0 + (y1 - y0) * s / dist);
    }
    dab(x1, y1);
}

void Paint::clearCanvas() { canvas_->fillSprite(PAPER); }

// ---------------------------------------------------------------------------
// Files screen

void Paint::showFiles(const char* status) {
    screen_ = Screen::Files;
    pending_ = Pending::None;
    ui::drawHeader(*tft_, "Pictures");

    for (int i = 0; i < NUM_SLOTS; i++) used_[i] = slotUsed(i);
    for (int i = 0; i < NUM_SLOTS; i++) drawSlot(i);
    drawFileButtons();

    if (!fsReady_) drawStatus("No storage!");
    else if (status) drawStatus(status);
}

void Paint::drawSlot(int slot) {
    int16_t x, y;
    slotRect(slot, x, y);
    const bool sel = slot == selected_;
    // The selected slot is pushed in and its number highlighted, like a
    // selected file in a 90s file dialog.
    ui::drawPanel(*tft_, x, y, SLOT_W, SLOT_H, sel ? ui::Bevel::Sunken : ui::Bevel::Raised);

    constexpr int16_t NUM_W = 16;
    const int16_t nx = x + 4, ny = y + SLOT_H / 2 - 9;
    tft_->fillRect(nx, ny, NUM_W, 18, sel ? color::TITLE : color::FACE);
    tft_->setTextDatum(MC_DATUM);
    tft_->setTextColor(sel ? color::TITLE_TEXT : color::FACE_TEXT);
    tft_->drawNumber(slot + 1, nx + NUM_W / 2, y + SLOT_H / 2, 2);

    const int16_t tx = x + SLOT_W - THUMB_W - 6;
    const int16_t ty = y + (SLOT_H - THUMB_H) / 2;
    ui::drawBevel(*tft_, tx - ui::BEVEL, ty - ui::BEVEL, THUMB_W + 2 * ui::BEVEL, THUMB_H + 2 * ui::BEVEL,
                  ui::Bevel::Sunken);
    if (!used_[slot]) {
        tft_->fillRect(tx, ty, THUMB_W, THUMB_H, color::EDGE_LIGHT);
        tft_->setTextColor(color::EDGE_SHADOW, color::EDGE_LIGHT);
        tft_->drawString("empty", tx + THUMB_W / 2, ty + THUMB_H / 2, 2);
        return;
    }

    // Thumbnail: every 4th pixel of every 4th row, read straight from flash.
    char path[24];
    slotPath(slot, path, sizeof(path));
    fs::File f = LittleFS.open(path, "r");
    if (!f || !readHeader(f)) return;

    uint8_t row[ROW_BYTES];
    uint16_t line[THUMB_W];
    const bool swap = tft_->getSwapBytes();
    tft_->setSwapBytes(true);
    for (int r = 0; r < THUMB_H; r++) {
        f.seek(HEADER_SIZE + r * THUMB_SCALE * ROW_BYTES);
        if (f.read(row, ROW_BYTES) != ROW_BYTES) break;
        // Sampled x is always even, so the pixel sits in the high nibble.
        for (int c = 0; c < THUMB_W; c++) line[c] = PALETTE[row[c * THUMB_SCALE / 2] >> 4];
        tft_->pushImage(tx, ty + r, THUMB_W, 1, line);
    }
    tft_->setSwapBytes(swap);
    f.close();
}

void Paint::drawFileButtons() {
    // Load and Delete are greyed out when the selected slot is empty.
    const bool used = used_[selected_];
    Button{fbtnX(0), FBTN_Y, FBTN_W, FBTN_H, "Save"}.draw(*tft_);
    Button{fbtnX(1), FBTN_Y, FBTN_W, FBTN_H, "Load"}.draw(*tft_, false, used);
    Button{fbtnX(2), FBTN_Y, FBTN_W, FBTN_H, "Delete"}.draw(*tft_, false, used);
    Button{fbtnX(3), FBTN_Y, FBTN_W, FBTN_H, "New"}.draw(*tft_);
    BTN_BACK.draw(*tft_);
}

void Paint::drawStatus(const char* msg) {
    // Shown on the right of the title bar.
    constexpr int16_t BAR_Y = ui::BEVEL + 1;
    tft_->fillRect(200, BAR_Y, cfg::SCREEN_W - 200 - BAR_Y, ui::TITLE_H, color::TITLE);
    tft_->setTextDatum(MR_DATUM);
    tft_->setTextColor(color::HIGHLIGHT, color::TITLE);
    tft_->drawString(msg, cfg::SCREEN_W - 8, BAR_Y + ui::TITLE_H / 2, 2);
}

void Paint::updateFiles(const TouchEvent& ev) {
    if (!ev.tapped) return;

    for (int i = 0; i < NUM_SLOTS; i++) {
        int16_t x, y;
        slotRect(i, x, y);
        if (ev.x >= x && ev.x < x + SLOT_W && ev.y >= y && ev.y < y + SLOT_H) {
            const int old = selected_;
            selected_ = i;
            drawSlot(old);
            drawSlot(i);
            drawFileButtons();
            return;
        }
    }

    if (BTN_BACK.contains(ev.x, ev.y)) {
        showCanvas();
        return;
    }
    if (ev.y < FBTN_Y || ev.y >= FBTN_Y + FBTN_H) return;

    char question[32];
    if (ev.x >= fbtnX(0) && ev.x < fbtnX(0) + FBTN_W) {  // Save
        if (used_[selected_]) {
            snprintf(question, sizeof(question), "Overwrite picture %d?", selected_ + 1);
            askConfirm(Pending::Save, question);
        } else {
            drawStatus("Saving...");
            const bool ok = saveSlot(selected_);
            showFiles(ok ? "Saved!" : "Save failed");
        }
    } else if (ev.x >= fbtnX(1) && ev.x < fbtnX(1) + FBTN_W) {  // Load
        if (!used_[selected_]) return;
        drawStatus("Loading...");
        if (loadSlot(selected_)) showCanvas();
        else showFiles("Load failed");
    } else if (ev.x >= fbtnX(2) && ev.x < fbtnX(2) + FBTN_W) {  // Delete
        if (!used_[selected_]) return;
        snprintf(question, sizeof(question), "Delete picture %d?", selected_ + 1);
        askConfirm(Pending::Delete, question);
    } else if (ev.x >= fbtnX(3) && ev.x < fbtnX(3) + FBTN_W) {  // New
        askConfirm(Pending::Clear, "Clear the canvas?");
    }
}

// ---------------------------------------------------------------------------
// Confirm dialog

void Paint::askConfirm(Pending action, const char* question) {
    screen_ = Screen::Confirm;
    pending_ = action;
    ui::drawWindow(*tft_, DLG_X, DLG_Y, DLG_W, DLG_H, "Paint");
    tft_->setTextDatum(MC_DATUM);
    tft_->drawString(question, cfg::SCREEN_W / 2, DLG_Y + 42, 2);
    BTN_YES.draw(*tft_);
    BTN_NO.draw(*tft_);
}

void Paint::updateConfirm(const TouchEvent& ev) {
    if (!ev.tapped) return;

    if (BTN_NO.contains(ev.x, ev.y)) {
        showFiles();
        return;
    }
    if (!BTN_YES.contains(ev.x, ev.y)) return;

    switch (pending_) {
        case Pending::Save: {
            drawStatus("Saving...");
            const bool ok = saveSlot(selected_);
            showFiles(ok ? "Saved!" : "Save failed");
            break;
        }
        case Pending::Delete: {
            char path[24];
            slotPath(selected_, path, sizeof(path));
            const bool ok = LittleFS.remove(path);
            showFiles(ok ? "Deleted" : "Delete failed");
            break;
        }
        case Pending::Clear:
            clearCanvas();
            showCanvas();
            break;
        case Pending::None:
            showFiles();
            break;
    }
}

// ---------------------------------------------------------------------------
// Storage

void Paint::slotPath(int slot, char* buf, size_t len) { snprintf(buf, len, "/paint%d.pnt", slot + 1); }

bool Paint::slotUsed(int slot) const {
    if (!fsReady_) return false;
    char path[24];
    slotPath(slot, path, sizeof(path));
    return LittleFS.exists(path);
}

bool Paint::saveSlot(int slot) {
    if (!fsReady_) return false;
    char path[24];
    slotPath(slot, path, sizeof(path));
    fs::File f = LittleFS.open(path, "w");
    if (!f) return false;

    Header hdr;
    memcpy(hdr.magic, MAGIC, sizeof(MAGIC));
    hdr.w = CANVAS_W;
    hdr.h = CANVAS_H;
    const bool ok =
        f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr) &&
        f.write(static_cast<const uint8_t*>(canvas_->getPointer()), IMAGE_BYTES) == IMAGE_BYTES;
    f.close();
    if (!ok) LittleFS.remove(path);  // don't leave a half-written picture behind
    return ok;
}

bool Paint::loadSlot(int slot) {
    if (!fsReady_) return false;
    char path[24];
    slotPath(slot, path, sizeof(path));
    fs::File f = LittleFS.open(path, "r");
    if (!f || !readHeader(f)) return false;

    // Read into a temporary buffer so a broken file can't wipe the current picture.
    uint8_t* buf = static_cast<uint8_t*>(malloc(IMAGE_BYTES));
    if (!buf) return false;
    const bool ok = f.read(buf, IMAGE_BYTES) == IMAGE_BYTES;
    f.close();
    if (ok) memcpy(canvas_->getPointer(), buf, IMAGE_BYTES);
    free(buf);
    return ok;
}
