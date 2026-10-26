#include <emscripten/emscripten.h>

#include "Game.h"

static void mainLoop() {
    Game::get().frame(emscripten_get_now());
}

int main() {
    Game::get().boot("ffa");
    emscripten_set_main_loop(mainLoop, 0, 0);
    return 0;
}
