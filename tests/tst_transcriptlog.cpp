#include "transcriptlog.h"

#include <QTest>
#include <QTimeZone>

namespace {

// 2026-10-14 09:12:04.318 at UTC-5, as a recording in Chicago would log it.
QDateTime at(int h, int m, int s, int ms = 0)
{
    return QDateTime(QDate(2026, 10, 14), QTime(h, m, s, ms),
                     QTimeZone::fromSecondsAheadOfUtc(-5 * 3600));
}

LogEntry entry(const QDateTime &start, double seconds, const QString &text)
{
    return {start, start.addMSecs(qint64(seconds * 1000)), seconds, text};
}

} // namespace

class TestTranscriptLog : public QObject {
    Q_OBJECT
private slots:
    void isoStampKeepsOffsetAndMilliseconds();
    void jsonRoundTrip();
    void jsonRejectsUnreadableLogs_data();
    void jsonRejectsUnreadableLogs();
    void tsvFlattensText();
    void exportNameSpansTheLog();
};

void TestTranscriptLog::isoStampKeepsOffsetAndMilliseconds()
{
    QCOMPARE(TranscriptLog::isoStamp(at(9, 12, 4, 318)),
             QStringLiteral("2026-10-14T09:12:04.318-05:00"));
    QCOMPARE(TranscriptLog::isoStamp(QDateTime()), QString());
}

void TestTranscriptLog::jsonRoundTrip()
{
    const QList<LogEntry> log = {
        entry(at(9, 12, 4, 318), 6.4,  QStringLiteral("Standup notes.")),
        entry(at(9, 59, 4),      4.126, QStringLiteral("Quotes \" and\nnew lines survive.")),
    };

    const auto parsed = TranscriptLog::fromJson(TranscriptLog::toJson(log));
    QVERIFY(parsed);
    QCOMPARE(parsed->size(), 2);
    QCOMPARE(parsed->at(0).start, log[0].start);
    QCOMPARE(parsed->at(0).end, log[0].end);
    QCOMPARE(parsed->at(0).start.offsetFromUtc(), -5 * 3600);
    QCOMPARE(parsed->at(1).text, log[1].text);
    QCOMPARE(parsed->at(1).duration, 4.13);   // stored to two decimals
}

void TestTranscriptLog::jsonRejectsUnreadableLogs_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::newRow("not JSON")        << QByteArray("{ truncated");
    QTest::newRow("no entries")      << QByteArray("{\"items\": []}");
    QTest::newRow("entries not list") << QByteArray("{\"entries\": {}}");
}

void TestTranscriptLog::jsonRejectsUnreadableLogs()
{
    QFETCH(QByteArray, data);
    QString error;
    QVERIFY(!TranscriptLog::fromJson(data, &error));
    QVERIFY(!error.isEmpty());
}

void TestTranscriptLog::tsvFlattensText()
{
    const QString tsv = TranscriptLog::toTsv(
        {entry(at(9, 12, 4, 318), 6.4, QStringLiteral("one\ttwo\r\nthree\nfour"))});
    QCOMPARE(tsv, QStringLiteral(
        "start\tend\tduration_sec\ttext\n"
        "2026-10-14T09:12:04.318-05:00\t2026-10-14T09:12:10.718-05:00\t6.40\tone two three four\n"));
}

void TestTranscriptLog::exportNameSpansTheLog()
{
    const QDateTime first = at(9, 12, 4);
    const QDateTime last  = at(14, 27, 4);
    const QString name = TranscriptLog::exportBaseName(
        {entry(first, 2.0, QStringLiteral("a")), entry(last, 5.0, QStringLiteral("b"))});

    const QString fmt = QStringLiteral("yyyy-MM-dd_HHmmss");
    QCOMPARE(name, QStringLiteral("transcript_log_%1_to_%2")
                       .arg(first.toLocalTime().toString(fmt),
                            last.addSecs(5).toLocalTime().toString(fmt)));
    QCOMPARE(TranscriptLog::exportBaseName({}), QStringLiteral("transcript_log"));
}

QTEST_APPLESS_MAIN(TestTranscriptLog)
#include "tst_transcriptlog.moc"
