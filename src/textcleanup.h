#pragma once

#include <QString>
#include <QStringList>

// Pure text clean-up applied to whisper's raw segment output.
namespace TextCleanup {
    // True for segments whisper emits on silence or noise rather than
    // speech: bracketed tags ("[BLANK_AUDIO]", "(wind blowing)") and bare
    // punctuation ("...").
    bool isNonSpeech(const QString &segment);

    // Upper-cases the first letter of |text|, unless the first word is
    // deliberately mixed case ("iPhone", "eBay").
    QString capitalizeFirstWord(const QString &text);

    // Joins the segments of one dictation into a single string, dropping
    // non-speech segments.  Each dictation is a new sentence, so the
    // result starts with a capital letter.
    QString assemble(const QStringList &segments);
}
