#pragma once

// Debug logging — disabled in production builds.
// Define WHISPER_VTT_DEBUG to enable logging to %TEMP%\whisper_vtt_debug.log

#include <QString>

#ifdef WHISPER_VTT_DEBUG
namespace DebugLog {
    void write(const char *tag, const QString &msg);
    QString logPath();
}
#define DLOG(tag, msg) DebugLog::write(tag, msg)
#else
#define DLOG(tag, msg) ((void)0)
#endif
