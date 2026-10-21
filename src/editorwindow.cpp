#include "editorwindow.h"
#include "formatter.h"
#include "logpanel.h"
#include "tones.h"

#include <QApplication>
#include <QActionGroup>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QCloseEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMimeData>
#include <QSlider>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

// ── Copy Rich stylesheet ─────────────────────────────────────────────

static const char *kPreviewCss =
    "body   { font-family: 'Segoe UI', sans-serif; font-size: 11pt;"
    "         color: #1a1a1a; }"
    "h1     { font-size: 18pt; margin-top: 14px; margin-bottom: 6px; }"
    "h2     { font-size: 15pt; margin-top: 12px; margin-bottom: 5px; }"
    "h3     { font-size: 13pt; margin-top: 10px; margin-bottom: 4px; }"
    "ul, ol { margin-left: 24px; }"
    "li     { margin-bottom: 3px; }"
    "p      { margin-top: 6px; margin-bottom: 6px; line-height: 1.45; }";

// ── construction ─────────────────────────────────────────────────────

EditorWindow::EditorWindow(const QString &dataDir, QWidget *parent)
    : QMainWindow(parent)
{
    m_log = new LogPanel(dataDir);

    m_blinkTimer.setInterval(500);
    connect(&m_blinkTimer, &QTimer::timeout, this, &EditorWindow::onBlink);

    m_tickTimer.setInterval(1000);
    connect(&m_tickTimer, &QTimer::timeout, this, &EditorWindow::onTick);

    buildUi();
    buildMenuBar();
}

// ── UI ───────────────────────────────────────────────────────────────

