#include "hotkeymanager.h"
#include "debuglog.h"

#define LOG(msg) DLOG("hotkey", msg)

HotkeyManager *HotkeyManager::s_instance = nullptr;

// ── construction / destruction ───────────────────────────────────────

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    s_instance = this;

    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(kHoldThresholdMs);
    connect(&m_holdTimer, &QTimer::timeout,
            this, &HotkeyManager::onHoldThreshold);
}

HotkeyManager::~HotkeyManager()
{
    stop();
    if (s_instance == this) s_instance = nullptr;
}

// ── start / stop ─────────────────────────────────────────────────────

bool HotkeyManager::start()
{
    if (m_thread.joinable()) return m_hookOk;

    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_thread = std::thread(&HotkeyManager::hookThreadMain, this, ready);
    WaitForSingleObject(ready, 5000);
    CloseHandle(ready);

    LOG(QStringLiteral("hook installed: %1").arg(m_hookOk.load()));
    return m_hookOk;
}

void HotkeyManager::stop()
{
    if (!m_thread.joinable()) return;
    if (DWORD tid = m_threadId.load())
        PostThreadMessageW(tid, WM_QUIT, 0, 0);
    m_thread.join();
    m_threadId = 0;
    m_hookOk   = false;
}

void HotkeyManager::hookThreadMain(HANDLE readyEvent)
{
    // Force creation of this thread's message queue before announcing
    // the thread id, so PostThreadMessage(WM_QUIT) can't be lost.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    m_threadId = GetCurrentThreadId();

    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, hookProc,
                                   GetModuleHandleW(nullptr), 0);
    m_hookOk = (hook != nullptr);
    SetEvent(readyEvent);
    if (!hook) return;

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(hook);
}

LRESULT CALLBACK HotkeyManager::hookProc(int nCode, WPARAM wParam,
                                          LPARAM lParam)
{
    // Runs on the hook thread — must return quickly and never touch
    // QObject state directly.  Hand events to the GUI thread instead.
    if (nCode == HC_ACTION && s_instance) {
        auto *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
        if (kb->vkCode == VK_F9 && !(kb->flags & LLKHF_INJECTED)) {
            const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

            // Ctrl/Shift/Alt/Win+F9 belong to the foreground app (they
            // are common IDE and Office shortcuts).  Decided on the first
            // key-down and kept until key-up, so a press is never split
            // between us and the app.
            static bool inPress = false, passThrough = false;   // hook thread only
            if (down && !inPress) {
                inPress = true;
                passThrough = false;
                for (int vk : {VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN})
                    if (GetAsyncKeyState(vk) & 0x8000) passThrough = true;
            }
            if (!down) inPress = false;
            if (passThrough)
                return CallNextHookEx(nullptr, nCode, wParam, lParam);

            if (down) {
                QMetaObject::invokeMethod(s_instance,
                    &HotkeyManager::onKeyDown, Qt::QueuedConnection);
            } else {
                QMetaObject::invokeMethod(s_instance,
                    &HotkeyManager::onKeyUp, Qt::QueuedConnection);
            }
            // Swallow F9 so the foreground app doesn't receive it (and,
            // in hold mode, a stream of auto-repeat F9 presses).
            return 1;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ── key logic ────────────────────────────────────────────────────────

void HotkeyManager::onKeyDown()
{
    if (m_pressed) return;   // ignore key-repeat
    m_pressed = true;
    m_held    = false;
    m_holdTimer.start();
}

void HotkeyManager::onKeyUp()
{
    if (!m_pressed) return;
    m_pressed = false;
    m_holdTimer.stop();

    if (m_held) {
        m_held = false;
        emit holdReleased();
    } else {
        emit tapped();
    }
}

void HotkeyManager::onHoldThreshold()
{
    m_held = true;
    emit holdStarted();
}
