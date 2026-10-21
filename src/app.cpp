#include "app.h"
#include "audiorecorder.h"
#include "editorwindow.h"
#include "hotkeymanager.h"
#include "livetyper.h"
#include "logpanel.h"
#include "modelcatalog.h"
#include "modelsetup.h"
#include "tones.h"
#include "transcriber.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QSettings>

using RecordingFlow::Action;
using RecordingFlow::Input;

// ── icon helpers ─────────────────────────────────────────────────────

// A white microphone on a rounded square of |bg|.  All geometry is a
// fraction of the icon size, so it renders crisply at every size.
static QPixmap drawMicIcon(int size, const QColor &bg)
{
    const qreal s = size;
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Background tile.
    const qreal margin = 0.06 * s;
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(margin, margin, s - 2 * margin, s - 2 * margin),
                      0.22 * s, 0.22 * s);

    // Capsule.
    const QRectF capsule((s - 0.30 * s) / 2, 0.14 * s, 0.30 * s, 0.40 * s);
    p.setBrush(Qt::white);
    p.drawRoundedRect(capsule, capsule.width() / 2, capsule.width() / 2);

    // Cradle, stem and base.
    p.setPen(QPen(Qt::white, 0.065 * s, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    const qreal cradleH = 0.32 * s;
    const QRectF cradle((s - 0.50 * s) / 2, capsule.bottom() - 0.45 * cradleH,
                        0.50 * s, cradleH);
    p.drawArc(cradle, 0, -180 * 16);
    const qreal stemTop = cradle.center().y() + cradleH / 2;
    const qreal stemBottom = stemTop + 0.12 * s;
    p.drawLine(QPointF(s / 2, stemTop), QPointF(s / 2, stemBottom));
    p.drawLine(QPointF(s / 2 - 0.12 * s, stemBottom), QPointF(s / 2 + 0.12 * s, stemBottom));
    return pix;
}

static QIcon micIcon(const QColor &bg)
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64}) icon.addPixmap(drawMicIcon(size, bg));
    return icon;
}

// ── settings ─────────────────────────────────────────────────────────

static constexpr char kGpu[]   = "gpu_enabled";
static constexpr char kModel[] = "model_file";
static constexpr char kRunKey[] =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static QString configuredModel()
{
    return QSettings().value(kModel, QString::fromLatin1(ModelCatalog::kDefaultModel)).toString();
}

// Below this raw peak level (~ -54 dBFS) the mic is effectively silent:
// muted, wrong device, or blocked by Windows privacy settings.  Feeding
// that to whisper only produces hallucinated text.
static constexpr float kSilentPeak = 0.002f;

// ── App ──────────────────────────────────────────────────────────────

App::App(QObject *parent)
    : QObject(parent),
      m_iconIdle(micIcon(QColor(0x1976D2))),
      m_iconRecording(micIcon(QColor(0xD32F2F))),
      m_iconWorking(micIcon(QColor(0xF59E0B)))
{
    m_gpuIdleTimer.setSingleShot(true);
    m_gpuIdleTimer.setInterval(GPU_IDLE_MS);
    connect(&m_gpuIdleTimer, &QTimer::timeout, this, &App::onGpuIdleTimeout);

    m_modelsSettle.setSingleShot(true);
    m_modelsSettle.setInterval(2000);
    connect(&m_modelsSettle, &QTimer::timeout, this, &App::checkForNewModel);

    m_recordLimitTimer.setSingleShot(true);
    m_recordLimitTimer.setInterval(RECORD_LIMIT_MS);
    connect(&m_recordLimitTimer, &QTimer::timeout, this, &App::onRecordLimit);
}

App::~App()
{
    // Stop new input first, then let in-flight work finish before the
    // objects it uses (notably the whisper context) are destroyed.
    if (m_hotkeys) m_hotkeys->stop();
    if (m_recorder && m_recorder->isRecording()) m_recorder->stop();
    if (m_transcriber) m_transcriber->abort();
    for (const QPointer<QThread> &t : std::as_const(m_workers))
        if (t) t->wait();

    Tones::stop();
    delete m_setupDialog;
}

bool App::readAutostartSetting() const
{
    return QSettings(QString::fromLatin1(kRunKey), QSettings::NativeFormat)
        .contains(QStringLiteral("WhisperVTT"));
}