void EditorWindow::buildUi()
{
    setWindowTitle(QStringLiteral("WhisperVTT"));
    setMinimumSize(660, 480);

    auto *central = new QWidget;
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(10, 10, 10, 4);
    root->setSpacing(6);

    // ── control bar (ALWAYS visible) ─────────────────────────────────
    m_controlBar = new QFrame;
    m_controlBar->setObjectName(QStringLiteral("controlBar"));
    // Idle: transparent — inherits the system/dark-mode palette.
    m_controlBar->setStyleSheet(QString());

    auto *barLay = new QHBoxLayout(m_controlBar);
    barLay->setContentsMargins(14, 8, 14, 8);
    barLay->setSpacing(10);

    // -- Record button (visible when idle) --
    m_btnRecord = new QPushButton(QStringLiteral("\u25cf  Record"));  // ● Record
    m_btnRecord->setCursor(Qt::PointingHandCursor);
    m_btnRecord->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1976D2; color: white; border: none;"
        " border-radius: 4px; padding: 6px 22px; font-weight: bold;"
        " font-size: 13px; }"
        "QPushButton:hover { background: #1565C0; }"));
    connect(m_btnRecord, &QPushButton::clicked,
            this, &EditorWindow::recordRequested);
    barLay->addWidget(m_btnRecord);

    // -- Recording indicator widgets (visible when recording) --
    m_recDot = new QLabel(QStringLiteral("\u25cf")); // ●
    m_recDot->setStyleSheet(
        QStringLiteral("color: white; font-size: 20px;"));
    m_recDot->setVisible(false);
    barLay->addWidget(m_recDot);

    m_recLabel = new QLabel(QStringLiteral("Recording"));
    m_recLabel->setStyleSheet(
        QStringLiteral("color: white; font-weight: bold; font-size: 13px;"));
    m_recLabel->setVisible(false);
    barLay->addWidget(m_recLabel);

    m_recTime = new QLabel(QStringLiteral("00:00"));
    m_recTime->setStyleSheet(
        QStringLiteral("color: rgba(255,255,255,0.8); font-size: 13px;"
                       " font-family: Consolas, monospace;"));
    m_recTime->setVisible(false);
    barLay->addWidget(m_recTime);

    barLay->addStretch();

    m_btnStop = new QPushButton(QStringLiteral("\u23f9  Stop Recording"));
    m_btnStop->setCursor(Qt::PointingHandCursor);
    m_btnStop->setStyleSheet(QStringLiteral(
        "QPushButton { background: white; color: #D32F2F; border: none;"
        " border-radius: 4px; padding: 5px 18px; font-weight: bold; }"
        "QPushButton:hover { background: #FFCDD2; }"));
    connect(m_btnStop, &QPushButton::clicked,
            this, &EditorWindow::stopRequested);
    m_btnStop->setVisible(false);
    barLay->addWidget(m_btnStop);

    root->addWidget(m_controlBar);

    // ── hint ─────────────────────────────────────────────────────────
    auto *hint = new QLabel(
        QStringLiteral("F9 tap = record into buffer  \u2502"
                       "  F9 hold = live push-to-talk"));
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(
        QStringLiteral("color: #888; font-size: 11px;"));
    root->addWidget(hint);

    // ── tabs ─────────────────────────────────────────────────────────
    m_tabs = new QTabWidget;
    root->addWidget(m_tabs, /*stretch=*/1);

    m_edit = new QTextEdit;
    m_edit->setFont(QFont(QStringLiteral("Consolas"), 11));
    m_edit->setAcceptRichText(false);
    m_edit->setPlaceholderText(
        QStringLiteral("Transcribed text will appear here...\n\n"
                       "Tip: while dictating you can say voice commands "
                       "like\n\"new paragraph\", \"heading\", \"bullet "
                       "point\" and then\nclick Format to apply Markdown "
                       "structure."));
    m_tabs->addTab(m_edit, QStringLiteral("Edit"));

    m_tabs->addTab(m_log, QStringLiteral("Log"));
    connect(m_log, &LogPanel::statusMessage, this,
            [this](const QString &msg, int ms){ m_status->showMessage(msg, ms); });

    // ── buttons ──────────────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    m_btnFormat = new QPushButton(QStringLiteral("Format"));
    m_btnFormat->setToolTip(
        QStringLiteral("Apply Markdown formatting "
                       "(headings, lists, paragraphs)"));
    connect(m_btnFormat, &QPushButton::clicked,
            this, &EditorWindow::onFormat);
    btnRow->addWidget(m_btnFormat);

    m_btnCopyText = new QPushButton(QStringLiteral("Copy Text"));
    m_btnCopyText->setToolTip(
        QStringLiteral("Copy plain Markdown text to clipboard"));
    connect(m_btnCopyText, &QPushButton::clicked,
            this, &EditorWindow::onCopyText);
    btnRow->addWidget(m_btnCopyText);

    m_btnCopyRich = new QPushButton(QStringLiteral("Copy Rich"));
    m_btnCopyRich->setToolTip(
        QStringLiteral("Copy as rich text for Word, Docs, email..."));
    connect(m_btnCopyRich, &QPushButton::clicked,
            this, &EditorWindow::onCopyRich);
    btnRow->addWidget(m_btnCopyRich);

    m_btnClear = new QPushButton(QStringLiteral("Clear"));
    connect(m_btnClear, &QPushButton::clicked,
            this, &EditorWindow::onClear);
    btnRow->addWidget(m_btnClear);

    btnRow->addStretch();
    root->addLayout(btnRow);

    // The Format / Copy / Clear buttons act on the Edit tab only; the Log
    // tab has its own.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this]{
        const bool editTab = !isLogTabActive();
        for (QPushButton *b : {m_btnFormat, m_btnCopyText, m_btnCopyRich, m_btnClear})
            b->setVisible(editTab);
    });

    // ── status bar ───────────────────────────────────────────────────
    m_status = new QStatusBar;
    setStatusBar(m_status);
    m_status->showMessage(QStringLiteral("Ready \u2014 press F9 or click Record"));
}

// ── menu bar ─────────────────────────────────────────────────────────

