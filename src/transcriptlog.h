#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>

// One transcription in the transcript log.
struct LogEntry {
    QDateTime start;           // mic opened (local time, offset recorded)
    QDateTime end;             // mic closed
    double    duration = 0.0;  // seconds of captured audio
    QString   text;
};

// The transcript log's file formats (SPEC §10).  Pure functions, so the
// on-disk and export formats are unit-tested.
namespace TranscriptLog {
    // ISO-8601 with milliseconds and an explicit UTC offset, e.g.
    // 2026-10-12T14:03:22.123-05:00.  Empty for an invalid time.
    QString isoStamp(const QDateTime &dt);

    // The log file, also used for JSON export: {"entries": [...]}.
    QByteArray toJson(const QList<LogEntry> &entries);

    // Parses toJson() output.  Nothing if the data isn't a valid log, in
    // which case |error| (if given) says why.
    std::optional<QList<LogEntry>> fromJson(const QByteArray &data, QString *error = nullptr);

    // Header plus one tab-separated row per entry; tabs and line breaks
    // inside the text become spaces.
    QString toTsv(const QList<LogEntry> &entries);

    // e.g. transcript_log_2026-10-12_090312_to_2026-10-12_154755
    QString exportBaseName(const QList<LogEntry> &entries);
}
