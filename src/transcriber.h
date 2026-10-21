#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

struct whisper_context;
struct whisper_vad_context;

// Thread-safety: loadModel(), unloadModel() and transcribe() may be
// called from any thread; they serialise on an internal mutex so the
// whisper context is never freed while it is in use.
class Transcriber : public QObject {
    Q_OBJECT
public:
    explicit Transcriber(const QString &modelPath, QObject *parent = nullptr);
    ~Transcriber() override;

    // Load the model.  Blocks — call from a worker thread.
    bool loadModel(bool useGpu);

    // Unload the model (frees context and VRAM/RAM).
    void unloadModel();

    // Switch model file; takes effect at the next loadModel().
    void setModelPath(const QString &path);

    // Silero VAD model used to cut silence/noise before transcription
    // (Whisper invents "Thank you." etc. on trailing room noise).
    // Empty or missing file = transcribe the whole recording.
    void setVadModelPath(const QString &path);

    // Blocking — call from a worker thread.
    QString transcribe(const std::vector<float> &audio);

    // Stop any in-flight transcribe() early and refuse new ones.
    // One-way — used on shutdown.
    void abort() { m_abort = true; }

    bool isModelLoaded() const { return m_loaded; }
    bool isGpuActive() const { return m_gpuActive; }

    // True if this build and machine have a GPU that ggml can use
    // (Vulkan: NVIDIA, AMD or Intel, dedicated or integrated).
    static bool gpuAvailable();
    // True if that GPU is a dedicated graphics card.
    static bool hasDedicatedGpu();
    // The GPU's name as its driver reports it; empty if there is none.
    static QString gpuName();

signals:
    void statusChanged(const QString &msg);

private:
    void unloadLocked();
    void warmUpLocked();
    bool speechOnlyLocked(const std::vector<float> &in, std::vector<float> &out);

    QString             m_modelPath;
    QString             m_loadedPath;   // file m_ctx was loaded from
    std::mutex          m_mutex;        // guards m_ctx, m_modelPath and the VAD
    whisper_context    *m_ctx = nullptr;
    QString             m_vadPath;      // guarded by m_mutex
    whisper_vad_context *m_vad = nullptr;
    bool                m_vadFailed = false;
    std::atomic<bool>   m_loaded{false};
    std::atomic<bool>   m_gpuActive{false};
    std::atomic<bool>   m_abort{false};
};
