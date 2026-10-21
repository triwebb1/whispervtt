#include "formatter.h"

#include <QTest>

class TestFormatter : public QObject {
    Q_OBJECT
private slots:
    void format_data();
    void format();
    void breaksLongParagraphs();
};

void TestFormatter::format_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("empty") << "" << "";
    QTest::newRow("plain text is untouched")
        << "Just a normal sentence." << "Just a normal sentence.";

    // ── voice commands ──
    QTest::newRow("new paragraph")
        << "Hello new paragraph world" << "Hello\n\nWorld";
    QTest::newRow("punctuated command")
        << "Hello. New paragraph. World." << "Hello.\n\nWorld.";
    QTest::newRow("new line")
        << "Dear team, new line thanks for coming." << "Dear team\nThanks for coming.";
    QTest::newRow("heading")
        << "Heading introduction new paragraph this is the body."
        << "# Introduction\n\nThis is the body.";
    QTest::newRow("sub heading")
        << "Sub heading details" << "## Details";
    QTest::newRow("heading drops whisper's full stop")
        << "Sub heading. Open questions. New paragraph. None yet."
        << "## Open questions\n\nNone yet.";
    QTest::newRow("bullet points")
        << "Shopping list. Bullet point apples. Bullet point pears."
        << "Shopping list.\n- Apples.\n- Pears.";
    QTest::newRow("command words inside other words")
        << "The dashboard headings are fine." << "The dashboard headings are fine.";
    QTest::newRow("'heading' mid-sentence is just a word")
        << "We're heading home." << "We're heading home.";
    QTest::newRow("'title' mid-sentence is just a word")
        << "The job title is open." << "The job title is open.";
    QTest::newRow("'bullet' mid-sentence is just a word")
        << "We had to bite the bullet." << "We had to bite the bullet.";
    QTest::newRow("'dash' mid-sentence is just a word")
        << "Make a dash for the train." << "Make a dash for the train.";
    QTest::newRow("command after a comma")
        << "Notes, heading introduction" << "Notes\n\n# Introduction";
    QTest::newRow("mixed-case word keeps its case")
        << "Notes new line iPhone sales are up." << "Notes\niPhone sales are up.";

    // ── ordinal lists ──
    QTest::newRow("inline ordinals become a list")
        << "Here is the plan. First, buy milk. Second, call Sam. Third, book the venue."
        << "Here is the plan.\n\n1. Buy milk.\n2. Call Sam.\n3. Book the venue.";
    QTest::newRow("ordinals on separate lines")
        << "first buy milk new line second call Sam"
        << "1. Buy milk\n2. Call Sam";
    QTest::newRow("a lone 'first' is prose")
        << "First of all, thanks for coming." << "First of all, thanks for coming.";
    QTest::newRow("mid-sentence ordinal is not an item")
        << "First, open the app. Then wait a second." << "First, open the app. Then wait a second.";
}

void TestFormatter::format()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(Formatter::format(input), expected);
}

void TestFormatter::breaksLongParagraphs()
{
    const QString sentence =
        QStringLiteral("This sentence is long enough to push the paragraph past the limit.");
    QStringList six;
    for (int i = 0; i < 6; ++i) six.append(sentence);

    const QString out = Formatter::format(six.join(QLatin1Char(' ')));

    const QString three = six.mid(0, 3).join(QLatin1Char(' '));
    QCOMPARE(out, three + QStringLiteral("\n\n") + three);
}

QTEST_APPLESS_MAIN(TestFormatter)
#include "tst_formatter.moc"
