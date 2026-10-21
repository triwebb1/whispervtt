#pragma once

#include "recordingflow.h"
#include "transcriptlog.h"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QIcon>
#include <QList>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <functional>
#include <memory>
#include <windows.h>

class AudioRecorder;
class EditorWindow;
class HotkeyManager;
class Transcriber;

// Owns every component and carries out the recording flow: hotkey and
// button input go through RecordingFlow::decide(), and App performs the
// resulting action (open the mic, load the model, transcribe on a worker
// thread, deliver the text).  Also owns the tray icon and settings.
class App : public QObject {
    Q_OBJECT
public:
    explicit App(QObject *parent = nullptr);
    ~App() override;
    void init();

private:
    using State = RecordingFlow::State;
    enum class Dest { Editor, Log, Paste };   // where a transcription goes

    void onInput(RecordingFlow::Input input);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onGpuToggled(bool enabled);
    void onAutostartToggled(bool enabled);
    void onGpuIdleTimeout();
    void onModelSelected(const QString &fileName);

    void setState(State s);
    bool startRecording(State kind);
    void stopAndTranscribe(Dest dest);
    void finishTranscription(const LogEntry &entry, Dest dest, bool silent);
    void status(const QString &msg);
    void showEditor();

    // Starts a |kind| recording, loading the model first (with the
    // loading tone) if it isn't in memory.
    void ensureModelAndRecord(State kind);
    void loadModelAsync();
    void onModelLoaded();
    void resetGpuIdleTimer();

    // First-run help when no model is installed, and pick-up of a model
    // that arrives in the models folder while the app is running.
    void showModelSetup();
    void checkForNewModel();
    void onRecordLimit();

    // Runs |fn| on a new thread.  Tracked so shutdown can wait for it.
    void runWorker(std::function<void()> fn);

    bool readAutostartSetting() const;
    void writeAutostartSetting(bool v);

    State          m_state = State::Idle;
    State          m_pendingRecordKind = State::Idle;   // queued while the model loads
    AudioRecorder *m_recorder    = nullptr;
    Transcriber   *m_transcriber = nullptr;
    HotkeyManager *m_hotkeys     = nullptr;
    std::unique_ptr<EditorWindow> m_editor;

    QSystemTrayIcon      *m_tray = nullptr;
    std::unique_ptr<QMenu> m_trayMenu;
    QAction              *m_trayGpuAct = nullptr;
    QIcon                 m_iconIdle, m_iconRecording, m_iconWorking;

    QString m_modelsDir;
    QString m_modelPath;
    bool    m_gpuAvailable = false;   // a GPU ggml can use exists
    bool    m_gpuEnabled   = true;    // the user's setting

    // A model has loaded successfully.  Stays true through an intentional
    // idle unload; until then the models folder is watched for a new or
    // changed model file.
    bool m_modelReady = false;

    struct FileStamp {
        qint64    size = -1;
        QDateTime modified;
        bool operator==(const FileStamp &o) const { return size == o.size && modified == o.modified; }
    };
    QFileSystemWatcher m_modelsWatcher;
    QTimer             m_modelsSettle;   // re-checks the folder every 2 s while waiting
    FileStamp          m_settleStamp;    // candidate model at the last check
    FileStamp          m_failedStamp;    // model file that last failed to load
    QPointer<QWidget>  m_setupDialog;

    QList<QPointer<QThread>> m_workers;
    QDateTime m_recordStart;   // when the mic opened for the current recording
    bool      m_recordToLog = false;
    HWND      m_prevForeground = nullptr;

    QTimer m_gpuIdleTimer;
    QTimer m_recordLimitTimer;
    static constexpr int GPU_IDLE_MS     = 60 * 1000;
    static constexpr int RECORD_LIMIT_MS = 10 * 60 * 1000;
};
