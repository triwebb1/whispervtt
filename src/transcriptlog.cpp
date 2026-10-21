#include "transcriptlog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

QString TranscriptLog::isoStamp(const QDateTime &dt)
{
    if (!dt.isValid()) return {};
    return dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODateWithMs);
}

static QDateTime parseStamp(const QJsonValue &v)
{
    return QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
}

QByteArray TranscriptLog::toJson(const QList<LogEntry> &entries)
{
    QJsonArray arr;
    for (const LogEntry &e : entries) {
        QJsonObject o;
        o[QStringLiteral("start")]        = isoStamp(e.start);
        o[QStringLiteral("end")]          = isoStamp(e.end);
        o[QStringLiteral("duration_sec")] = qRound(e.duration * 100.0) / 100.0;
        o[QStringLiteral("text")]         = e.text;
        arr.append(o);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("entries"), arr}})
        .toJson(QJsonDocument::Indented);
}

std::optional<QList<LogEntry>> TranscriptLog::fromJson(const QByteArray &data, QString *error)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    const QJsonValue entries = doc.object().value(QStringLiteral("entries"));
    if (err.error != QJsonParseError::NoError || !entries.isArray()) {
        if (error)
            *error = err.error != QJsonParseError::NoError
                ? err.errorString() : QStringLiteral("no \"entries\" array");
        return std::nullopt;
    }

    QList<LogEntry> out;
    for (const QJsonValue &v : entries.toArray()) {
        const QJsonObject o = v.toObject();
        out.append({parseStamp(o.value(QStringLiteral("start"))),
                    parseStamp(o.value(QStringLiteral("end"))),
                    o.value(QStringLiteral("duration_sec")).toDouble(),
                    o.value(QStringLiteral("text")).toString()});
    }
    return out;
}

QString TranscriptLog::toTsv(const QList<LogEntry> &entries)
{
    auto clean = [](QString s) {
        s.replace(QLatin1String("\r\n"), QLatin1String(" "));
        s.replace(QLatin1Char('\n'), QLatin1Char(' '));
        s.replace(QLatin1Char('\r'), QLatin1Char(' '));
        s.replace(QLatin1Char('\t'), QLatin1Char(' '));
        return s;
    };
    QString out = QStringLiteral("start\tend\tduration_sec\ttext\n");
    for (const LogEntry &e : entries) {
        out += isoStamp(e.start) + QLatin1Char('\t')
             + isoStamp(e.end) + QLatin1Char('\t')
             + QString::number(e.duration, 'f', 2) + QLatin1Char('\t')
             + clean(e.text) + QLatin1Char('\n');
    }
    return out;
}

QString TranscriptLog::exportBaseName(const QList<LogEntry> &entries)
{
    if (entries.isEmpty()) return QStringLiteral("transcript_log");
    const QString fmt = QStringLiteral("yyyy-MM-dd_HHmmss");
    const QDateTime first = entries.first().start;
    const QDateTime last  = entries.last().end.isValid() ? entries.last().end
                                                         : entries.last().start;
    return QStringLiteral("transcript_log_%1_to_%2")
        .arg(first.toLocalTime().toString(fmt), last.toLocalTime().toString(fmt));
}
