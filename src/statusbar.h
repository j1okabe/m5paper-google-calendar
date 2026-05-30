#pragma once

#include <M5GFX.h>

class StatusBar {
public:
    // draw statusbar
    static void draw(M5Canvas *canvas, int32_t width);
    static int height();
};
