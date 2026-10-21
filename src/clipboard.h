#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>
#include <windows.h>

// Win32 clipboard access for pasting dictated text without leaving a
// trace or disturbing what the user had copied.
namespace Clipboard {
    // A copy of every memory-based clipboard format (text, rich text,
    // HTML, images as DIB, file lists, application formats).
    struct Snapshot {
        struct Format {
            UINT       id;
            QByteArray data;
        };
        QList<Format> formats;
    };

    // Replaces the clipboard with |text|, marked private: Windows keeps
    // it out of clipboard history (Win+V) and cloud sync, and clipboard
    // managers ignore it.  Returns the previous content, or nothing if
    // the clipboard couldn't be opened.
    std::optional<Snapshot> replaceWithPrivateText(const QString &text);

    // Puts |previous| back, or empties the clipboard if it was empty.
    // Also marked private: it's already in history from when it was
    // first copied.
    bool restore(const Snapshot &previous);
}
