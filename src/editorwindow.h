#pragma once

#include <QAction>
#include <QFrame>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>

class LogPanel;

class EditorWindow : public QMainWindow {
    Q_OBJECT
public:
    // |dataDir| is where the transcript log is stored.
    explicit EditorWindow(const QString &dataDir, QWidget *parent = nullptr);

    void appendText(const QString &text);
    void setRecording(bool on, bool toLog = false);
    void setStatus(const QString &msg);

    // True when the Log tab is the one showing.
    bool isLogTabActive() const;
    LogPanel *logPanel() const { return m_log; }

    // Call after settings are loaded to sync the checkmarks.
    // "Enable GPU" is disabled when no usable GPU is found.
    void setGpuState(bool available, bool checked);
    void setAutostartChecked(bool on);

    // Settings > Model lists the *.bin files in |dir|.
    void setModelsDir(const QString &dir) { m_modelsDir = dir; }
    void setCurrentModel(const QString &fileName) { m_currentModel = fileName; }

signals:
    void stopRequested();
    void recordRequested();
    void gpuToggled(bool enabled);
    void autostartToggled(bool enabled);
    void modelSelected(const QString &fileName);

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onFormat();
    void onCopyText();
    void onCopyRich();
    void onClear();
    void onBlink();
    void onTick();

private:
    void buildUi();
    void buildMenuBar();
    void populateModelMenu();

    // Menu actions (shared with tray)
    QAction *m_actGpu       = nullptr;
    QAction *m_actAutostart = nullptr;
    QMenu   *m_modelMenu    = nullptr;
    QString  m_modelsDir;
    QString  m_currentModel;

    // Control bar
    QFrame      *m_controlBar  = nullptr;
    QPushButton *m_btnRecord   = nullptr;
    QLabel      *m_recDot      = nullptr;
    QLabel      *m_recLabel    = nullptr;
    QLabel      *m_recTime     = nullptr;
    QPushButton *m_btnStop     = nullptr;
    QTimer       m_blinkTimer;
    QTimer       m_tickTimer;
    int          m_recSec      = 0;
    bool         m_blinkOn     = true;
    bool         m_isRecording = false;

    // Tabs
    QTabWidget   *m_tabs      = nullptr;
    QTextEdit    *m_edit      = nullptr;
    LogPanel     *m_log       = nullptr;

    // Buttons
    QPushButton *m_btnFormat   = nullptr;
    QPushButton *m_btnCopyText = nullptr;
    QPushButton *m_btnCopyRich = nullptr;
    QPushButton *m_btnClear    = nullptr;

    QStatusBar *m_status = nullptr;
};
