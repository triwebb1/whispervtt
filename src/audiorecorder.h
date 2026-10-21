#pragma once

#include "audioconvert.h"

#include <QObject>
#include <QTimer>
#include <vector>

#include <windows.h>
#include <mmsystem.h>

// Captures microphone audio from the default input device via the
// Win32 waveIn API.  Buffers are polled on the owning (GUI) thread.
class AudioRecorder : public QObject {
    Q_OBJECT
public:
    explicit AudioRecorder(QObject *parent = nullptr);
    ~AudioRecorder() override;

    // Returns false if no microphone could be opened.
    bool start();
    PcmCapture stop();
    bool isRecording() const { return m_recording; }

private slots:
    void poll();

private:
    void collectDoneBuffers();
    void closeDevice();

    HWAVEIN  m_hWave = nullptr;
    // 16 x 100 ms = 1.6 s of headroom if the GUI thread stalls.
    static constexpr int NUM_BUFS = 16;
    static constexpr int BUF_MS   = 100;
    WAVEHDR              m_hdrs[NUM_BUFS] = {};
    std::vector<char>    m_bufs[NUM_BUFS];
    int                  m_nextBuf = 0;   // next buffer the driver will finish

    int m_sampleRate    = 0;
    int m_channels      = 0;

    QByteArray m_captured;
    QTimer     m_pollTimer;
    bool       m_recording = false;
};
