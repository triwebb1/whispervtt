#pragma once

#include <QString>

class QWidget;

// First-run help shown when no Whisper model is installed: explains what
// is needed, recommends a model for this machine, and opens the download
// link and the models folder.  The app itself never downloads anything.
namespace ModelSetup {
    // Non-modal; deletes itself when closed.  Returns the dialog so the
    // caller can close it once a model arrives.
    QWidget *show(const QString &modelsDir, bool gpu);
}
