#pragma once

#include "transcriptlog.h"

#include <QList>
#include <QString>
#include <QWidget>

class QLabel;
class QTableWidget;

// The "Log" tab: a running, timestamped table of transcriptions that is
// kept (across Clear on the Edit tab and app restarts) until the user
// exports and clears it.
//
// Stored at <dataDir>/transcript_log.json, saved atomically on every
// change.  Clearing entries that haven't been exported requires typing
// DELETE.
class LogPanel : public QWidget {
    Q_OBJECT
public:
    explicit LogPanel(const QString &dataDir, QWidget *parent = nullptr);

    void append(const LogEntry &entry);

signals:
    void statusMessage(const QString &msg, int timeoutMs);

private slots:
    void onCellChanged(int row, int column);
    void onDeleteSelected();
    void onCopyAll();
    void onExport();
    void onClearLog();

private:
    void load();
    bool save();
    void addRow(const LogEntry &e);
    void markChanged();
    void updateCountLabel();
    void clearAll();
    bool confirmClear(const QString &title);

    QString         m_path;
    QList<LogEntry> m_entries;   // same order as table rows
    bool            m_loadFailed = false;
    bool            m_exported   = false;  // exported since the last change

    QTableWidget *m_table = nullptr;
    QLabel       *m_countLabel = nullptr;
};
