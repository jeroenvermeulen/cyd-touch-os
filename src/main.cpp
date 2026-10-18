#include <Arduino.h>
#include <TFT_eSPI.h>

#include "Config.h"
#include "Input.h"
#include "Menu.h"
#include "Net.h"
#include "Settings.h"
#include "apps/Clock.h"
#include "apps/CrossyRoad.h"
#include "apps/PacMan.h"
#include "apps/Paint.h"
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
Clock clockApp;
Weather weather;
Settings settings;  // keep it last
App* const apps[] = {&ticTacToe, &crossyRoad, &paint, &snake, &tetris, &pacMan, &clockApp, &weather, &settings};

Menu menu(apps, sizeof(apps) / sizeof(apps[0]));
App* activeApp = nullptr;

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
    net::begin();  // connects in the background with the saved network
}

void loop() {
    const TouchEvent ev = input.poll();
    net::loop();

    if (activeApp) {
        activeApp->update(ev);
        if (activeApp->finished()) {
            activeApp = nullptr;
            menu.draw(tft);
        }
    } else {
        const int selected = menu.update(ev);
        if (selected >= 0) {
            activeApp = apps[selected];
            activeApp->begin(tft);
        }
    }

    delay(2);
}
