#include "transcriber.h"
#include "debuglog.h"
#include "textcleanup.h"
#include "whisper.h"
#include "ggml-backend.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <cmath>
#include <algorithm>
#include <cstring>

#define LOG(msg) DLOG("whisper", msg)

// Route whisper/ggml's own log output (normally stderr, which a GUI app
// doesn't have) into our debug log.
static void whisperLogCallback(ggml_log_level level, const char *text, void *)
{
#ifdef WHISPER_VTT_DEBUG
    if (level >= GGML_LOG_LEVEL_INFO)
        DLOG("ggml", QString::fromUtf8(text).trimmed());
#else
    Q_UNUSED(level); Q_UNUSED(text);
#endif
}

// ggml's backends are DLLs next to the exe (see CMakeLists.txt).  Load
// them once, from there only, before anything asks ggml for a device.
static void loadBackends()
{
    static std::once_flag once;
    std::call_once(once, [] {
        ggml_backend_load_all_from_path(
            QCoreApplication::applicationDirPath().toUtf8().constData());
    });
}

// The device whisper.cpp uses when asked for a GPU: a dedicated one if
// present, else an integrated one.
static ggml_backend_dev_t gpuDevice()
{
    loadBackends();
    if (ggml_backend_dev_t dev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU))
        return dev;
    return ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_IGPU);
}

bool Transcriber::gpuAvailable()
{
    return gpuDevice() != nullptr;
}

bool Transcriber::hasDedicatedGpu()
{
    loadBackends();
    return ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU) != nullptr;
}

QString Transcriber::gpuName()
{
    ggml_backend_dev_t dev = gpuDevice();
    return dev ? QString::fromUtf8(ggml_backend_dev_description(dev)) : QString();
}

// Settings shared by dictation and the GPU warm-up.  `abort` stops a
// run early when set.
static whisper_full_params fullParams(std::atomic<bool> *abort)
{
    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_realtime   = false;
    params.print_progress   = false;
    params.print_timestamps = false;
    params.print_special    = false;
    params.no_context       = true;   // each dictation is independent
    params.suppress_nst     = true;   // no "[MUSIC]", "(sighs)" etc.
    // Beyond ~8 threads whisper gains little, and pegging every logical
    // core makes the rest of the system stutter.
    params.n_threads        = std::clamp(QThread::idealThreadCount(), 1, 8);
    params.language         = "en";
    params.abort_callback = [](void *ud) {
        return static_cast<std::atomic<bool> *>(ud)->load();
    };
    params.abort_callback_user_data = abort;
    return params;
}

Transcriber::Transcriber(const QString &modelPath, QObject *parent)
    : QObject(parent), m_modelPath(modelPath)
{
    whisper_log_set(whisperLogCallback, nullptr);
}

Transcriber::~Transcriber()
{
    m_abort = true;
    std::lock_guard<std::mutex> lock(m_mutex);
    unloadLocked();
    if (m_vad) whisper_vad_free(m_vad);
}

void Transcriber::unloadModel()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_ctx) {
        unloadLocked();
        emit statusChanged(QStringLiteral("Model unloaded"));
    }
}

void Transcriber::setModelPath(const QString &path)
{
    // Only records the path; the next loadModel() (on a worker thread)
    // frees the old model, so the GUI thread never waits on whisper_free.
    std::lock_guard<std::mutex> lock(m_mutex);
    m_modelPath = path;
}

void Transcriber::setVadModelPath(const QString &path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (path == m_vadPath) return;
    if (m_vad) {
        whisper_vad_free(m_vad);
        m_vad = nullptr;
    }
    m_vadPath = path;
    m_vadFailed = false;
}

void Transcriber::unloadLocked()
{
    if (m_ctx) {
        LOG("Unloading model");
        whisper_free(m_ctx);
        m_ctx = nullptr;
        m_loaded = false;
        m_gpuActive = false;
    }
}

