#pragma once

// Tone playback via Windows PlaySound.
// Volume is 0–100 and persisted via QSettings.

namespace Tones {
    void playLoading();
    void playReady();
    void stop();

    // Volume 0–100.  Changes take effect on next play call.
    int  volume();
    void setVolume(int v);   // persists to QSettings
}
