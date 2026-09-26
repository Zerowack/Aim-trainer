// audio.cpp - builds short PCM waveforms at startup.
#include "audio.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int kRate = 44100;
constexpr double kTwoPi = 6.28318530717958647692;

// Parameters for one synthesised blip.
struct Tone {
    double f0;        // start frequency (Hz)
    double f1;        // end frequency (Hz), linear sweep
    double seconds;   // duration
    double decay;     // exponential decay rate (1/s)
    double noise;     // 0..1 amount of white noise
    double harmonic;  // 0..1 amount of the 2nd harmonic
    double gain;      // peak amplitude 0..1
};

std::vector<int16_t> Synth(const Tone* tones, int count) {
    double total = 0.0;
    for (int i = 0; i < count; ++i) total += tones[i].seconds;
    const size_t n = static_cast<size_t>(total * kRate) + 1;
    std::vector<double> buf(n, 0.0);

    uint32_t rng = 0x12345678u;
    size_t offset = 0;
    for (int i = 0; i < count; ++i) {
        const Tone& t = tones[i];
        const size_t len = static_cast<size_t>(t.seconds * kRate);
        double phase = 0.0;
        for (size_t s = 0; s < len && offset + s < n; ++s) {
            const double time = static_cast<double>(s) / kRate;
            const double frac = static_cast<double>(s) / static_cast<double>(len);
            const double freq = t.f0 + (t.f1 - t.f0) * frac;
            phase += kTwoPi * freq / kRate;
            rng = rng * 1664525u + 1013904223u;
            const double white = static_cast<double>(rng >> 8) / 8388608.0 - 1.0;
            // 2 ms attack avoids clicks, then exponential decay.
            const double attack = std::fmin(1.0, time / 0.002);
            const double env = attack * std::exp(-t.decay * time);
            const double v = std::sin(phase) * (1.0 - t.noise) + std::sin(2.0 * phase) * t.harmonic +
                             white * t.noise;
            buf[offset + s] += v * env * t.gain;
        }
        offset += len;
    }

    std::vector<int16_t> pcm(n);
    for (size_t i = 0; i < n; ++i) {
        double v = buf[i];
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        pcm[i] = static_cast<int16_t>(v * 32000.0);
    }
    return pcm;
}

Sound MakeSound(const Tone* tones, int count) {
    std::vector<int16_t> pcm = Synth(tones, count);
    Wave w = {};
    w.frameCount = static_cast<unsigned int>(pcm.size());
    w.sampleRate = kRate;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = pcm.data();
    return LoadSoundFromWave(w);  // copies the samples into the audio buffer
}

}  // namespace

bool Audio::Init() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return false;

    const Tone hit[] = {{1250, 1700, 0.07, 45, 0.10, 0.20, 0.60}};
    const Tone kill[] = {{880, 880, 0.05, 30, 0.05, 0.25, 0.55}, {1320, 1320, 0.12, 25, 0.0, 0.25, 0.55}};
    const Tone head[] = {{1500, 1500, 0.04, 30, 0.10, 0.30, 0.55}, {2200, 2400, 0.14, 22, 0.0, 0.20, 0.55}};
    const Tone miss[] = {{200, 110, 0.07, 50, 0.35, 0.0, 0.35}};
    const Tone tick[] = {{2600, 2600, 0.018, 180, 0.20, 0.0, 0.30}};
    const Tone beep[] = {{660, 660, 0.12, 12, 0.0, 0.15, 0.45}};
    const Tone go[] = {{990, 990, 0.22, 8, 0.0, 0.20, 0.50}};
    const Tone click[] = {{1800, 1400, 0.025, 120, 0.15, 0.0, 0.25}};
    const Tone shot[] = {{180, 60, 0.11, 30, 0.75, 0.2, 0.55}};       // your rifle: short noisy crack
    const Tone enemy[] = {{240, 90, 0.10, 34, 0.80, 0.1, 0.35}};      // enemy rifle, a bit higher and quieter
    const Tone hurt[] = {{320, 160, 0.12, 25, 0.25, 0.3, 0.45}};      // you took damage

    Sound base[static_cast<int>(Sfx::Count)];
    base[static_cast<int>(Sfx::Hit)] = MakeSound(hit, 1);
    base[static_cast<int>(Sfx::Kill)] = MakeSound(kill, 2);
    base[static_cast<int>(Sfx::Headshot)] = MakeSound(head, 2);
    base[static_cast<int>(Sfx::Miss)] = MakeSound(miss, 1);
    base[static_cast<int>(Sfx::Tick)] = MakeSound(tick, 1);
    base[static_cast<int>(Sfx::CountBeep)] = MakeSound(beep, 1);
    base[static_cast<int>(Sfx::CountGo)] = MakeSound(go, 1);
    base[static_cast<int>(Sfx::UiClick)] = MakeSound(click, 1);
    base[static_cast<int>(Sfx::Shot)] = MakeSound(shot, 1);
    base[static_cast<int>(Sfx::EnemyShot)] = MakeSound(enemy, 1);
    base[static_cast<int>(Sfx::Hurt)] = MakeSound(hurt, 1);

    for (int s = 0; s < static_cast<int>(Sfx::Count); ++s) {
        sounds_[s][0] = base[s];
        // Aliases share the sample data but play independently.
        for (int v = 1; v < kVoices; ++v) sounds_[s][v] = LoadSoundAlias(base[s]);
    }
    ready_ = true;
    return true;
}

void Audio::Shutdown() {
    if (!ready_) return;
    for (int s = 0; s < static_cast<int>(Sfx::Count); ++s) {
        for (int v = 1; v < kVoices; ++v) UnloadSoundAlias(sounds_[s][v]);
        UnloadSound(sounds_[s][0]);
    }
    CloseAudioDevice();
    ready_ = false;
}

void Audio::SetVolumes(float master, float hit) {
    master_ = master;
    hit_ = hit;
}

void Audio::Play(Sfx s) {
    if (!ready_) return;
    const int i = static_cast<int>(s);
    const bool hitType = (s == Sfx::Hit || s == Sfx::Kill || s == Sfx::Headshot || s == Sfx::Tick ||
                          s == Sfx::Miss);
    const float vol = master_ * (hitType ? hit_ : 1.0f);
    if (vol <= 0.0f) return;
    Sound& snd = sounds_[i][nextVoice_[i]];
    nextVoice_[i] = (nextVoice_[i] + 1) % kVoices;
    SetSoundVolume(snd, vol);
    PlaySound(snd);
}
