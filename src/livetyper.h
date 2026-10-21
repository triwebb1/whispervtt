#pragma once

#include <QString>
#include <functional>
#include <windows.h>

class QObject;

// Pastes text at the current OS cursor position.
//
// Restores focus to the target window first (in case our app stole
// it), then uses the clipboard + Ctrl-V.  The dictated text is marked
// private (kept out of clipboard history, cloud sync and clipboard
// managers), and afterwards the user's previous clipboard content, in
// every format, is put back, unless something else changed the
// clipboard in the meantime.
//
// Asynchronous: returns immediately and drives the steps with timers on
// |context|'s thread, so the GUI never blocks.  |done| is called with
// false if the clipboard couldn't be set (nothing is pasted then).
namespace LiveTyper {
    void typeText(const QString &text, HWND targetWindow, QObject *context,
                  std::function<void(bool ok)> done);
}
