#include "Audio.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace bh {

namespace {

constexpr int kRate = 44100;
constexpr float kTau = 6.2831853f;

Sound synth(float seconds, const std::function<float(float t, float k)> &f)
{
    int n = (int)(seconds * kRate);
    std::vector<int16_t> data(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / kRate;
        float v = f(t, t / seconds);
        v = std::tanh(v);  // мягкое ограничение
        data[i] = (int16_t)(v * 32000);
    }
    Wave w{(unsigned)n, kRate, 16, 1, data.data()};
    return LoadSoundFromWave(w);  // данные копируются
}

float noise()
{
    static std::mt19937 rng(42);
    return std::uniform_real_distribution<float>(-1, 1)(rng);
}

float env(float k, float attack, float decayPow)
{
    float a = k < attack ? k / attack : 1.0f;
    return a * std::pow(1.0f - k, decayPow);
}

}  // namespace

void Audio::init()
{
    InitAudioDevice();
    ready_ = IsAudioDeviceReady();
    if (!ready_) return;

    // Глухой гравитационный «тук» с плавающей высотой.
    sounds_[SFX_CLICK] = synth(0.16f, [](float t, float k) {
        float ph = kTau * (120 * t - 180 * t * t);
        return 0.9f * std::sin(ph) * env(k, 0.02f, 3) + 0.08f * noise() * env(k, 0.01f, 8);
    });
    sounds_[SFX_CRIT] = synth(0.45f, [](float t, float k) {
        float boom = std::sin(kTau * (70 * t - 40 * t * t)) * env(k, 0.01f, 2);
        float zap = std::sin(kTau * (1400 * t - 1500 * t * t)) * env(k, 0.005f, 4) * 0.4f;
        return 1.1f * boom + zap + 0.15f * noise() * env(k, 0.01f, 6);
    });
    sounds_[SFX_BUY] = synth(0.22f, [](float t, float k) {
        float f = t < 0.08f ? 660 : 990;
        float tri = std::asin(std::sin(kTau * f * t)) * 0.64f;
        return 0.55f * tri * env(k, 0.02f, 1.5f);
    });
    sounds_[SFX_NODE] = synth(0.5f, [](float t, float k) {
        const float notes[] = {523.3f, 659.3f, 784.0f, 1046.5f};
        int i = std::min(3, (int)(t / 0.08f));
        float s = std::sin(kTau * notes[i] * t) + 0.3f * std::sin(kTau * notes[i] * 2 * t);
        return 0.45f * s * env(k, 0.01f, 1.2f);
    });
    sounds_[SFX_ACH] = synth(0.9f, [](float t, float k) {
        const float notes[] = {523.3f, 784.0f, 1046.5f, 1318.5f};
        int i = std::min(3, (int)(t / 0.11f));
        float s = 0;
        for (int h = 1; h <= 3; h++) s += std::sin(kTau * notes[i] * h * t) / h;
        float sparkle = std::sin(kTau * 3000 * t) * (noise() > 0.97f ? 1.0f : 0.0f);
        return 0.4f * s * env(k, 0.01f, 1.0f) + 0.2f * sparkle * (1 - k);
    });
    sounds_[SFX_COMET] = synth(0.6f, [](float t, float k) {
        float f = 1800 + 1200 * std::sin(kTau * 9 * t);
        return 0.35f * std::sin(kTau * f * t) * env(k, 0.02f, 1.5f) + 0.1f * noise() * (1 - k);
    });
    sounds_[SFX_WAVE] = synth(1.2f, [](float t, float k) {
        float s = std::sin(kTau * (45 * t - 8 * t * t));
        return 1.3f * s * env(k, 0.04f, 1.5f) + 0.25f * noise() * env(k, 0.02f, 3);
    });
    sounds_[SFX_FRENZY] = synth(0.9f, [](float t, float k) {
        float ph = kTau * (200 * t + 600 * t * t);
        float sq = std::sin(ph) > 0 ? 1.0f : -1.0f;
        return 0.3f * sq * env(k, 0.05f, 1.0f) + 0.4f * std::sin(ph * 0.5f) * env(k, 0.05f, 1);
    });
    sounds_[SFX_COLLAPSE] = synth(4.0f, [](float t, float k) {
        float f = 110 * std::pow(1.0f - k, 2.0f) + 25;
        float s = std::sin(kTau * f * t) + 0.5f * std::sin(kTau * f * 1.5f * t);
        float a = k < 0.9f ? k / 0.9f : (1 - k) * 10;
        return 1.2f * s * a + 0.3f * noise() * a * k;
    });
    sounds_[SFX_DENY] = synth(0.12f, [](float t, float k) {
        return 0.3f * (std::sin(kTau * 140 * t) > 0 ? 1.0f : -1.0f) * env(k, 0.01f, 2);
    });
    sounds_[SFX_RANK] = synth(1.4f, [](float t, float k) {
        float s = std::sin(kTau * 220 * t) + std::sin(kTau * 277.2f * t) + std::sin(kTau * 329.6f * t)
                + 0.5f * std::sin(kTau * 440 * t);
        return 0.3f * s * env(k, 0.15f, 1.3f);
    });

    // Фоновый гул: целое число периодов на 4 секунды — петля без щелчков.
    drone_ = synth(4.0f, [](float t, float) {
        float lfo = 0.6f + 0.4f * std::sin(kTau * 0.25f * t);
        return 0.22f * (std::sin(kTau * 55 * t) + 0.6f * std::sin(kTau * 82.5f * t) * lfo
                        + 0.25f * std::sin(kTau * 110.25f * t));
    });
    SetSoundVolume(drone_, 0.35f);
}

void Audio::shutdown()
{
    if (!ready_) return;
    for (auto &s : sounds_) UnloadSound(s);
    UnloadSound(drone_);
    CloseAudioDevice();
    ready_ = false;
}

void Audio::play(Sfx s, float pitch, float volume)
{
    if (!ready_ || muted_) return;
    SetSoundPitch(sounds_[s], pitch);
    SetSoundVolume(sounds_[s], volume);
    PlaySound(sounds_[s]);
}

void Audio::update()
{
    if (!ready_) return;
    if (muted_) {
        if (IsSoundPlaying(drone_)) StopSound(drone_);
        return;
    }
    if (!IsSoundPlaying(drone_)) PlaySound(drone_);
}

}  // namespace bh