void EditorWindow::buildMenuBar()
{
    auto *mb = menuBar();
    auto *settingsMenu = mb->addMenu(QStringLiteral("Settings"));

    m_actGpu = settingsMenu->addAction(QStringLiteral("Enable GPU"));
    m_actGpu->setCheckable(true);
    m_actGpu->setChecked(true);
    connect(m_actGpu, &QAction::toggled, this, &EditorWindow::gpuToggled);

    m_actAutostart = settingsMenu->addAction(QStringLiteral("Start with Windows"));
    m_actAutostart->setCheckable(true);
    m_actAutostart->setChecked(false);
    connect(m_actAutostart, &QAction::toggled, this, &EditorWindow::autostartToggled);

    m_modelMenu = settingsMenu->addMenu(QStringLiteral("Model"));
    connect(m_modelMenu, &QMenu::aboutToShow, this, &EditorWindow::populateModelMenu);

    settingsMenu->addSeparator();

    // Volume slider embedded in the menu
    auto *volWidget = new QWidget;
    auto *volLay = new QHBoxLayout(volWidget);
    volLay->setContentsMargins(16, 4, 16, 4);
    auto *volLabel = new QLabel(QStringLiteral("Sound Volume"));
    volLay->addWidget(volLabel);
    auto *volSlider = new QSlider(Qt::Horizontal);
    volSlider->setRange(0, 100);
    volSlider->setValue(Tones::volume());
    volSlider->setFixedWidth(120);
    connect(volSlider, &QSlider::valueChanged, this, [](int v){
        Tones::setVolume(v);
    });
    volLay->addWidget(volSlider);
    auto *volAction = new QWidgetAction(settingsMenu);
    volAction->setDefaultWidget(volWidget);
    settingsMenu->addAction(volAction);
}

void EditorWindow::populateModelMenu()
{
    m_modelMenu->clear();
    qDeleteAll(m_modelMenu->findChildren<QActionGroup *>(Qt::FindDirectChildrenOnly));
    auto *group = new QActionGroup(m_modelMenu);
    group->setExclusive(true);

    const QStringList files = QDir(m_modelsDir).entryList(
        {QStringLiteral("*.bin")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        QAction *a = m_modelMenu->addAction(f);
        a->setCheckable(true);
        a->setChecked(f.compare(m_currentModel, Qt::CaseInsensitive) == 0);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, f]{ emit modelSelected(f); });
    }
    if (files.isEmpty())
        m_modelMenu->addAction(QStringLiteral("(no .bin models in folder)"))->setEnabled(false);

    m_modelMenu->addSeparator();
    connect(m_modelMenu->addAction(QStringLiteral("Open Models Folder")),
            &QAction::triggered, this, [this]{
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_modelsDir));
    });
}

void EditorWindow::setGpuState(bool available, bool checked)
{
    QSignalBlocker block(m_actGpu);
    m_actGpu->setEnabled(available);
    m_actGpu->setChecked(available && checked);
    m_actGpu->setText(available ? QStringLiteral("Enable GPU")
                                : QStringLiteral("Enable GPU (no compatible GPU found)"));
}

void EditorWindow::setAutostartChecked(bool on)
{
    QSignalBlocker block(m_actAutostart);
    m_actAutostart->setChecked(on);
}

// ── recording UI state ───────────────────────────────────────────────

bool EditorWindow::isLogTabActive() const
{
    return m_tabs->currentWidget() == m_log;
}

void EditorWindow::setRecording(bool on, bool toLog)
{
    m_isRecording = on;

    if (on) {
        m_recLabel->setText(toLog ? QStringLiteral("Recording \u2192 Log")
                                  : QStringLiteral("Recording"));
        // Switch to recording appearance
        m_controlBar->setStyleSheet(
            QStringLiteral("#controlBar { background-color: #D32F2F;"
                           " border-radius: 6px; }"));
        m_btnRecord->setVisible(false);
        m_recDot->setVisible(true);
        m_recLabel->setVisible(true);
        m_recTime->setVisible(true);
        m_btnStop->setVisible(true);

        m_recSec = 0;
        m_recTime->setText(QStringLiteral("00:00"));
        m_blinkOn = true;
        m_recDot->setStyleSheet(
            QStringLiteral("color: white; font-size: 20px;"));
        m_blinkTimer.start();
        m_tickTimer.start();

        setWindowTitle(QStringLiteral("WhisperVTT \u2014 Recording"));
        show();
        raise();
        activateWindow();
    } else {
        // Switch back to idle appearance
        m_blinkTimer.stop();
        m_tickTimer.stop();

        m_controlBar->setStyleSheet(QString());
        m_btnRecord->setVisible(true);
        m_recDot->setVisible(false);
        m_recLabel->setVisible(false);
        m_recTime->setVisible(false);
        m_btnStop->setVisible(false);

        setWindowTitle(QStringLiteral("WhisperVTT"));
    }
}