void App::writeAutostartSetting(bool v)
{
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    // Quoted, as the path may contain spaces.
    if (v) run.setValue(QStringLiteral("WhisperVTT"), QStringLiteral("\"%1\"").arg(
                            QDir::toNativeSeparators(qApp->applicationFilePath())));
    else   run.remove(QStringLiteral("WhisperVTT"));
}

// ── init ─────────────────────────────────────────────────────────────

void App::init()
{
    const QString dataDir = qEnvironmentVariable("APPDATA") + QStringLiteral("/WhisperVTT");
    QDir().mkpath(dataDir);

    // Portable layout: a "models" folder next to the exe wins over the
    // per-user one, so the whole app can be copied to another machine.
    const QString portableModels = qApp->applicationDirPath() + QStringLiteral("/models");
    m_modelsDir = QFileInfo(portableModels).isDir() ? portableModels
                                                    : dataDir + QStringLiteral("/models");
    QDir().mkpath(m_modelsDir);
    const QString modelFile = ModelCatalog::choose(
        QDir(m_modelsDir).entryList({QStringLiteral("*.bin")}, QDir::Files), configuredModel());
    m_modelPath = m_modelsDir + QLatin1Char('/') + modelFile;

    m_gpuAvailable = Transcriber::gpuAvailable();
    m_gpuEnabled   = QSettings().value(kGpu, true).toBool();

    m_recorder    = new AudioRecorder(this);
    m_transcriber = new Transcriber(m_modelPath, this);
    m_transcriber->setVadModelPath(QStringLiteral(":/vad/ggml-silero-v6.2.0.bin"));
    m_hotkeys     = new HotkeyManager(this);
    m_editor      = std::make_unique<EditorWindow>(dataDir);

    qApp->setWindowIcon(m_iconIdle);
    m_editor->setWindowIcon(m_iconIdle);
    m_editor->setGpuState(m_gpuAvailable, m_gpuEnabled);
    m_editor->setAutostartChecked(readAutostartSetting());
    m_editor->setModelsDir(m_modelsDir);
    m_editor->setCurrentModel(modelFile);

    // ── tray ─────────────────────────────────────────────────────────
    m_tray = new QSystemTrayIcon(m_iconIdle, this);
    m_tray->setToolTip(QStringLiteral("WhisperVTT — hold F9 to dictate"));

    m_trayMenu = std::make_unique<QMenu>();
    connect(m_trayMenu->addAction(QStringLiteral("Show Editor")), &QAction::triggered,
            this, &App::showEditor);
    m_trayMenu->addSeparator();
    m_trayGpuAct = m_trayMenu->addAction(QStringLiteral("Enable GPU"));
    m_trayGpuAct->setCheckable(true);
    m_trayGpuAct->setChecked(m_gpuAvailable && m_gpuEnabled);
    m_trayGpuAct->setEnabled(m_gpuAvailable);
    connect(m_trayGpuAct, &QAction::toggled, this, &App::onGpuToggled);
    m_trayMenu->addSeparator();
    connect(m_trayMenu->addAction(QStringLiteral("Quit")), &QAction::triggered,
            qApp, &QApplication::quit);

    m_tray->setContextMenu(m_trayMenu.get());
    connect(m_tray, &QSystemTrayIcon::activated, this, &App::onTrayActivated);
    m_tray->show();

    // ── signals ──────────────────────────────────────────────────────
    connect(m_hotkeys, &HotkeyManager::tapped,       this, [this]{ onInput(Input::Tap); });
    connect(m_hotkeys, &HotkeyManager::holdStarted,  this, [this]{ onInput(Input::HoldStart); });
    connect(m_hotkeys, &HotkeyManager::holdReleased, this, [this]{ onInput(Input::HoldRelease); });
    connect(m_editor.get(), &EditorWindow::recordRequested,  this, [this]{ onInput(Input::Tap); });
    connect(m_editor.get(), &EditorWindow::stopRequested,    this, [this]{ onInput(Input::Stop); });
    connect(m_editor.get(), &EditorWindow::gpuToggled,       this, &App::onGpuToggled);
    connect(m_editor.get(), &EditorWindow::autostartToggled, this, &App::onAutostartToggled);
    connect(m_editor.get(), &EditorWindow::modelSelected,    this, &App::onModelSelected);
    // Emitted from worker threads — queued onto the GUI thread.
    connect(m_transcriber, &Transcriber::statusChanged,
            this, [this](const QString &msg){ status(msg); });

    // ── model ────────────────────────────────────────────────────────
    m_modelsWatcher.addPath(m_modelsDir);
    connect(&m_modelsWatcher, &QFileSystemWatcher::directoryChanged,
            &m_modelsSettle, qOverload<>(&QTimer::start));

    if (QFileInfo::exists(m_modelPath))
        loadModelAsync();
    else
        showModelSetup();

    if (!m_hotkeys->start()) {
        status(QStringLiteral("Could not install the F9 keyboard hook"));
        m_tray->showMessage(QStringLiteral("WhisperVTT"),
            QStringLiteral("F9 hotkey unavailable — use the Record button."),
            QSystemTrayIcon::Warning, 8000);
    }
}

