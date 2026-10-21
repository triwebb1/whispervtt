#include "debuglog.h"

#ifdef WHISPER_VTT_DEBUG
#include <QStandardPaths>
#include <QDateTime>
#include <QFile>
#include <cstdio>
#include <share.h>
#include <mutex>

static QString getLogPath()
{
    static QString p = QStandardPaths::writableLocation(
                           QStandardPaths::TempLocation)
                       + QStringLiteral("/whisper_vtt_debug.log");
    return p;
}

QString DebugLog::logPath() { return getLogPath(); }

void DebugLog::write(const char *tag, const QString &msg)
{
    // Called from the GUI, worker and hook threads.
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);

    // Start fresh once per run if the log has grown large.
    static bool checked = false;
    if (!checked) {
        checked = true;
        if (QFile(getLogPath()).size() > 5 * 1024 * 1024)
            QFile::remove(getLogPath());
    }

    // Shared, so the log can be read while the app is writing it.
    FILE *f = _wfsopen(reinterpret_cast<const wchar_t *>(getLogPath().utf16()), L"a", _SH_DENYNO);
    if (!f) return;
    QByteArray ts = QDateTime::currentDateTime()
                        .toString(QStringLiteral("hh:mm:ss.zzz")).toUtf8();
    fprintf(f, "%s  [%s] %s\n", ts.constData(), tag, msg.toUtf8().constData());
    fclose(f);
}
#endif
