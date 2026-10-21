#include "audiorecorder.h"
#include "debuglog.h"
#include <cstring>

#define LOG(msg) DLOG("audio", msg)

AudioRecorder::AudioRecorder(QObject *parent) : QObject(parent)
{
    m_pollTimer.setInterval(50);
    connect(&m_pollTimer, &QTimer::timeout, this, &AudioRecorder::poll);
}

AudioRecorder::~AudioRecorder()
{
    if (m_recording) stop();
}

bool AudioRecorder::start()
{
    if (m_recording) return true;
    m_captured.clear();

    struct Fmt { DWORD rate; WORD ch; };
    static const Fmt fmts[] = {
        {48000,1},{44100,1},{48000,2},{44100,2},{16000,1},
    };

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag     = WAVE_FORMAT_PCM;
    wfx.wBitsPerSample = 16;
    MMRESULT res = MMSYSERR_ERROR;

    for (auto &a : fmts) {
        wfx.nSamplesPerSec  = a.rate;
        wfx.nChannels       = a.ch;
        wfx.nBlockAlign     = wfx.nChannels * wfx.wBitsPerSample / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        res = waveInOpen(&m_hWave, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
        LOG(QStringLiteral("try %1Hz %2ch → %3").arg(a.rate).arg(a.ch).arg(res));
        if (res == MMSYSERR_NOERROR) break;
        m_hWave = nullptr;
    }

    if (res != MMSYSERR_NOERROR) {
        m_hWave = nullptr;
        LOG("no usable input device");
        return false;
    }

    m_sampleRate = (int)wfx.nSamplesPerSec;
    m_channels   = (int)wfx.nChannels;
    m_nextBuf    = 0;

    const int bufBytes = (int)wfx.nAvgBytesPerSec * BUF_MS / 1000;
    for (int i = 0; i < NUM_BUFS; ++i) {
        m_bufs[i].assign((size_t)bufBytes, '\0');
        memset(&m_hdrs[i], 0, sizeof(WAVEHDR));
        m_hdrs[i].lpData         = m_bufs[i].data();
        m_hdrs[i].dwBufferLength = (DWORD)bufBytes;
        if (waveInPrepareHeader(m_hWave, &m_hdrs[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
            waveInAddBuffer(m_hWave, &m_hdrs[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            LOG(QStringLiteral("buffer %1 setup failed").arg(i));
            closeDevice();
            return false;
        }
    }

    if (waveInStart(m_hWave) != MMSYSERR_NOERROR) {
        LOG("waveInStart failed");
        closeDevice();
        return false;
    }

    m_recording = true;
    m_pollTimer.start();
    return true;
}

void AudioRecorder::poll()
{
    collectDoneBuffers();
}

void AudioRecorder::collectDoneBuffers()
{
    // The driver fills buffers strictly in the order they were queued, so
    // walk the ring from the oldest one.  (Scanning 0..N-1 would append
    // chunks out of order whenever a poll was late and several finished.)
    for (int n = 0; n < NUM_BUFS; ++n) {
        WAVEHDR &h = m_hdrs[m_nextBuf];
        if (!(h.dwFlags & WHDR_DONE)) break;

        if (h.dwBytesRecorded > 0)
            m_captured.append(h.lpData, (qsizetype)h.dwBytesRecorded);
        if (m_recording && m_hWave) {
            h.dwBytesRecorded = 0;
            h.dwFlags &= WHDR_PREPARED;
            waveInAddBuffer(m_hWave, &h, sizeof(WAVEHDR));
        } else {
            h.dwFlags &= ~WHDR_DONE;
        }
        m_nextBuf = (m_nextBuf + 1) % NUM_BUFS;
    }
}

void AudioRecorder::closeDevice()
{
    if (!m_hWave) return;
    waveInReset(m_hWave);   // returns all queued buffers, marked done
    for (int i = 0; i < NUM_BUFS; ++i)
        if (m_hdrs[i].dwFlags & WHDR_PREPARED)
            waveInUnprepareHeader(m_hWave, &m_hdrs[i], sizeof(WAVEHDR));
    waveInClose(m_hWave);
    m_hWave = nullptr;
}

PcmCapture AudioRecorder::stop()
{
    PcmCapture cap;
    if (!m_recording) return cap;
    m_recording = false;
    m_pollTimer.stop();

    if (m_hWave) {
        waveInStop(m_hWave);
        waveInReset(m_hWave);
        collectDoneBuffers();
        closeDevice();
    }

    cap.pcm        = std::move(m_captured);
    cap.sampleRate = m_sampleRate;
    cap.channels   = m_channels;
    m_captured.clear();
    LOG(QStringLiteral("captured %1 s").arg(cap.seconds(), 0, 'f', 2));
    return cap;
}
