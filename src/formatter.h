#pragma once

#include <QString>

// Converts raw transcription text into Markdown.
//
// Two strategies:
//   1. Voice commands — spoken cues like "new paragraph", "heading",
//      "bullet point" that the user says while dictating.
//   2. Auto-detection — sentences introduced by sequential ordinals
//      ("First, … Second, …") become numbered lists; long blocks get
//      paragraph breaks.
namespace Formatter {
    QString format(const QString &text);
}
