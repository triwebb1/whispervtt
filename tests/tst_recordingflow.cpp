#include "recordingflow.h"

#include <QTest>

using namespace RecordingFlow;

Q_DECLARE_METATYPE(RecordingFlow::State)
Q_DECLARE_METATYPE(RecordingFlow::Input)
Q_DECLARE_METATYPE(RecordingFlow::Action)

// Every state × input combination from SPEC §3.
class TestRecordingFlow : public QObject {
    Q_OBJECT
private slots:
    void decide_data();
    void decide();
};

void TestRecordingFlow::decide_data()
{
    QTest::addColumn<State>("state");
    QTest::addColumn<Input>("input");
    QTest::addColumn<State>("pending");
    QTest::addColumn<Action>("expected");

    const State none = State::Idle;

    // RS-1: Idle — tap records into the buffer, hold records live.
    QTest::newRow("idle: tap")          << State::Idle << Input::Tap         << none << Action::StartBuffer;
    QTest::newRow("idle: hold")         << State::Idle << Input::HoldStart   << none << Action::StartLive;
    QTest::newRow("idle: release")      << State::Idle << Input::HoldRelease << none << Action::None;
    QTest::newRow("idle: stop")         << State::Idle << Input::Stop        << none << Action::None;

    // RS-2: Loading — busy; releasing a queued hold cancels it.
    QTest::newRow("loading: tap")       << State::Loading << Input::Tap       << none << Action::ReportLoading;
    QTest::newRow("loading: hold")      << State::Loading << Input::HoldStart << none << Action::None;
    QTest::newRow("loading: stop")      << State::Loading << Input::Stop      << none << Action::None;
    QTest::newRow("loading: release, live queued")
        << State::Loading << Input::HoldRelease << State::RecordingLive   << Action::CancelPendingLive;
    QTest::newRow("loading: release, buffer queued")
        << State::Loading << Input::HoldRelease << State::RecordingBuffer << Action::None;
    QTest::newRow("loading: release, nothing queued")
        << State::Loading << Input::HoldRelease << none                   << Action::None;

    // RS-3: buffer recording — tap, a slow press or Stop all finish it.
    QTest::newRow("buffer: tap")        << State::RecordingBuffer << Input::Tap         << none << Action::FinishToTab;
    QTest::newRow("buffer: hold")       << State::RecordingBuffer << Input::HoldStart   << none << Action::None;
    QTest::newRow("buffer: release")    << State::RecordingBuffer << Input::HoldRelease << none << Action::FinishToTab;
    QTest::newRow("buffer: stop")       << State::RecordingBuffer << Input::Stop        << none << Action::FinishToTab;

    // RS-4: live recording — release pastes, Stop delivers to the tab.
    QTest::newRow("live: tap")          << State::RecordingLive << Input::Tap         << none << Action::None;
    QTest::newRow("live: hold")         << State::RecordingLive << Input::HoldStart   << none << Action::None;
    QTest::newRow("live: release")      << State::RecordingLive << Input::HoldRelease << none << Action::FinishToCursor;
    QTest::newRow("live: stop")         << State::RecordingLive << Input::Stop        << none << Action::FinishToTab;

    // RS-5: transcribing — no new recordings.
    QTest::newRow("transcribing: tap")     << State::Transcribing << Input::Tap         << none << Action::ReportTranscribing;
    QTest::newRow("transcribing: hold")    << State::Transcribing << Input::HoldStart   << none << Action::None;
    QTest::newRow("transcribing: release") << State::Transcribing << Input::HoldRelease << none << Action::None;
    QTest::newRow("transcribing: stop")    << State::Transcribing << Input::Stop        << none << Action::None;
}

void TestRecordingFlow::decide()
{
    QFETCH(State, state);
    QFETCH(Input, input);
    QFETCH(State, pending);
    QFETCH(Action, expected);
    QCOMPARE(RecordingFlow::decide(state, input, pending), expected);
}

QTEST_APPLESS_MAIN(TestRecordingFlow)
#include "tst_recordingflow.moc"
