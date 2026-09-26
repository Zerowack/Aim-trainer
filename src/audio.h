// audio.h - Sound effects synthesised in code (no audio files needed).
#pragma once

#include "raylib.h"

enum class Sfx : int { Hit = 0, Kill, Headshot, Miss, Tick, CountBeep, CountGo, UiClick, Count };

class Audio {
public:
    bool Init();
    void Shutdown();
    // Hit-type sounds (hits, kills, ticks) use hitVolume; the rest use 1.0.
    // Everything is multiplied by masterVolume.
    void SetVolumes(float master, float hit);
    void Play(Sfx s);

private:
    static constexpr int kVoices = 4;  // allows overlapping copies of a sound
    Sound sounds_[static_cast<int>(Sfx::Count)][kVoices] = {};
    int nextVoice_[static_cast<int>(Sfx::Count)] = {};
    bool ready_ = false;
    float master_ = 0.8f;
    float hit_ = 0.7f;
};
