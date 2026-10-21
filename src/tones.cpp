#include "tones.h"

#include <QSettings>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <windows.h>
#include <mmsystem.h>

static int s_volume = -1;  // cached; -1 = not yet loaded

static int loadVolume()
{
    if (s_volume < 0) {
        QSettings s;
        s_volume = std::clamp(s.value("tone_volume", 70).toInt(), 0, 100);
    }
    return s_volume;
}

int  Tones::volume()          { return loadVolume(); }
void Tones::setVolume(int v)
{
    s_volume = std::clamp(v, 0, 100);
    QSettings s;
    s.setValue("tone_volume", s_volume);
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr int SR = 44100;

// ── WAV helpers ──────────────────────────────────────────────────────

static void writeHeader(std::vector<char> &wav, int nSamples)
{
    int db = nSamples * 2;
    wav.resize(44);
    char *p = wav.data();
    auto w16 = [&](int16_t v){ memcpy(p,&v,2); p+=2; };
    auto w32 = [&](int32_t v){ memcpy(p,&v,4); p+=4; };
    auto wT  = [&](const char*s){ memcpy(p,s,4); p+=4; };
    wT("RIFF"); w32(36+db); wT("WAVE");
    wT("fmt "); w32(16); w16(1); w16(1);
    w32(SR); w32(SR*2); w16(2); w16(16);
    wT("data"); w32(db);
}

static void addSample(std::vector<char> &wav, float s)
{
    s = fmaxf(-1.0f, fminf(1.0f, s));
    auto pcm = (int16_t)(s * 32767.0f);
    wav.push_back((char)(pcm & 0xFF));
    wav.push_back((char)((pcm >> 8) & 0xFF));
}

static void addBell(std::vector<char> &wav, float freq, int ms,
                    float vol, float decay = 10.0f)
{
    int n = SR * ms / 1000;
    // Scale by user volume
    float vScale = loadVolume() / 100.0f;
    vol *= vScale;
    for (int i = 0; i < n; ++i) {
        float t = (float)i / SR;
        float env = expf(-decay * t);
        float s = 0.0f;
        s += 1.00f * sinf(2.0f*(float)M_PI * freq * 1.0f * t);
        s += 0.50f * sinf(2.0f*(float)M_PI * freq * 2.0f * t);
        s += 0.25f * sinf(2.0f*(float)M_PI * freq * 3.0f * t);
        s += 0.12f * sinf(2.0f*(float)M_PI * freq * 4.17f * t);
        s *= env * vol / 1.87f;
        addSample(wav, s);
    }
}

static void addSilence(std::vector<char> &wav, int ms)
{
    int n = SR * ms / 1000;
    for (int i = 0; i < n; ++i) addSample(wav, 0.0f);
}

// WAVs are regenerated each time so volume changes apply immediately.

static std::vector<char> buildLoading()
{
    std::vector<char> wav;
    int nSamples = SR / 2;
    writeHeader(wav, nSamples);
    addBell(wav, 1200, 200, 0.55f, 12.0f);
    addSilence(wav, 300);
    return wav;
}

static std::vector<char> buildReady()
{
    std::vector<char> wav;
    int nSamples = SR * 60 / 1000 + SR * 80 / 1000;
    writeHeader(wav, nSamples);
    addBell(wav, 600,  60, 0.60f, 25.0f);
    addBell(wav, 900,  80, 0.65f, 20.0f);
    return wav;
}

// PlaySound(SND_ASYNC | SND_MEMORY) keeps reading the buffer while the
// sound plays, so stop playback *before* the old buffer is replaced.

void Tones::playLoading()
{
    static std::vector<char> wav;
    auto next = buildLoading();
    Tones::stop();
    wav.swap(next);
    PlaySoundA(wav.data(), nullptr,
               SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
}

void Tones::playReady()
{
    static std::vector<char> wav;
    auto next = buildReady();
    Tones::stop();
    wav.swap(next);
    PlaySoundA(wav.data(), nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void Tones::stop()
{
    PlaySoundA(nullptr, nullptr, 0);
}

