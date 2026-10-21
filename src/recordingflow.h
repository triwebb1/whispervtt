#pragma once

// What a hotkey or button press means in each recording state (SPEC §3).
// Pure decision logic with no I/O, so every transition is unit-tested;
// App carries out the returned action.
namespace RecordingFlow {
    enum class State { Idle, Loading, RecordingBuffer, RecordingLive, Transcribing };

    enum class Input {
        Tap,           // F9 tapped, or the Record button
        HoldStart,     // F9 held past the tap threshold
        HoldRelease,   // F9 released after a hold
        Stop,          // the Stop button
    };

    enum class Action {
        None,
        StartBuffer,         // start a buffer recording (model loaded first if needed)
        StartLive,           // start a live recording (model loaded first if needed)
        FinishToTab,         // stop; deliver to the editor or log
        FinishToCursor,      // stop; paste at the cursor
        CancelPendingLive,   // hold released before the model finished loading
        ReportLoading,       // busy: still loading the model
        ReportTranscribing,  // busy: a transcription is in progress
    };

    // |pending| is the recording queued while the model loads (Idle if none).
    Action decide(State state, Input input, State pending = State::Idle);
}
