#include "clipboard.h"

#include <QTest>
#include <string>

// Exercises the real Windows clipboard.  The user's clipboard is saved
// before the tests and put back afterwards.

namespace {

UINT format(const wchar_t *name) { return RegisterClipboardFormatW(name); }

// Sets the clipboard to |formats| directly through Win32.
void setClipboard(const QList<Clipboard::Snapshot::Format> &formats)
{
    QVERIFY(OpenClipboard(nullptr));
    EmptyClipboard();
    for (const auto &f : formats) {
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, SIZE_T(f.data.size()));
        memcpy(GlobalLock(mem), f.data.constData(), size_t(f.data.size()));
        GlobalUnlock(mem);
        SetClipboardData(f.id, mem);
    }
    CloseClipboard();
}

QByteArray read(UINT id)
{
    QByteArray out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE h = GetClipboardData(id)) {
        out = QByteArray(static_cast<const char *>(GlobalLock(h)), qsizetype(GlobalSize(h)));
        GlobalUnlock(h);
    }
    CloseClipboard();
    return out;
}

QString readText()
{
    const QByteArray b = read(CF_UNICODETEXT);
    return QString::fromWCharArray(reinterpret_cast<const wchar_t *>(b.constData()));
}

Clipboard::Snapshot::Format text(const QString &s)
{
    const std::wstring w = s.toStdWString();
    return {CF_UNICODETEXT, QByteArray(reinterpret_cast<const char *>(w.c_str()),
                                       qsizetype((w.size() + 1) * sizeof(wchar_t)))};
}

} // namespace

class TestClipboard : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void dictationIsPrivate();
    void restoresEveryFormat();
    void emptyClipboardStaysEmpty();

private:
    std::optional<Clipboard::Snapshot> m_user;
};

void TestClipboard::initTestCase()
{
    m_user = Clipboard::replaceWithPrivateText(QString());
    if (!m_user)
        QSKIP("No clipboard in this session (e.g. a service account on a CI runner)");
}

void TestClipboard::cleanupTestCase()
{
    if (m_user) Clipboard::restore(*m_user);
}

void TestClipboard::dictationIsPrivate()
{
    setClipboard({text(QStringLiteral("copied earlier"))});
    QVERIFY(Clipboard::replaceWithPrivateText(QStringLiteral("dictated words")));

    QCOMPARE(readText(), QStringLiteral("dictated words"));
    QVERIFY(IsClipboardFormatAvailable(format(L"CanIncludeInClipboardHistory")));
    QVERIFY(IsClipboardFormatAvailable(format(L"CanUploadToCloudClipboard")));
    QVERIFY(IsClipboardFormatAvailable(format(L"ExcludeClipboardContentFromMonitorProcessing")));
    QCOMPARE(read(format(L"CanIncludeInClipboardHistory")).left(4), QByteArray(4, '\0'));
}

void TestClipboard::restoresEveryFormat()
{
    // Text, HTML and an image (a tiny DIB), as a browser or Office would put them.
    const QByteArray html("Version:0.9\r\n<b>copied earlier</b>");
    QByteArray dib(sizeof(BITMAPINFOHEADER) + 4, '\0');
    auto *bih = reinterpret_cast<BITMAPINFOHEADER *>(dib.data());
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = 1; bih->biHeight = 1; bih->biPlanes = 1; bih->biBitCount = 32;
    dib[int(sizeof(BITMAPINFOHEADER)) + 2] = '\xFF';   // one red pixel

    setClipboard({text(QStringLiteral("copied earlier")),
                  {format(L"HTML Format"), html},
                  {CF_DIB, dib}});

    const auto previous = Clipboard::replaceWithPrivateText(QStringLiteral("dictated words"));
    QVERIFY(previous);
    QVERIFY(!IsClipboardFormatAvailable(CF_DIB));

    QVERIFY(Clipboard::restore(*previous));
    QCOMPARE(readText(), QStringLiteral("copied earlier"));
    QVERIFY(read(format(L"HTML Format")).startsWith(html));
    QVERIFY(read(CF_DIB).startsWith(dib));
}

void TestClipboard::emptyClipboardStaysEmpty()
{
    setClipboard({});
    const auto previous = Clipboard::replaceWithPrivateText(QStringLiteral("dictated words"));
    QVERIFY(previous);
    QVERIFY(Clipboard::restore(*previous));
    QVERIFY(!IsClipboardFormatAvailable(CF_UNICODETEXT));
}

QTEST_APPLESS_MAIN(TestClipboard)
#include "tst_clipboard.moc"