// ── input ────────────────────────────────────────────────────────────

void App::onInput(Input input)
{
    switch (RecordingFlow::decide(m_state, input, m_pendingRecordKind)) {
    case Action::StartBuffer:    ensureModelAndRecord(State::RecordingBuffer); break;
    case Action::StartLive:      ensureModelAndRecord(State::RecordingLive);   break;
    case Action::FinishToCursor: stopAndTranscribe(Dest::Paste);               break;
    case Action::FinishToTab:
        stopAndTranscribe(m_recordToLog ? Dest::Log : Dest::Editor);
        break;
    case Action::CancelPendingLive:
        m_pendingRecordKind = State::Idle;
        Tones::stop();
        status(QStringLiteral("Released before model was ready — hold F9 again"));
        break;
    case Action::ReportLoading:
        status(QStringLiteral("Still loading model..."));
        break;
    case Action::ReportTranscribing:
        status(QStringLiteral("Transcription in progress..."));
        break;
    case Action::None:
        break;
    }
}

// ── worker threads ───────────────────────────────────────────────────

void App::runWorker(std::function<void()> fn)
{
    // Drop entries for threads that have finished and been deleted.
    m_workers.removeIf([](const QPointer<QThread> &t){ return t.isNull(); });

    QThread *thread = QThread::create(std::move(fn));
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    m_workers.append(thread);
    thread->start();
}

// ── model loading ────────────────────────────────────────────────────

void App::loadModelAsync()
{
    setState(State::Loading);
    m_gpuIdleTimer.stop();

    const bool gpu = m_gpuAvailable && m_gpuEnabled;
    runWorker([this, gpu]{
        m_transcriber->loadModel(gpu);
        QMetaObject::invokeMethod(this, &App::onModelLoaded, Qt::QueuedConnection);
    });
}

void App::onModelLoaded()
{
    Tones::stop();
    const State pending = m_pendingRecordKind;
    m_pendingRecordKind = State::Idle;
    setState(State::Idle);

    m_modelReady = m_transcriber->isModelLoaded();
    if (!m_modelReady) {
        // Transcriber already reported why.  Retry once the file changes
        // (e.g. a copy that hadn't finished).
        const QFileInfo info(m_modelPath);
        m_failedStamp = {info.size(), info.lastModified()};
        m_modelsSettle.start();
        return;
    }

    if (pending != State::Idle) {
        ensureModelAndRecord(pending);
    } else {
        status(m_transcriber->isGpuActive()
            ? QStringLiteral("Ready (GPU) — press F9")
            : QStringLiteral("Ready (CPU) — press F9"));
        resetGpuIdleTimer();
    }
}

void App::showModelSetup()
{
    m_modelReady = false;   // watch the models folder again
    status(QStringLiteral("No speech model installed — see the setup window"));
    if (m_setupDialog) {
        m_setupDialog->raise();
        m_setupDialog->activateWindow();
        return;
    }
    m_setupDialog = ModelSetup::show(m_modelsDir,
                                     m_gpuEnabled && Transcriber::hasDedicatedGpu());
}

