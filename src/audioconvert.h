#pragma once

#include <QByteArray>
#include <vector>

// Raw 16-bit PCM as captured from the device.
struct PcmCapture {
    QByteArray pcm;
    int sampleRate = 0;
    int channels   = 0;

    double seconds() const {
        return (sampleRate > 0 && channels > 0)
            ? pcm.size() / (2.0 * channels * sampleRate) : 0.0;
    }
};

namespace AudioConvert {
    constexpr int kWhisperRate = 16000;

    // Converts a capture to what whisper expects: 16 kHz mono float,
    // normalised.  CPU-heavy for long recordings — call from a worker
    // thread.  |rawPeak| receives the peak level before normalisation
    // (0..1), so callers can detect a silent/muted microphone.
    std::vector<float> toWhisperInput(const PcmCapture &cap,
                                      float *rawPeak = nullptr);
}