bool Transcriber::loadModel(bool useGpu)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Keep the loaded model if it's the right file on the right device.
    if (m_ctx && m_loadedPath == m_modelPath && m_gpuActive == (useGpu && gpuAvailable()))
        return true;

    unloadLocked();

    if (!QFileInfo::exists(m_modelPath)) {
        LOG(QStringLiteral("Model not found: %1").arg(m_modelPath));
        emit statusChanged(
            QStringLiteral("Model file not found: %1").arg(m_modelPath));
        return false;
    }

    // whisper.cpp silently falls back to CPU when no GPU device exists
    // (CPU-only build, no Vulkan driver), so check for one.
    const bool gpu = useGpu && gpuAvailable();
    if (useGpu && !gpu)
        LOG("GPU requested but no GPU backend device found — using CPU");

    LOG(QStringLiteral("Loading model (gpu=%1): %2").arg(gpu).arg(m_modelPath));
    emit statusChanged(gpu
        ? QStringLiteral("Loading Whisper model on GPU...")
        : QStringLiteral("Loading Whisper model on CPU..."));

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu    = gpu;
    cparams.flash_attn = gpu;

    const std::string path = m_modelPath.toStdString();
    m_ctx = whisper_init_from_file_with_params(path.c_str(), cparams);

    if (!m_ctx && gpu) {
        LOG("GPU init failed, retrying CPU");
        emit statusChanged(QStringLiteral("GPU init failed, falling back to CPU..."));
        cparams.use_gpu = false;
        cparams.flash_attn = false;
        m_ctx = whisper_init_from_file_with_params(path.c_str(), cparams);
        m_gpuActive = false;
    } else {
        m_gpuActive = gpu && m_ctx;
    }
    m_loaded = (m_ctx != nullptr);
    m_loadedPath = m_ctx ? m_modelPath : QString();

    if (m_ctx) {
        LOG(QStringLiteral("Model loaded OK (gpu=%1)").arg(m_gpuActive.load()));
        if (m_gpuActive) warmUpLocked();
        emit statusChanged(m_gpuActive
            ? QStringLiteral("Model loaded on GPU (%1) — ready").arg(gpuName())
            : QStringLiteral("Model loaded on CPU — ready"));
        return true;
    }

    LOG("Model load FAILED");
    emit statusChanged(QStringLiteral("Failed to load Whisper model"));
    return false;
}

// A GPU's first inference is slow: the driver compiles each compute
// pipeline the first time it is used.  Which pipelines run depends on
// tensor shapes (the encoder, the prompt, single-token decoding, the
// attention length as it grows token by token, and the batched decoding
// of whisper's temperature fallback), so a token or two of warm-up isn't
// enough.  Exercise them all now on a second of silence, while the
// loading chime plays: 128 single-token steps with end-of-text
// suppressed, then a short batched pass.
void Transcriber::warmUpLocked()
{
    QElapsedTimer timer;
    timer.start();
    const std::vector<float> silence(16000, 0.0f);

    whisper_full_params params = fullParams(&m_abort);
    params.temperature_inc = 0.0f;   // no fallback retries
    params.no_timestamps   = true;   // a timestamp past the audio's end stops decoding
    params.max_tokens      = 128;
    params.logits_filter_callback = [](whisper_context *ctx, whisper_state *,
                                       const whisper_token_data *, int, float *logits, void *) {
        logits[whisper_token_eot(ctx)] = -INFINITY;
    };
    whisper_full(m_ctx, params, silence.data(), int(silence.size()));

    params.temperature = 0.5f;       // sampling decodes best_of candidates as one batch
    params.max_tokens  = 8;
    whisper_full(m_ctx, params, silence.data(), int(silence.size()));
    LOG(QStringLiteral("GPU warm-up took %1 ms").arg(timer.elapsed()));
}

