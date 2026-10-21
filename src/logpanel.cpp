#include "logpanel.h"
#include "debuglog.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

#define LOG(msg) DLOG("log", msg)

enum Col { ColStart, ColDuration, ColText, ColCount };

using TranscriptLog::isoStamp;

static QString displayStamp(const QDateTime &dt)
{
    return dt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

static QString entries(qsizetype n)
{
    return n == 1 ? QStringLiteral("1 entry") : QStringLiteral("%1 entries").arg(n);
}

// ── multi-line cell editor ───────────────────────────────────────────

namespace {
class MultiLineDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *ed = new QPlainTextEdit(parent);
        ed->setTabChangesFocus(true);
        return ed;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        static_cast<QPlainTextEdit *>(editor)->setPlainText(
            index.data(Qt::EditRole).toString());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        model->setData(index, static_cast<QPlainTextEdit *>(editor)->toPlainText(),
                       Qt::EditRole);
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &opt,
                              const QModelIndex &) const override
    {
        QRect r = opt.rect;
        r.setHeight(std::max(r.height(), 90));
        editor->setGeometry(r);
    }
};
}

// ── construction ─────────────────────────────────────────────────────

LogPanel::LogPanel(const QString &dataDir, QWidget *parent)
    : QWidget(parent),
      m_path(dataDir + QStringLiteral("/transcript_log.json"))
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 4, 0, 0);
    root->setSpacing(6);

    auto *hint = new QLabel(QStringLiteral(
        "While this tab is active, F9 recordings are added here with timestamps. "
        "Double-click text to edit."));
    hint->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Started"),
                                        QStringLiteral("Duration"),
                                        QStringLiteral("Text")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked |
                             QAbstractItemView::EditKeyPressed);
    m_table->setWordWrap(true);
    m_table->setAlternatingRowColors(true);
    m_table->setItemDelegateForColumn(ColText, new MultiLineDelegate(m_table));
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    auto *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(ColStart,    QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColDuration, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColText,     QHeaderView::Stretch);
    connect(m_table, &QTableWidget::cellChanged, this, &LogPanel::onCellChanged);
    root->addWidget(m_table, 1);

    // Delete key on the table (not while editing a cell).
    auto *del = new QShortcut(QKeySequence::Delete, m_table);
    del->setContext(Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, &LogPanel::onDeleteSelected);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    auto *btnCopy = new QPushButton(QStringLiteral("Copy All"));
    btnCopy->setToolTip(QStringLiteral("Copy the whole log as tab-separated text "
                                       "(pastes into Excel / Sheets)"));
    connect(btnCopy, &QPushButton::clicked, this, &LogPanel::onCopyAll);
    btnRow->addWidget(btnCopy);

    auto *btnExport = new QPushButton(QStringLiteral("Export..."));
    btnExport->setToolTip(QStringLiteral("Save the log as TSV or JSON"));
    connect(btnExport, &QPushButton::clicked, this, &LogPanel::onExport);
    btnRow->addWidget(btnExport);

    auto *btnDelete = new QPushButton(QStringLiteral("Delete Rows"));
    btnDelete->setToolTip(QStringLiteral("Delete the selected rows"));
    connect(btnDelete, &QPushButton::clicked, this, &LogPanel::onDeleteSelected);
    btnRow->addWidget(btnDelete);

    btnRow->addStretch();

    m_countLabel = new QLabel;
    m_countLabel->setStyleSheet(QStringLiteral("color: #888;"));
    btnRow->addWidget(m_countLabel);

    auto *btnClear = new QPushButton(QStringLiteral("Clear Log..."));
    btnClear->setToolTip(QStringLiteral("Remove every entry (asks first)"));
    connect(btnClear, &QPushButton::clicked, this, &LogPanel::onClearLog);
    btnRow->addWidget(btnClear);

    root->addLayout(btnRow);

    load();
    updateCountLabel();
    QTimer::singleShot(0, m_table, &QTableWidget::scrollToBottom);
}

// ── persistence ──────────────────────────────────────────────────────

