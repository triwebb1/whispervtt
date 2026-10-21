#include "modelcatalog.h"

#include <QTest>

class TestModelCatalog : public QObject {
    Q_OBJECT
private slots:
    void choose_data();
    void choose();
    void recognisesModelFiles();
    void recommendsByHardware();
    void downloadUrlPointsAtModelRepo();
};

void TestModelCatalog::choose_data()
{
    QTest::addColumn<QStringList>("present");
    QTest::addColumn<QString>("configured");
    QTest::addColumn<QString>("expected");

    const QString turbo = QStringLiteral("ggml-large-v3-turbo.bin");

    QTest::newRow("nothing installed keeps the configured name")
        << QStringList{} << turbo << turbo;
    QTest::newRow("configured model is present")
        << QStringList{"ggml-small.en.bin", "ggml-base.en.bin"} << "ggml-base.en.bin"
        << "ggml-base.en.bin";
    QTest::newRow("configured match ignores case")
        << QStringList{"GGML-BASE.EN.BIN"} << "ggml-base.en.bin" << "ggml-base.en.bin";
    QTest::newRow("missing configured model falls back to turbo first")
        << QStringList{"ggml-small.en.bin", "ggml-large-v3-turbo.bin"} << "ggml-medium.bin"
        << turbo;
    QTest::newRow("preference order beyond turbo")
        << QStringList{"ggml-base.en.bin", "ggml-small.en.bin"} << turbo
        << "ggml-small.en.bin";
    QTest::newRow("unknown models fall back alphabetically")
        << QStringList{"zeta.bin", "custom-model.bin"} << turbo << "custom-model.bin";
}

void TestModelCatalog::choose()
{
    QFETCH(QStringList, present);
    QFETCH(QString, configured);
    QFETCH(QString, expected);
    QCOMPARE(ModelCatalog::choose(present, configured), expected);
}

void TestModelCatalog::recognisesModelFiles()
{
    QVERIFY(ModelCatalog::hasModelHeader(QByteArray("lmgg\x9a\xca\0\0", 8)));
    QVERIFY(!ModelCatalog::hasModelHeader(QByteArray(4, '\0')));   // preallocated placeholder
    QVERIFY(!ModelCatalog::hasModelHeader(QByteArray()));            // empty file
    QVERIFY(!ModelCatalog::hasModelHeader("<!DOCTYPE html>"));       // saved error page
}

void TestModelCatalog::recommendsByHardware()
{
    QCOMPARE(ModelCatalog::recommend(true).file,  QStringLiteral("ggml-large-v3-turbo.bin"));
    QCOMPARE(ModelCatalog::recommend(false).file, QStringLiteral("ggml-small.en.bin"));
    QCOMPARE(QString::fromLatin1(ModelCatalog::kDefaultModel), ModelCatalog::recommend(true).file);
}

void TestModelCatalog::downloadUrlPointsAtModelRepo()
{
    QCOMPARE(ModelCatalog::downloadUrl(QStringLiteral("ggml-base.en.bin")),
             QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin"));
}

QTEST_APPLESS_MAIN(TestModelCatalog)
#include "tst_modelcatalog.moc"