// Whisper hallucinates on non-speech: the room noise between finishing a
// sentence and pressing F9 comes back as "Thank you.", "Okay." and the
// like.  Keep only what Silero VAD calls speech, padded so soft word
// endings survive; pauses longer than a second are shortened, shorter
// ones are left alone.  Without a VAD model the audio passes through
// untouched.  Returns false if the recording holds no speech at all.
bool Transcriber::speechOnlyLocked(const std::vector<float> &in,
                                   std::vector<float> &out)
{
    out.clear();
    if (!m_vad && !m_vadFailed && !m_vadPath.isEmpty()) {
        // Read through QFile so the model can be a Qt resource.
        QFile f(m_vadPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray data = f.readAll();
            struct Reader { const QByteArray *d; qsizetype pos; } r{&data, 0};
            whisper_model_loader loader{};
            loader.context = &r;
            loader.read = [](void *ctx, void *out, size_t n) -> size_t {
                auto *r = static_cast<Reader *>(ctx);
                n = std::min(n, size_t(r->d->size() - r->pos));
                memcpy(out, r->d->constData() + r->pos, n);
                r->pos += qsizetype(n);
                return n;
            };
            loader.eof = [](void *ctx) {
                auto *r = static_cast<Reader *>(ctx);
                return r->pos >= r->d->size();
            };
            loader.close = [](void *) {};

            whisper_vad_context_params vcp = whisper_vad_default_context_params();
            vcp.n_threads = 2;
            vcp.use_gpu   = false;   // ~1 MB model; CPU takes milliseconds
            m_vad = whisper_vad_init_with_params(&loader, vcp);
        }
        if (!m_vad) {
            m_vadFailed = true;
            LOG(QStringLiteral("VAD model unavailable: %1").arg(m_vadPath));
        }
    }
    if (!m_vad) {
        out = in;
        return true;
    }

    whisper_vad_params vp = whisper_vad_default_params();
    vp.threshold               = 0.5f;
    vp.min_speech_duration_ms  = 250;
    vp.min_silence_duration_ms = 1000;
    vp.speech_pad_ms           = 300;
    whisper_vad_segments *segs =
        whisper_vad_segments_from_samples(m_vad, vp, in.data(), (int)in.size());
    if (!segs) {
        LOG("VAD failed — using the whole recording");
        out = in;
        return true;
    }

    // Segment times are in centiseconds.
    auto toSample = [&](float cs) {
        return std::min(in.size(), (size_t)std::max(0.0f, cs * 160.0f));
    };
    const int n = whisper_vad_segments_n_segments(segs);
    for (int i = 0; i < n; ++i) {
        const size_t a = toSample(whisper_vad_segments_get_segment_t0(segs, i));
        const size_t b = toSample(whisper_vad_segments_get_segment_t1(segs, i));
        if (b <= a) continue;
        const size_t at = out.size();
        out.insert(out.end(), in.begin() + a, in.begin() + b);
        // 10 ms fades so the splice points aren't clicks.
        const size_t fade = std::min<size_t>(160, (b - a) / 2);
        for (size_t k = 0; k < fade; ++k) {
            const float g = float(k) / fade;
            out[at + k] *= g;
            out[out.size() - 1 - k] *= g;
        }
    }
    whisper_vad_free_segments(segs);
    LOG(QStringLiteral("VAD: %1 speech segment(s), %2 s of %3 s kept")
            .arg(n).arg(out.size() / 16000.0, 0, 'f', 2)
            .arg(in.size() / 16000.0, 0, 'f', 2));
    return !out.empty();
}

QString Transcriber::transcribe(const std::vector<float> &audio)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_ctx || audio.empty()) return {};

#ifdef WHISPER_VTT_DEBUG
    float peak = 0.0f;
    double rms = 0.0;
    for (float s : audio) {
        peak = std::max(peak, std::abs(s));
        rms += double(s) * s;
    }
    LOG(QStringLiteral("transcribe(): %1 samples (%2 s), peak=%3, rms=%4")
            .arg(audio.size()).arg(audio.size() / 16000.0, 0, 'f', 2)
            .arg((double)peak, 0, 'f', 6).arg(std::sqrt(rms / audio.size()), 0, 'f', 6));
#endif

    if (m_abort) return {};
    std::vector<float> speech;
    if (!speechOnlyLocked(audio, speech)) {
        LOG("VAD: no speech in recording");
        return {};
    }

    const whisper_full_params params = fullParams(&m_abort);
#ifdef WHISPER_VTT_DEBUG
    QElapsedTimer timer;
    timer.start();
    whisper_reset_timings(m_ctx);
#endif
    int rc = whisper_full(m_ctx, params, speech.data(), (int)speech.size());
#ifdef WHISPER_VTT_DEBUG
    if (const whisper_timings *t = whisper_get_timings(m_ctx))
        LOG(QStringLiteral("whisper_full → %1 in %2 ms (encode %3, decode %4, batch %5)")
                .arg(rc).arg(timer.elapsed()).arg(double(t->encode_ms), 0, 'f', 0)
                .arg(double(t->decode_ms), 0, 'f', 0).arg(double(t->batchd_ms), 0, 'f', 0));
#endif
    if (rc != 0) {
        if (!m_abort)
            emit statusChanged(QStringLiteral("Transcription failed (rc=%1)").arg(rc));
        return {};
    }

    QStringList segments;
    const int n = whisper_full_n_segments(m_ctx);
    for (int i = 0; i < n; ++i) {
        segments.append(QString::fromUtf8(whisper_full_get_segment_text(m_ctx, i)));
        LOG(QStringLiteral("  seg[%1]: \"%2\"").arg(i).arg(segments.last().trimmed()));
    }

    const QString result = TextCleanup::assemble(segments);
    LOG(QStringLiteral("Result: \"%1\"").arg(result));
    return result;
}
