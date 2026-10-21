#include "audioconvert.h"

#include <QTest>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

constexpr double kPi = 3.14159265358979323846;

// |seconds| of a sine at |freq| Hz and |amplitude| (0..1), identical on
// every channel.
PcmCapture sine(int rate, int channels, double freq, double amplitude,
                double seconds = 1.0)
{
    const int frames = int(rate * seconds);
    PcmCapture cap;
    cap.sampleRate = rate;
    cap.channels   = channels;
    cap.pcm.resize(qsizetype(frames) * channels * 2);
    auto *out = reinterpret_cast<int16_t *>(cap.pcm.data());
    for (int i = 0; i < frames; ++i) {
        const auto s = int16_t(std::lround(
            amplitude * 32767.0 * std::sin(2.0 * kPi * freq * i / rate)));
        for (int c = 0; c < channels; ++c) out[i * channels + c] = s;
    }
    return cap;
}

float peakOf(const std::vector<float> &v, size_t margin = 0)
{
    float p = 0.0f;
    for (size_t i = margin; i + margin < v.size(); ++i) p = std::max(p, std::abs(v[i]));
    return p;
}

// Steady-state output level in input units: normalisation gain divided
// out, and the first/last few ms skipped (the filter kernel is truncated
// there).
float steadyStateLevel(const PcmCapture &cap)
{
    float raw = 0.0f;
    const auto out = AudioConvert::toWhisperInput(cap, &raw);
    const float gain = peakOf(out) / raw;
    return peakOf(out, 160) / gain;
}

} // namespace

class TestAudioConvert : public QObject {
    Q_OBJECT
private slots:
    void emptyCapture();
    void captureDuration();
    void resamplesToWhisperRate_data();
    void resamplesToWhisperRate();
    void stereoIsDownmixed();
    void suppressesAliasing();
    void normalisesQuietAudio();
    void capsNormalisationGain();
    void leavesLoudAudioAlone();
};

void TestAudioConvert::emptyCapture()
{
    float peak = -1.0f;
    QVERIFY(AudioConvert::toWhisperInput(PcmCapture{}, &peak).empty());
    QCOMPARE(peak, 0.0f);
}

void TestAudioConvert::captureDuration()
{
    QCOMPARE(sine(48000, 2, 440, 0.5, 2.0).seconds(), 2.0);
    QCOMPARE(PcmCapture{}.seconds(), 0.0);
}

void TestAudioConvert::resamplesToWhisperRate_data()
{
    QTest::addColumn<int>("rate");
    QTest::addColumn<int>("channels");

    QTest::newRow("16 kHz mono")   << 16000 << 1;
    QTest::newRow("44.1 kHz mono") << 44100 << 1;
    QTest::newRow("48 kHz mono")   << 48000 << 1;
    QTest::newRow("48 kHz stereo") << 48000 << 2;
}

void TestAudioConvert::resamplesToWhisperRate()
{
    QFETCH(int, rate);
    QFETCH(int, channels);

    const auto out = AudioConvert::toWhisperInput(sine(rate, channels, 440, 0.5));
    QVERIFY(std::abs(int(out.size()) - AudioConvert::kWhisperRate) <= 1);
}

void TestAudioConvert::stereoIsDownmixed()
{
    // Left and right in antiphase cancel out when averaged.
    PcmCapture cap = sine(48000, 2, 440, 0.5);
    auto *s = reinterpret_cast<int16_t *>(cap.pcm.data());
    for (qsizetype i = 1; i < cap.pcm.size() / 2; i += 2) s[i] = int16_t(-s[i]);

    float peak = -1.0f;
    AudioConvert::toWhisperInput(cap, &peak);
    QVERIFY(peak < 1e-4f);
}

void TestAudioConvert::suppressesAliasing()
{
    // 12 kHz is above the 8 kHz Nyquist limit at 16 kHz; without the
    // low-pass filter it would fold down to an audible 4 kHz tone.
    const float alias  = steadyStateLevel(sine(48000, 1, 12000, 0.5));
    const float speech = steadyStateLevel(sine(48000, 1, 1000, 0.5));

    QVERIFY2(alias < 0.5f * 0.001f, qPrintable(QString::number(alias)));   // > 60 dB down
    QVERIFY2(std::abs(speech - 0.5f) < 0.01f, qPrintable(QString::number(speech)));
}

void TestAudioConvert::normalisesQuietAudio()
{
    float raw = 0.0f;
    const auto out = AudioConvert::toWhisperInput(sine(16000, 1, 440, 0.1), &raw);
    QVERIFY(std::abs(raw - 0.1f) < 0.001f);
    QVERIFY(std::abs(peakOf(out) - 0.9f) < 0.001f);
}

void TestAudioConvert::capsNormalisationGain()
{
    // Near-silence is boosted at most 50x, so hiss stays hiss.
    float raw = 0.0f;
    const auto out = AudioConvert::toWhisperInput(sine(16000, 1, 440, 0.001), &raw);
    QVERIFY(std::abs(peakOf(out) - raw * 50.0f) < 1e-4f);
}

void TestAudioConvert::leavesLoudAudioAlone()
{
    const auto out = AudioConvert::toWhisperInput(sine(16000, 1, 440, 0.85));
    QVERIFY(std::abs(peakOf(out) - 0.85f) < 0.001f);
}

QTEST_APPLESS_MAIN(TestAudioConvert)
#include "tst_audioconvert.moc"
