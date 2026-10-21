#pragma once

#include <QObject>
#include <QTimer>

#include <windows.h>
#include <atomic>
#include <thread>

// Detects global F9 key events via a low-level keyboard hook.
//
//   tap   (press + release < 400 ms) → tapped()
//   hold  (press held ≥ 400 ms)      → holdStarted()  …  holdReleased()
//
// The hook is installed/removed with start()/stop().  A plain F9 is
// consumed (not passed on to the foreground app); F9 with a modifier
// held, and all other keys, pass through.
//
// The hook runs on its own thread with a dedicated message loop.  A
// low-level hook is called on the thread that installed it, and if that
// thread doesn't respond within LowLevelHooksTimeout, Windows silently
// removes the hook (F9 just stops working).  Keeping it off the GUI
// thread means a busy GUI can never cause that.
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    static constexpr int kHoldThresholdMs = 400;

    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    bool start();
    void stop();

signals:
    void tapped();
    void holdStarted();
    void holdReleased();

private:
    void onKeyDown();
    void onKeyUp();
    void onHoldThreshold();

    static LRESULT CALLBACK hookProc(int nCode, WPARAM wParam,
                                     LPARAM lParam);
    void hookThreadMain(HANDLE readyEvent);

    std::thread         m_thread;
    std::atomic<DWORD>  m_threadId{0};
    std::atomic<bool>   m_hookOk{false};

    QTimer m_holdTimer;
    bool   m_pressed = false;
    bool   m_held    = false;

    static HotkeyManager *s_instance; // singleton for the C callback
};
