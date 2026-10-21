#include "recordingflow.h"

using namespace RecordingFlow;

Action RecordingFlow::decide(State state, Input input, State pending)
{
    switch (input) {
    case Input::Tap:
        switch (state) {
        case State::Idle:            return Action::StartBuffer;
        case State::RecordingBuffer: return Action::FinishToTab;
        case State::Loading:         return Action::ReportLoading;
        case State::Transcribing:    return Action::ReportTranscribing;
        case State::RecordingLive:   return Action::None;
        }
        break;

    case Input::HoldStart:
        return state == State::Idle ? Action::StartLive : Action::None;

    case Input::HoldRelease:
        switch (state) {
        case State::RecordingLive:   return Action::FinishToCursor;
        // A slow press while buffer-recording is still a stop.
        case State::RecordingBuffer: return Action::FinishToTab;
        // Released before the model was ready: drop the queued live
        // recording, or it would start with no key held to stop it.
        case State::Loading:
            return pending == State::RecordingLive ? Action::CancelPendingLive
                                                   : Action::None;
        default:                     return Action::None;
        }

    case Input::Stop:
        return (state == State::RecordingBuffer || state == State::RecordingLive)
            ? Action::FinishToTab : Action::None;
    }
    return Action::None;
}