void LogPanel::load()
{
    QFile f(m_path);
    if (!f.exists()) return;

    QString error;
    std::optional<QList<LogEntry>> loaded;
    if (f.open(QIODevice::ReadOnly))
        loaded = TranscriptLog::fromJson(f.readAll(), &error);
    else
        error = f.errorString();
    if (!loaded) {
        // Never overwrite a log we couldn't read.
        m_loadFailed = true;
        LOG(QStringLiteral("load failed: %1").arg(error));
        const QString msg = QStringLiteral(
            "The log file could not be read, so new entries won't be saved "
            "to it this session (to avoid overwriting it):\n\n%1")
            .arg(QDir::toNativeSeparators(m_path));
        QTimer::singleShot(0, this, [msg]{
            QMessageBox::warning(nullptr, QStringLiteral("WhisperVTT — Log"), msg);
        });
        return;
    }

    const QSignalBlocker block(m_table);
    m_entries = *loaded;
    for (const LogEntry &e : std::as_const(m_entries))
        addRow(e);
    LOG(QStringLiteral("loaded %1 entries").arg(m_entries.size()));
}

bool LogPanel::save()
{
    if (m_loadFailed) {
        emit statusMessage(QStringLiteral("Log not saved: the existing log file couldn't be read"), 0);
        return false;
    }

    QSaveFile f(m_path);   // writes a temp file, renames on commit
    const QByteArray bytes = TranscriptLog::toJson(m_entries);
    const bool ok = f.open(QIODevice::WriteOnly) &&
                    f.write(bytes) == bytes.size() && f.commit();
    if (!ok) {
        LOG(QStringLiteral("save failed: %1").arg(f.errorString()));
        emit statusMessage(QStringLiteral("Couldn't save the log: %1").arg(f.errorString()), 0);
    }
    return ok;
}

// ── table ────────────────────────────────────────────────────────────

void LogPanel::addRow(const LogEntry &e)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto readOnly = [](const QString &s) {
        auto *it = new QTableWidgetItem(s);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        return it;
    };

    auto *start = readOnly(displayStamp(e.start));
    start->setToolTip(QStringLiteral("Start: %1\nEnd:   %2")
                          .arg(isoStamp(e.start), isoStamp(e.end)));
    m_table->setItem(row, ColStart, start);

    auto *dur = readOnly(QStringLiteral("%1 s").arg(e.duration, 0, 'f', 1));
    dur->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_table->setItem(row, ColDuration, dur);

    m_table->setItem(row, ColText, new QTableWidgetItem(e.text));
}

void LogPanel::markChanged()
{
    m_exported = false;
    save();
    updateCountLabel();
}

void LogPanel::append(const LogEntry &entry)
{
    m_entries.append(entry);
    {
        const QSignalBlocker block(m_table);
        addRow(entry);
    }
    markChanged();
    m_table->scrollToBottom();
}

void LogPanel::onCellChanged(int row, int column)
{
    if (column != ColText || row < 0 || row >= m_entries.size()) return;
    const QString newText = m_table->item(row, column)->text();
    if (newText == m_entries[row].text) return;
    m_entries[row].text = newText;
    markChanged();
    emit statusMessage(QStringLiteral("Log entry updated"), 3000);
}

