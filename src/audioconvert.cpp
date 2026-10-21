#include "audioconvert.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

static constexpr double kPi = 3.14159265358979323846;
// Normalisation never boosts by more than this (~34 dB).  Unlimited gain
// turns background hiss into full-scale noise, which whisper happily
// "transcribes" into hallucinated sentences.
static constexpr float kMaxGain = 50.0f;

// Windowed-sinc low-pass FIR (Blackman), cutoff given as a fraction of
// the input sample rate.
static std::vector<float> designLowPass(double cutoff, int halfLen)
{
    std::vector<float> h(2 * halfLen + 1);
    double sum = 0.0;
    for (int i = -halfLen; i <= halfLen; ++i) {
        const double x = 2.0 * cutoff * i;
        const double sinc = (i == 0) ? 1.0 : std::sin(kPi * x) / (kPi * x);
        const double w = 0.42 + 0.5 * std::cos(kPi * i / halfLen)
                              + 0.08 * std::cos(2.0 * kPi * i / halfLen);
        h[i + halfLen] = (float)(2.0 * cutoff * sinc * w);
        sum += h[i + halfLen];
    }
    for (float &c : h) c = (float)(c / sum);   // unity gain at DC
    return h;
}

std::vector<float> AudioConvert::toWhisperInput(const PcmCapture &cap, float *rawPeak)
{
    if (rawPeak) *rawPeak = 0.0f;
    if (cap.pcm.size() < 2 || cap.channels < 1 || cap.sampleRate < 1) return {};

    const auto *src = reinterpret_cast<const int16_t*>(cap.pcm.constData());
    const qsizetype frames = (cap.pcm.size() / 2) / cap.channels;

    std::vector<float> mono((size_t)frames);
    for (qsizetype i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < cap.channels; ++c)
            sum += src[i * cap.channels + c];
        mono[(size_t)i] = sum / (32768.0f * cap.channels);
    }

    std::vector<float> out;
    if (cap.sampleRate == kWhisperRate) {
        out = std::move(mono);
    } else {
        // Low-pass below the new Nyquist (8 kHz) before decimating so
        // content above it doesn't alias down into the speech band.
        const double ratio = (double)cap.sampleRate / kWhisperRate;
        const int halfLen = 48;
        const auto h = designLowPass(7200.0 / cap.sampleRate, halfLen);
        const qsizetype n = (qsizetype)mono.size();

        auto filtered = [&](qsizetype c) {
            const qsizetype lo = std::max<qsizetype>(0, c - halfLen);
            const qsizetype hi = std::min<qsizetype>(n - 1, c + halfLen);
            float acc = 0.0f;
            for (qsizetype k = lo; k <= hi; ++k)
                acc += h[(size_t)(k - c + halfLen)] * mono[(size_t)k];
            return acc;
        };

        const qsizetype outLen = (qsizetype)(n / ratio);
        out.resize((size_t)std::max<qsizetype>(0, outLen));
        for (qsizetype i = 0; i < outLen; ++i) {
            const double si = i * ratio;
            const qsizetype idx = (qsizetype)si;
            const double f = si - (double)idx;
            float v = filtered(idx);
            if (f > 1e-6 && idx + 1 < n)
                v = (float)(v * (1.0 - f) + filtered(idx + 1) * f);
            out[(size_t)i] = v;
        }
    }

    // Normalise quiet audio so Whisper can detect speech.
    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    if (rawPeak) *rawPeak = peak;
    if (peak > 0.00001f && peak < 0.8f) {
        const float gain = std::min(0.9f / peak, kMaxGain);
        for (float &s : out) s *= gain;
    }
    return out;
}
