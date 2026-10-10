// Minimal Teensy 4.1 program: pulls the whole portable core into the link so that the compiler and the linker
// report portability problems and the real code / RAM sizes. It does not drive any hardware yet (see backlog B-100+).
#include <Arduino.h>

#include "../../core/engine.h"
#include "../../core/ui/canvas.h"
#include "../../core/ui/ui_controller.h"

// Big objects are static: the engine and the UI do not fit on the stack.
static Engine       g_engine;
static UiController g_ui(g_engine);
static float        g_left[cfg::BLOCK];
static float        g_right[cfg::BLOCK];

void setup() {
    Serial.begin(115200);
}

void loop() {
    g_ui.update(g_engine.state(), millis());
    g_engine.render(g_left, g_right, cfg::BLOCK);

    static Canvas canvas;
    g_ui.render(canvas);
}
