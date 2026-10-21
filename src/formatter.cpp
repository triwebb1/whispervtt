#include "formatter.h"
#include "textcleanup.h"

#include <QRegularExpression>
#include <QStringList>
#include <vector>

// ── voice-command replacements ───────────────────────────────────────

struct VoiceCmd {
    QRegularExpression re;
    QString replacement;
};

enum class Where { Anywhere, SentenceStart };

static const std::vector<VoiceCmd> &voiceCmds()
{
    // Whisper punctuates a spoken command like any other words ("Hello.
    // New paragraph. World."), so each pattern also swallows the
    // punctuation around it.  Commands that are also everyday words
    // ("heading home", "job title", "bite the bullet") only count at the
    // start of a sentence or after a comma.
    auto cmd = [](Where where, const char *words, const char *replacement) {
        const QString lead = where == Where::Anywhere
            ? QStringLiteral("[,;:]?\\s*")
            : QStringLiteral("(?:^|(?<=[.!?\\n])|[,;:])\\s*");
        return VoiceCmd{
            QRegularExpression(lead + QStringLiteral("\\b%1\\b[.,;:!?]*\\s*")
                                          .arg(QLatin1String(words)),
                               QRegularExpression::CaseInsensitiveOption),
            QString::fromLatin1(replacement)};
    };
    static const std::vector<VoiceCmd> v = {
        cmd(Where::Anywhere,      "new paragraph",        "\n\n"),
        cmd(Where::Anywhere,      "new line",             "\n"),
        cmd(Where::Anywhere,      "line break",           "\n"),
        cmd(Where::SentenceStart, "sub\\s*heading",       "\n\n## "),
        cmd(Where::SentenceStart, "heading",              "\n\n# "),
        cmd(Where::SentenceStart, "title",                "\n\n# "),
        cmd(Where::SentenceStart, "bullet(?:\\s*point)?", "\n- "),
        cmd(Where::SentenceStart, "dash",                 "\n- "),
    };
    return v;
}

// ── ordinal detection ────────────────────────────────────────────────

static const QStringList &ordinals()
{
    static const QStringList v = {
        QStringLiteral("first"),   QStringLiteral("second"), QStringLiteral("third"),
        QStringLiteral("fourth"),  QStringLiteral("fifth"),  QStringLiteral("sixth"),
        QStringLiteral("seventh"), QStringLiteral("eighth"), QStringLiteral("ninth"),
        QStringLiteral("tenth"),
    };
    return v;
}