void EditorWindow::onBlink()
{
    m_blinkOn = !m_blinkOn;
    m_recDot->setStyleSheet(
        m_blinkOn ? QStringLiteral("color: white; font-size: 20px;")
                  : QStringLiteral("color: transparent; font-size: 20px;"));
}

void EditorWindow::onTick()
{
    ++m_recSec;
    int m = m_recSec / 60, s = m_recSec % 60;
    m_recTime->setText(QStringLiteral("%1:%2")
                           .arg(m, 2, 10, QLatin1Char('0'))
                           .arg(s, 2, 10, QLatin1Char('0')));
}

// ── text slots ───────────────────────────────────────────────────────

void EditorWindow::appendText(const QString &text)
{
    m_tabs->setCurrentIndex(0);
    const QString cur = m_edit->toPlainText();
    const QString sep = cur.isEmpty()                    ? QString()
                      : cur.endsWith(QLatin1Char('\n')) ? QStringLiteral("\n")
                                                         : QStringLiteral("\n\n");
    // Insert via a cursor rather than setPlainText() so the user's undo
    // history survives each new transcription.
    QTextCursor tc(m_edit->document());
    tc.movePosition(QTextCursor::End);
    tc.insertText(sep + text);
    m_edit->setTextCursor(tc);
}

void EditorWindow::setStatus(const QString &msg)
{
    m_status->showMessage(msg);
}

// ── button handlers ──────────────────────────────────────────────────

// Replaces the whole text as one edit, so Ctrl+Z undoes it (setPlainText
// and clear() would wipe the undo history instead).
static void replaceAll(QTextEdit *edit, const QString &text)
{
    QTextCursor c(edit->document());
    c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(text);
    c.endEditBlock();
}

void EditorWindow::onFormat()
{
    const QString text = m_edit->toPlainText();
    if (!text.trimmed().isEmpty()) {
        replaceAll(m_edit, Formatter::format(text));
        m_status->showMessage(QStringLiteral("Formatting applied (Ctrl+Z to undo)"), 3000);
    }
}

void EditorWindow::onCopyText()
{
    QString text = m_edit->toPlainText();
    if (text.trimmed().isEmpty()) return;
    QApplication::clipboard()->setText(text);
    m_status->showMessage(
        QStringLiteral("Copied as plain text"), 3000);
}

void EditorWindow::onCopyRich()
{
    QString text = m_edit->toPlainText();
    if (text.trimmed().isEmpty()) return;

    QTextDocument doc;
    doc.setDefaultStyleSheet(QString::fromLatin1(kPreviewCss));
    doc.setMarkdown(text);

    auto *mime = new QMimeData;
    mime->setHtml(doc.toHtml());
    mime->setText(text);
    QApplication::clipboard()->setMimeData(mime);
    m_status->showMessage(
        QStringLiteral("Copied as formatted text"), 3000);
}

void EditorWindow::onClear()
{
    if (m_edit->document()->isEmpty()) return;
    replaceAll(m_edit, QString());
    m_status->showMessage(QStringLiteral("Cleared (Ctrl+Z to undo)"), 3000);
}

// ── window behaviour ─────────────────────────────────────────────────

void EditorWindow::closeEvent(QCloseEvent *e)
{
    e->ignore();
    hide();
}
