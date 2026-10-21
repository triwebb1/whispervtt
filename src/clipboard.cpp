#include "clipboard.h"
#include "debuglog.h"

#include <algorithm>
#include <cstring>
#include <string>

#define LOG(msg) DLOG("clipboard", msg)

using Format = Clipboard::Snapshot::Format;

// Owns the clipboard while we write to it.  (EmptyClipboard with no
// owner window leaves the clipboard unowned, and SetClipboardData may
// then fail.)
static HWND owner()
{
    static const HWND hwnd = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                             HWND_MESSAGE, nullptr,
                                             GetModuleHandleW(nullptr), nullptr);
    return hwnd;
}

// Clipboard managers, RDP and Office often hold the clipboard open for a
// few ms; a single OpenClipboard attempt fails intermittently.
static bool open()
{
    for (int i = 0; i < 20; ++i) {
        if (OpenClipboard(owner())) return true;
        Sleep(10);
    }
    LOG("OpenClipboard failed");
    return false;
}

// Every memory-based format on the (open) clipboard.  GDI handle formats
// are skipped; Windows re-synthesises CF_BITMAP and friends from the DIB.
static QList<Format> readAll()
{
    QList<Format> out;
    for (UINT f = EnumClipboardFormats(0); f; f = EnumClipboardFormats(f)) {
        const bool gdiHandle = f == CF_BITMAP || f == CF_ENHMETAFILE ||
                               f == CF_METAFILEPICT || f == CF_PALETTE ||
                               f == CF_OWNERDISPLAY || f == CF_DSPBITMAP ||
                               f == CF_DSPENHMETAFILE || f == CF_DSPMETAFILEPICT ||
                               (f >= CF_PRIVATEFIRST && f <= CF_GDIOBJLAST);
        if (gdiHandle) continue;
        HANDLE h = GetClipboardData(f);
        const void *p = h ? GlobalLock(h) : nullptr;
        if (!p) continue;
        out.append({f, QByteArray(static_cast<const char *>(p), qsizetype(GlobalSize(h)))});
        GlobalUnlock(h);
    }
    return out;
}

// Puts |formats| on the (open, emptied) clipboard.
static bool writeAll(const QList<Format> &formats)
{
    bool ok = true;
    for (const Format &f : formats) {
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, SIZE_T(std::max<qsizetype>(f.data.size(), 1)));
        void *p = mem ? GlobalLock(mem) : nullptr;
        if (!p) { if (mem) GlobalFree(mem); ok = false; continue; }
        memcpy(p, f.data.constData(), size_t(f.data.size()));
        GlobalUnlock(mem);
        if (!SetClipboardData(f.id, mem)) {
            GlobalFree(mem);   // ownership only transfers on success
            ok = false;
        }
    }
    return ok;
}

// Formats that tell Windows and clipboard managers to treat the content
// as transient (documented under "Clipboard Formats" on learn.microsoft.com).
static QList<Format> privateMarkers()
{
    const QByteArray zero(sizeof(DWORD), '\0');
    return {
        {RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"), zero},
        {RegisterClipboardFormatW(L"CanIncludeInClipboardHistory"), zero},
        {RegisterClipboardFormatW(L"CanUploadToCloudClipboard"), zero},
    };
}

std::optional<Clipboard::Snapshot> Clipboard::replaceWithPrivateText(const QString &text)
{
    if (!open()) return std::nullopt;
    Snapshot previous{readAll()};
    EmptyClipboard();

    const std::wstring wide = text.toStdWString();
    const Format unicode{CF_UNICODETEXT,
                         QByteArray(reinterpret_cast<const char *>(wide.c_str()),
                                    qsizetype((wide.size() + 1) * sizeof(wchar_t)))};
    const bool ok = writeAll(QList<Format>{unicode} + privateMarkers());
    CloseClipboard();
    if (!ok) {
        LOG("SetClipboardData failed");
        return std::nullopt;
    }
    return previous;
}

bool Clipboard::restore(const Snapshot &previous)
{
    if (!open()) return false;
    EmptyClipboard();
    const bool ok = previous.formats.isEmpty() || writeAll(previous.formats + privateMarkers());
    CloseClipboard();
    return ok;
}