// An ordinal at the start of a line, plus the punctuation after it.
static const QRegularExpression &leadingOrdinalRe()
{
    static const QRegularExpression re(
        QStringLiteral("^(?:number\\s+)?(?:%1)\\b\\s*[,:;.]?\\s*")
            .arg(ordinals().join(QLatin1Char('|'))),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

static const QRegularExpression &digitPrefixRe()
{
    static const QRegularExpression re(QStringLiteral("^\\d+[.)]\\s*"));
    return re;
}

// Position of ordinal |k| ("first" = 0) where it opens a sentence, at or
// after |from|; -1 if there is none.  Only sentence-initial ordinals
// count, so "wait a second" never starts a list item.
static qsizetype findSentenceOrdinal(const QString &s, int k, qsizetype from)
{
    const QRegularExpression re(
        QStringLiteral("(?:^|(?<=[.!?;]\\s))((?:number\\s+)?%1)\\b").arg(ordinals().at(k)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(s, from);
    return m.hasMatch() ? m.capturedStart(1) : -1;
}

// ── passes ───────────────────────────────────────────────────────────

static QString applyVoiceCommands(const QString &text)
{
    QString t = text;
    for (const auto &cmd : voiceCmds())
        t.replace(cmd.re, cmd.replacement);
    return t;
}

// "Intro. First, a. Second, b." -> "Intro.", "", "First, a.", "Second, b."
//
// A list needs "first" followed by "second" (and optionally "third", …)
// in order, each opening a sentence.  The last item runs to the end of
// the line.
static QStringList splitInlineLists(const QStringList &lines)
{
    QStringList out;
    for (const QString &line : lines) {
        const qsizetype start = findSentenceOrdinal(line, 0, 0);
        QList<qsizetype> cuts;
        if (start >= 0) {
            cuts.append(start);
            for (int k = 1; k < ordinals().size(); ++k) {
                const qsizetype at = findSentenceOrdinal(line, k, cuts.last() + 1);
                if (at < 0) break;
                cuts.append(at);
            }
        }
        if (cuts.size() < 2) {
            out.append(line);
            continue;
        }

        const QString intro = line.left(start).trimmed();
        if (!intro.isEmpty()) out << intro << QString();
        cuts.append(line.size());
        for (qsizetype i = 0; i + 1 < cuts.size(); ++i)
            out.append(line.mid(cuts[i], cuts[i + 1] - cuts[i]).trimmed());
    }
    return out;
}

// Consecutive lines that open with an ordinal become "1. …", "2. …".  A
// single such line is left alone: "First of all, …" is prose, not a list.
static QStringList autoNumberOrdinals(const QStringList &lines)
{
    auto isItem = [](const QString &line) {
        const QString s = line.trimmed();
        return !digitPrefixRe().match(s).hasMatch() &&
               leadingOrdinalRe().match(s).hasMatch();
    };

    QStringList out = lines;
    for (qsizetype i = 0; i < lines.size();) {
        qsizetype end = i;
        while (end < lines.size() && isItem(lines[end])) ++end;
        if (end - i >= 2) {
            for (qsizetype k = i; k < end; ++k) {
                const QString s = lines[k].trimmed();
                const QString rest = s.mid(leadingOrdinalRe().match(s).capturedEnd());
                out[k] = QStringLiteral("%1. %2")
                             .arg(k - i + 1)
                             .arg(TextCleanup::capitalizeFirstWord(rest));
            }
        }
        i = (end > i) ? end : i + 1;
    }
    return out;
}

static QStringList breakLongParagraphs(const QStringList &lines)
{
    static const QRegularExpression sentEnd(
        QStringLiteral("(?<=[.!?])\\s+"));

    QStringList out;
    for (const QString &line : lines) {
        const QString s = line.trimmed();
        if (s.length() <= 300 || s.startsWith(QLatin1Char('#')) ||
            s.startsWith(QLatin1Char('-')) || s.startsWith(QLatin1Char('*')) ||
            digitPrefixRe().match(s).hasMatch()) {
            out.append(line);
            continue;
        }
        QStringList sentences = s.split(sentEnd);
        if (sentences.size() <= 4) {
            out.append(line);
            continue;
        }
        QStringList group;
        for (int i = 0; i < sentences.size(); ++i) {
            group.append(sentences[i]);
            if (group.size() >= 3 && i < sentences.size() - 1) {
                out.append(group.join(QLatin1Char(' ')));
                out.append(QString());
                group.clear();
            }
        }
        if (!group.isEmpty())
            out.append(group.join(QLatin1Char(' ')));
    }
    return out;
}

static QString cleanup(const QString &text)
{
    static const QRegularExpression markerRe(QStringLiteral("^(#{1,6} |- |\\d+\\. )?(.*)$"));

    QStringList lines;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        // Capitalise the first word of every line, after any Markdown
        // marker ("# intro" -> "# Intro", "- apples" -> "- Apples").
        const QRegularExpressionMatch m = markerRe.match(line.trimmed());
        const QString marker = m.captured(1);
        QString body = TextCleanup::capitalizeFirstWord(m.captured(2));
        // Headings don't end in a full stop ("# Open questions." ->
        // "# Open questions"); an ellipsis or other punctuation stays.
        if (marker.startsWith(QLatin1Char('#')) && body.endsWith(QLatin1Char('.')) &&
            !body.endsWith(QLatin1String("..")))
            body.chop(1);
        lines.append(marker + body);
    }

    QString t = lines.join(QLatin1Char('\n'));
    static const QRegularExpression blankRuns(QStringLiteral("\\n{3,}"));
    t.replace(blankRuns, QStringLiteral("\n\n"));
    return t.trimmed();
}

// ── Formatter ────────────────────────────────────────────────────────

QString Formatter::format(const QString &text)
{
    if (text.trimmed().isEmpty()) return text;

    QStringList lines = applyVoiceCommands(text).split(QLatin1Char('\n'));
    lines = splitInlineLists(lines);
    lines = autoNumberOrdinals(lines);
    lines = breakLongParagraphs(lines);
    return cleanup(lines.join(QLatin1Char('\n')));
}