void LogPanel::onDeleteSelected()
{
    QList<int> rows;
    for (const QModelIndex &idx : m_table->selectionModel()->selectedRows())
        rows.append(idx.row());
    if (rows.isEmpty()) {
        emit statusMessage(QStringLiteral("Select one or more log rows to delete"), 3000);
        return;
    }
    if (rows.size() == m_entries.size()) {
        onClearLog();   // deleting everything goes through the clear safeguards
        return;
    }

    const auto answer = QMessageBox::question(this,
        QStringLiteral("Delete log rows"),
        QStringLiteral("Delete %1 selected row%2?")
            .arg(rows.size()).arg(rows.size() == 1 ? "" : "s"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    std::sort(rows.begin(), rows.end(), std::greater<int>());
    {
        const QSignalBlocker block(m_table);
        for (int r : rows) {
            m_entries.removeAt(r);
            m_table->removeRow(r);
        }
    }
    markChanged();
}

bool LogPanel::confirmClear(const QString &title)
{
    if (m_exported) {
        return QMessageBox::question(this, title,
            QStringLiteral("Clear %1 from the log?\n\nThey have been exported.")
                .arg(entries(m_entries.size())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    }

    // Not exported since the last change — require unmistakable intent.
    bool ok = false;
    const QString typed = QInputDialog::getText(this, title,
        QStringLiteral("The log has %1 that have NOT been exported.\n"
                       "Clearing will permanently delete them.\n\n"
                       "Type DELETE to confirm:").arg(entries(m_entries.size())),
        QLineEdit::Normal, QString(), &ok);
    return ok && typed.trimmed() == QStringLiteral("DELETE");
}

void LogPanel::onClearLog()
{
    if (m_entries.isEmpty()) {
        emit statusMessage(QStringLiteral("Log is already empty"), 3000);
        return;
    }
    if (!confirmClear(QStringLiteral("Clear log")))
        return;
    clearAll();
}

void LogPanel::clearAll()
{
    {
        const QSignalBlocker block(m_table);
        m_entries.clear();
        m_table->setRowCount(0);
    }
    markChanged();
    emit statusMessage(QStringLiteral("Log cleared"), 3000);
}

// ── copy / export ────────────────────────────────────────────────────

void LogPanel::onCopyAll()
{
    if (m_entries.isEmpty()) {
        emit statusMessage(QStringLiteral("Log is empty"), 3000);
        return;
    }
    QApplication::clipboard()->setText(TranscriptLog::toTsv(m_entries));
    emit statusMessage(QStringLiteral("Copied %1 to the clipboard").arg(entries(m_entries.size())), 3000);
}

void LogPanel::onExport()
{
    if (m_entries.isEmpty()) {
        emit statusMessage(QStringLiteral("Log is empty"), 3000);
        return;
    }
    const QString tsvFilter  = QStringLiteral("Tab-separated values (*.tsv)");
    const QString jsonFilter = QStringLiteral("JSON (*.json)");
    QString filter = tsvFilter;
    const QString suggested =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QLatin1Char('/') + TranscriptLog::exportBaseName(m_entries) + QStringLiteral(".tsv");
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export log"), suggested,
                                                tsvFilter + QStringLiteral(";;") + jsonFilter,
                                                &filter);
    if (path.isEmpty()) return;

    // The suggested name ends in .tsv; if the user picked the JSON filter
    // without changing it, switch the extension to match.
    bool json = path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive);
    if (!json && filter == jsonFilter) {
        if (path.endsWith(QLatin1String(".tsv"), Qt::CaseInsensitive)) path.chop(4);
        path += QStringLiteral(".json");
        json = true;
    }

    QSaveFile f(path);
    // BOM so Excel opens the TSV as UTF-8 rather than the ANSI codepage.
    const QByteArray bytes = json ? TranscriptLog::toJson(m_entries)
                                  : QByteArray("\xEF\xBB\xBF") + TranscriptLog::toTsv(m_entries).toUtf8();
    const bool ok = f.open(QIODevice::WriteOnly) &&
                    f.write(bytes) == bytes.size() && f.commit();
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Export failed"),
            QStringLiteral("Could not write %1:\n%2")
                .arg(QDir::toNativeSeparators(path), f.errorString()));
        return;
    }
    m_exported = true;
    emit statusMessage(QStringLiteral("Exported %1 to %2")
                           .arg(entries(m_entries.size())).arg(QDir::toNativeSeparators(path)), 5000);

    if (QMessageBox::question(this, QStringLiteral("Export complete"),
            QStringLiteral("Exported %1 to:\n%2\n\nClear the log now?")
                .arg(entries(m_entries.size())).arg(QDir::toNativeSeparators(path)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        clearAll();
}

void LogPanel::updateCountLabel()
{
    QString s = m_entries.size() == 1 ? QStringLiteral("1 entry")
                                      : QStringLiteral("%1 entries").arg(m_entries.size());
    if (!m_entries.isEmpty() && !m_exported)
        s += QStringLiteral(" (not exported)");
    m_countLabel->setText(s);
    m_countLabel->setToolTip(QDir::toNativeSeparators(m_path));
}
