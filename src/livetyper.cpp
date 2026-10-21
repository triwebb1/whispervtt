#include "livetyper.h"
#include "clipboard.h"
#include "debuglog.h"

#include <QObject>
#include <QTimer>
#include <memory>

#define LOG(msg) DLOG("typer", msg)

static void sendCtrlV()
{
    INPUT inputs[4] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    if (SendInput(4, inputs, sizeof(INPUT)) != 4)
        LOG("SendInput blocked (target window elevated?)");
}

static bool modifiersHeld()
{
    for (int vk : {VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN})
        if (GetAsyncKeyState(vk) & 0x8000) return true;
    return false;
}

namespace {
struct PasteJob {
    QString text;
    QObject *context;
    std::function<void(bool)> done;
};
}

static void pasteStep(std::shared_ptr<PasteJob> job, int triesLeft)
{
    // Wait (bounded) until the user isn't holding Shift/Alt/etc, which
    // would turn our Ctrl+V into a different shortcut.
    if (modifiersHeld() && triesLeft > 0) {
        QTimer::singleShot(50, job->context,
                           [job, triesLeft]{ pasteStep(job, triesLeft - 1); });
        return;
    }

    const std::optional<Clipboard::Snapshot> previous =
        Clipboard::replaceWithPrivateText(job->text);
    if (!previous) {
        job->done(false);
        return;
    }
    const DWORD ourSeq = GetClipboardSequenceNumber();
    sendCtrlV();

    // Put the user's clipboard back once the target app has had time to
    // read ours (Electron apps and RDP sessions read lazily, so be
    // generous), unless something else has replaced it meanwhile.  The
    // dictated text never stays on the clipboard.
    QTimer::singleShot(500, job->context, [job, previous, ourSeq]{
        if (GetClipboardSequenceNumber() == ourSeq)
            Clipboard::restore(*previous);
        job->done(true);
    });
}

void LiveTyper::typeText(const QString &text, HWND targetWindow,
                         QObject *context, std::function<void(bool)> done)
{
    // Restore focus to the window the user was in before recording.
    int focusDelay = 30;
    if (targetWindow && IsWindow(targetWindow) &&
        GetForegroundWindow() != targetWindow) {
        SetForegroundWindow(targetWindow);
        focusDelay = 120;
    }

    auto job = std::make_shared<PasteJob>(PasteJob{text, context, std::move(done)});
    QTimer::singleShot(focusDelay, context, [job]{ pasteStep(job, 20); });
}