// Runs while no model has loaded yet: when the models folder changes, and
// every 2 s while a candidate file is still arriving.  A model is loaded
// only once it looks like a real model file and its size and timestamp
// have held steady between two checks, so a download or copy in progress
// is never read.
void App::checkForNewModel()
{
    if (m_modelReady) return;
    if (m_state != State::Idle) {
        m_modelsSettle.start();
        return;
    }

    const QString file = ModelCatalog::choose(
        QDir(m_modelsDir).entryList({QStringLiteral("*.bin")}, QDir::Files), configuredModel());
    const QFileInfo info(m_modelsDir + QLatin1Char('/') + file);
    if (!info.exists()) {
        m_settleStamp = {};
        return;   // wait for the folder to change
    }

    QFile f(info.filePath());
    const bool looksValid = f.open(QIODevice::ReadOnly) && ModelCatalog::hasModelHeader(f.read(4));
    const FileStamp now{info.size(), info.lastModified()};
    if (!looksValid || !(now == m_settleStamp) || now == m_failedStamp) {
        m_settleStamp = now;
        m_modelsSettle.start();
        return;
    }

    m_settleStamp = {};
    if (m_setupDialog) m_setupDialog->close();
    m_modelPath = info.filePath();
    m_transcriber->setModelPath(m_modelPath);
    m_editor->setCurrentModel(file);
    status(QStringLiteral("Found %1 — loading...").arg(file));
    loadModelAsync();
}

void App::resetGpuIdleTimer()
{
    if (m_transcriber->isGpuActive())
        m_gpuIdleTimer.start();
    else
        m_gpuIdleTimer.stop();
}

void App::onGpuIdleTimeout()
{
    if (m_state != State::Idle) return;   // busy — skip
    runWorker([this]{ m_transcriber->unloadModel(); });
    status(QStringLiteral("GPU released (idle %1 s) — will reload on next F9")
               .arg(GPU_IDLE_MS / 1000));
}

void App::ensureModelAndRecord(State kind)
{
    m_gpuIdleTimer.stop();

    if (!m_transcriber->isModelLoaded() && !QFileInfo::exists(m_modelPath)) {
        showModelSetup();
        return;
    }

    if (m_transcriber->isModelLoaded()) {
        if (!startRecording(kind)) {
            status(QStringLiteral("No microphone available — check Windows sound input settings"));
            resetGpuIdleTimer();
            return;
        }
        Tones::playReady();
        status(kind == State::RecordingBuffer
            ? QStringLiteral("Recording... (tap F9 or click Stop)")
            : QStringLiteral("Live recording... (release F9 to type)"));
    } else {
        Tones::playLoading();
        m_pendingRecordKind = kind;
        status(QStringLiteral("Loading model..."));
        loadModelAsync();
    }
}

// ── settings slots ───────────────────────────────────────────────────

void App::onGpuToggled(bool enabled)
{
    // Reloading the model while recording would kick the state machine
    // into Loading with the mic still open, so only allow it when idle.
    if (m_state != State::Idle) {
        status(QStringLiteral("Cannot change GPU while busy"));
        enabled = m_gpuEnabled;
    } else if (enabled != m_gpuEnabled) {
        m_gpuEnabled = enabled;
        QSettings().setValue(kGpu, enabled);
        if (QFileInfo::exists(m_modelPath))
            loadModelAsync();
    }
    { QSignalBlocker b(m_trayGpuAct); m_trayGpuAct->setChecked(enabled); }
    m_editor->setGpuState(m_gpuAvailable, enabled);
}

void App::onModelSelected(const QString &fileName)
{
    const QString path = m_modelsDir + QLatin1Char('/') + fileName;
    if (path == m_modelPath) return;
    if (m_state != State::Idle) {
        status(QStringLiteral("Cannot change model while busy"));
        return;
    }
    QSettings().setValue(kModel, fileName);
    m_editor->setCurrentModel(fileName);
    m_modelPath = path;
    m_transcriber->setModelPath(path);
    loadModelAsync();
}

void App::onAutostartToggled(bool enabled)
{
    writeAutostartSetting(enabled);
    m_editor->setAutostartChecked(enabled);
    status(enabled ? QStringLiteral("Will start with Windows")
                   : QStringLiteral("Will no longer start with Windows"));
}

void App::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick)
        showEditor();
}

