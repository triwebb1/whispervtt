#include "modelcatalog.h"

QString ModelCatalog::choose(const QStringList &present, const QString &configured)
{
    if (present.contains(configured, Qt::CaseInsensitive))
        return configured;

    static const char *preference[] = {
        "ggml-large-v3-turbo.bin", "ggml-large-v3-turbo-q8_0.bin",
        "ggml-large-v3-turbo-q5_0.bin", "ggml-large-v3.bin",
        "ggml-medium.en.bin", "ggml-medium.bin",
        "ggml-small.en.bin", "ggml-base.en.bin",
    };
    for (const char *p : preference)
        if (present.contains(QLatin1String(p), Qt::CaseInsensitive))
            return QString::fromLatin1(p);

    QStringList sorted = present;
    sorted.sort(Qt::CaseInsensitive);
    return sorted.isEmpty() ? configured : sorted.first();
}

ModelCatalog::Recommendation ModelCatalog::recommend(bool gpu)
{
    if (gpu)
        return {QStringLiteral("ggml-large-v3-turbo.bin"), QStringLiteral("1.6 GB"),
                QStringLiteral("the best balance of accuracy and speed on your graphics card")};
    return {QStringLiteral("ggml-small.en.bin"), QStringLiteral("488 MB"),
            QStringLiteral("good accuracy at interactive speed without a graphics card")};
}

bool ModelCatalog::hasModelHeader(const QByteArray &head)
{
    // GGML_FILE_MAGIC (0x67676d6c), little-endian.
    return head.startsWith("lmgg");
}

QString ModelCatalog::downloadUrl(const QString &file)
{
    return QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/") + file;
}

QString ModelCatalog::listUrl()
{
    return QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/tree/main");
}
