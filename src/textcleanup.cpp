#include "textcleanup.h"

#include <QRegularExpression>
#include <algorithm>

bool TextCleanup::isNonSpeech(const QString &segment)
{
    static const QRegularExpression bracketed(
        QStringLiteral("^\\s*(?:[\\[\\(\\*][^\\]\\)\\*]*[\\]\\)\\*]\\s*)+$"));
    const bool hasWord = std::any_of(segment.begin(), segment.end(),
                                     [](QChar c){ return c.isLetterOrNumber(); });
    return !hasWord || bracketed.match(segment).hasMatch();
}

QString TextCleanup::capitalizeFirstWord(const QString &text)
{
    if (text.isEmpty() || !text.at(0).isLower()) return text;

    qsizetype end = 1;
    while (end < text.size() && text.at(end).isLetter()) ++end;
    const QString rest = text.mid(1, end - 1);
    if (rest != rest.toLower()) return text;

    QString out = text;
    out[0] = out.at(0).toUpper();
    return out;
}

QString TextCleanup::assemble(const QStringList &segments)
{
    QStringList kept;
    for (const QString &seg : segments) {
        const QString s = seg.trimmed();
        if (!isNonSpeech(s)) kept.append(s);
    }
    // Whisper sometimes lower-cases the first word of a clip that starts
    // right at the speech (as after VAD trimming).
    return capitalizeFirstWord(kept.join(QLatin1Char(' ')));
}
