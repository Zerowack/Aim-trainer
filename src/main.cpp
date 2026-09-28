// main.cpp - entry point.
//
// Valtrainer: a Valorant-calibrated aim trainer.
//   * Raw Input mouse (no Windows acceleration), every packet applied in order
//   * 0.07 degrees per count x sensitivity, 103 degree Hor+ FOV
//   * 19 training modes, a performance-based sensitivity finder and progress tracking
#include "app.h"

int main() {
    App app;
    return app.Run();
}
