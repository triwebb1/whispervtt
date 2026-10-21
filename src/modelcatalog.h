#pragma once

#include <QString>
#include <QStringList>

// Which Whisper model to use and which to recommend.  Pure functions of
// their inputs, so they are unit-tested without touching the disk.
namespace ModelCatalog {
    // Configured on first run.
    inline constexpr char kDefaultModel[] = "ggml-large-v3-turbo.bin";

    // The model to load from the |present| files in the models folder:
    // the |configured| one if it's there, otherwise the best present
    // model by preference (turbo first), otherwise the alphabetically
    // first .bin.  Returns |configured| when nothing is present.
    QString choose(const QStringList &present, const QString &configured);

    struct Recommendation {
        QString file;
        QString size;
        QString reason;
    };

    // large-v3-turbo with a usable GPU, small.en on CPU-only machines.
    Recommendation recommend(bool gpu);

    // True if |head| (the first bytes of a file) starts like a whisper.cpp
    // model.  Rejects placeholders that browsers create before a download
    // has finished.
    bool hasModelHeader(const QByteArray &head);

    QString downloadUrl(const QString &file);
    QString listUrl();
}
