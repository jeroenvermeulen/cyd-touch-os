#include <Arduino.h>
#include <TFT_eSPI.h>

#include "Config.h"
#include "Input.h"
#include "Menu.h"
#include "Net.h"
#include "Settings.h"
#include "apps/Clock.h"
#include "apps/CrossyRoad.h"
#include "apps/Minesweeper.h"
#include "apps/PacMan.h"
#include "apps/Paint.h"
#include "apps/Screensaver.h"
#include "apps/Snake.h"
#include "apps/Tetris.h"
#include "apps/TicTacToe.h"
#include "apps/Weather.h"

TFT_eSPI tft;
Input input;

// --- App registry: add new apps here ---
TicTacToe ticTacToe;
CrossyRoad crossyRoad;
Paint paint;
Snake snake;
Tetris tetris;
PacMan pacMan;
Minesweeper minesweeper;
Clock clockApp;
Weather weather;
Settings settings;  // keep it last
App* const apps[] = {&ticTacToe, &crossyRoad, &paint, &snake, &tetris, &pacMan, &minesweeper, &clockApp, &weather, &settings};

Menu menu(apps, sizeof(apps) / sizeof(apps[0]));
Screensaver screensaver;  // not in the menu; starts when nothing is touched
App* activeApp = nullptr;
App* suspendedApp = nullptr;  // the game under the screensaver, if any
uint32_t screensaverAt = 0;  // millis() when the idle menu starts the screensaver

void setup() {
    Serial.begin(115200);

    // Keep the RGB LED off (it's active low).
    for (int pin : {cfg::LED_R, cfg::LED_G, cfg::LED_B}) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }

    tft.init();
    tft.setRotation(1);  // landscape, USB connector on the right
    input.begin();
    menu.draw(tft);
    screensaverAt = millis() + cfg::SCREENSAVER_MS;
    net::begin();  // connects in the background with the saved network
}

void loop() {
    const TouchEvent ev = input.poll();
    net::loop();
    if (ev.touching) screensaverAt = millis() + cfg::SCREENSAVER_MS;

    const bool idle = (int32_t)(millis() - screensaverAt) >= 0;

    if (activeApp == &screensaver) {
        screensaver.update(ev);
        if (screensaver.finished()) {
            activeApp = suspendedApp;
            suspendedApp = nullptr;
            if (activeApp) activeApp->restore();
            else menu.draw(tft);
            screensaverAt = millis() + cfg::SCREENSAVER_MS;
        }
    } else if (activeApp && idle && activeApp->suspend()) {
        suspendedApp = activeApp;
        activeApp = &screensaver;
        screensaver.begin(tft);
    } else if (activeApp) {
        activeApp->update(ev);
        if (activeApp->finished()) {
            activeApp = nullptr;
            menu.draw(tft);
            screensaverAt = millis() + cfg::SCREENSAVER_MS;
        }
    } else if (idle) {
        activeApp = &screensaver;
        screensaver.begin(tft);
    } else {
        const int selected = menu.update(ev);
        if (selected >= 0) {
            activeApp = apps[selected];
            activeApp->begin(tft);
        }
    }

    delay(2);
}