void App::showEditor()
{
    m_editor->show(); m_editor->raise(); m_editor->activateWindow();
}

// ── recording ────────────────────────────────────────────────────────

bool App::startRecording(State kind)
{
    if (kind == State::RecordingLive)
        m_prevForeground = GetForegroundWindow();
    if (!m_recorder->start())
        return false;
    // Timestamp the moment the mic actually opened.
    m_recordStart = QDateTime::currentDateTime();
    // The tab that's active when recording starts decides where a buffer
    // recording goes: Log tab -> log, Edit tab -> editor.
    m_recordToLog = m_editor->isLogTabActive();
    m_recordLimitTimer.start();
    setState(kind);
    if (kind == State::RecordingBuffer)
        m_editor->setRecording(true, m_recordToLog);
    return true;
}

void App::onRecordLimit()
{
    const bool live = m_state == State::RecordingLive;
    if (!live && m_state != State::RecordingBuffer) return;
    m_tray->showMessage(QStringLiteral("WhisperVTT"),
        QStringLiteral("Recording stopped at the %1-minute limit.").arg(RECORD_LIMIT_MS / 60000),
        QSystemTrayIcon::Information, 5000);
    stopAndTranscribe(live ? Dest::Paste : m_recordToLog ? Dest::Log : Dest::Editor);
}

void App::stopAndTranscribe(Dest dest)
{
    m_recordLimitTimer.stop();
    PcmCapture cap = m_recorder->stop();
    const QDateTime recordEnd = QDateTime::currentDateTime();
    m_editor->setRecording(false);

    if (cap.seconds() < 0.3) {
        setState(State::Idle);
        status(QStringLiteral("Too short — speak longer and try again"));
        resetGpuIdleTimer();
        return;
    }

    setState(State::Transcribing);
    status(QStringLiteral("Transcribing..."));

    LogEntry entry;
    entry.start    = m_recordStart;
    entry.end      = recordEnd;
    entry.duration = cap.seconds();

    runWorker([this, cap = std::move(cap), dest, entry]{
        float rawPeak = 0.0f;
        const std::vector<float> audio = AudioConvert::toWhisperInput(cap, &rawPeak);
        const bool silent = rawPeak < kSilentPeak;
        const QString text = silent ? QString() : m_transcriber->transcribe(audio);
        QMetaObject::invokeMethod(this, [this, text, dest, silent, entry]() mutable {
            entry.text = text.trimmed();
            finishTranscription(entry, dest, silent);
        }, Qt::QueuedConnection);
    });
}

void App::finishTranscription(const LogEntry &entry, Dest dest, bool silent)
{
    auto done = [this]{
        setState(State::Idle);
        resetGpuIdleTimer();
    };

    if (entry.text.isEmpty()) {
        status(silent ? QStringLiteral("Microphone is silent — check it isn't muted")
                      : QStringLiteral("No speech detected"));
        done();
        return;
    }

    switch (dest) {
    case Dest::Log:
        m_editor->logPanel()->append(entry);
        status(QStringLiteral("Added to log — press F9 for the next entry"));
        done();
        break;
    case Dest::Editor:
        m_editor->appendText(entry.text);
        status(QStringLiteral("Done — press F9 or click Record"));
        done();
        break;
    case Dest::Paste:
        // Stay in Transcribing until the paste completes, so another F9
        // can't start a recording while we're driving the clipboard.
        // Trailing space so consecutive dictations don't run together.
        LiveTyper::typeText(entry.text + QLatin1Char(' '), m_prevForeground, this,
                            [this, done](bool ok){
            status(ok ? QStringLiteral("Typed — hold F9 for more")
                      : QStringLiteral("Couldn't access the clipboard — text not pasted"));
            done();
        });
        break;
    }
}

void App::setState(State s)
{
    m_state = s;
    if (!m_tray) return;
    switch (m_state) {
    case State::RecordingBuffer:
    case State::RecordingLive: m_tray->setIcon(m_iconRecording); break;
    case State::Transcribing:
    case State::Loading:       m_tray->setIcon(m_iconWorking);   break;
    case State::Idle:          m_tray->setIcon(m_iconIdle);      break;
    }
}

void App::status(const QString &msg)
{
    if (m_editor) m_editor->setStatus(msg);
}
