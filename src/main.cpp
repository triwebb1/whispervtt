#include "app.h"

#include <QApplication>
#include <QMessageBox>
#include <windows.h>

int main(int argc, char *argv[])
{
    QApplication qapp(argc, argv);
    // Also the settings location: HKCU\Software\WhisperVTT\WhisperVTT
    qapp.setOrganizationName(QStringLiteral("WhisperVTT"));
    qapp.setApplicationName(QStringLiteral("WhisperVTT"));
    qapp.setApplicationVersion(QStringLiteral(WHISPER_VTT_VERSION));
    qapp.setQuitOnLastWindowClosed(false);

    // One instance per user session: a second copy would fight over F9,
    // load the model twice and overwrite the first one's log.  Windows
    // releases the mutex when the process exits.
    CreateMutexW(nullptr, FALSE, L"Local\\WhisperVTT-SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        QMessageBox::information(nullptr, QStringLiteral("WhisperVTT"),
            QStringLiteral("WhisperVTT is already running.\n\n"
                           "Look for the microphone icon in the system tray."));
        return 0;
    }

    App app;
    app.init();

    return qapp.exec();
}
