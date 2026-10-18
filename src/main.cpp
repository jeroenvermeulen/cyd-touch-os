#include <Arduino.h>
#include <TFT_eSPI.h>

#include "Config.h"
#include "Input.h"
#include "Menu.h"
#include "games/CrossyRoad.h"
#include "games/Paint.h"
#include "games/Snake.h"
#include "games/TicTacToe.h"

TFT_eSPI tft;
Input input;

// --- Game registry: add new games here ---
TicTacToe ticTacToe;
CrossyRoad crossyRoad;
Paint paint;
Snake snake;
Game* const games[] = {&ticTacToe, &crossyRoad, &paint, &snake};

Menu menu(games, sizeof(games) / sizeof(games[0]));
Game* activeGame = nullptr;

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
}

void loop() {
    const TouchEvent ev = input.poll();

    if (activeGame) {
        activeGame->update(ev);
        if (activeGame->finished()) {
            activeGame = nullptr;
            menu.draw(tft);
        }
    } else {
        const int selected = menu.update(ev);
        if (selected >= 0) {
            activeGame = games[selected];
            activeGame->begin(tft);
        }
    }

    delay(2);
}
