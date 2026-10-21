#include "textcleanup.h"

#include <QTest>

class TestTextCleanup : public QObject {
    Q_OBJECT
private slots:
    void isNonSpeech_data();
    void isNonSpeech();
    void capitalizeFirstWord_data();
    void capitalizeFirstWord();
    void assemble_data();
    void assemble();
};

void TestTextCleanup::isNonSpeech_data()
{
    QTest::addColumn<QString>("segment");
    QTest::addColumn<bool>("expected");

    QTest::newRow("empty")            << ""                       << true;
    QTest::newRow("blank tag")        << "[BLANK_AUDIO]"          << true;
    QTest::newRow("sound effect")     << "(wind blowing)"         << true;
    QTest::newRow("asterisks")        << "*sighs*"                << true;
    QTest::newRow("several tags")     << " [Music] (applause) "   << true;
    QTest::newRow("bare punctuation") << "..."                    << true;
    QTest::newRow("sentence")         << "Hello there."           << false;
    QTest::newRow("number")           << "42"                     << false;
    QTest::newRow("tag then words")   << "[Note] remember milk"   << false;
}

void TestTextCleanup::isNonSpeech()
{
    QFETCH(QString, segment);
    QFETCH(bool, expected);
    QCOMPARE(TextCleanup::isNonSpeech(segment), expected);
}

void TestTextCleanup::capitalizeFirstWord_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("empty")         << ""            << "";
    QTest::newRow("lower")         << "hello world" << "Hello world";
    QTest::newRow("already upper") << "Hello"       << "Hello";
    QTest::newRow("mixed case 1")  << "iPhone sales" << "iPhone sales";
    QTest::newRow("mixed case 2")  << "eBay"        << "eBay";
    QTest::newRow("leading quote") << "'quoted'"    << "'quoted'";
    QTest::newRow("accented")      << "école"       << "École";
}

void TestTextCleanup::capitalizeFirstWord()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(TextCleanup::capitalizeFirstWord(input), expected);
}

void TestTextCleanup::assemble_data()
{
    QTest::addColumn<QStringList>("segments");
    QTest::addColumn<QString>("expected");

    QTest::newRow("none") << QStringList{} << "";
    QTest::newRow("only noise")
        << QStringList{"[BLANK_AUDIO]", " (silence)", "..."} << "";
    QTest::newRow("joins and trims")
        << QStringList{" hello there.", " How are you?"}
        << "Hello there. How are you?";
    QTest::newRow("drops noise between speech")
        << QStringList{" Okay, so", "[BLANK_AUDIO]", " let's begin."}
        << "Okay, so let's begin.";
}

void TestTextCleanup::assemble()
{
    QFETCH(QStringList, segments);
    QFETCH(QString, expected);
    QCOMPARE(TextCleanup::assemble(segments), expected);
}

QTEST_APPLESS_MAIN(TestTextCleanup)
#include "tst_textcleanup.moc"
