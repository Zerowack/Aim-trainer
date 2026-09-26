// main.cpp - entry point.
//
// RawAim: a Valorant-calibrated aim trainer.
//   * Raw Input mouse (no Windows acceleration), every packet applied in order
//   * 0.07 degrees per count x sensitivity, 103 degree Hor+ FOV
//   * Six training modes, a PSA sensitivity finder and progress tracking
#include "app.h"

int main() {
    App app;
    return app.Run();
}
