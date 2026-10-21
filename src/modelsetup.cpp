#include "modelsetup.h"
#include "modelcatalog.h"

#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>

QWidget *ModelSetup::show(const QString &modelsDir, bool gpu)
{
    const ModelCatalog::Recommendation rec = ModelCatalog::recommend(gpu);
    const QString folder = QDir::toNativeSeparators(modelsDir);

    auto *box = new QMessageBox(QMessageBox::Information,
                                QStringLiteral("WhisperVTT — Speech model needed"),
                                QStringLiteral("<b>Download a speech model to start dictating.</b>"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setTextFormat(Qt::RichText);
    box->setInformativeText(QStringLiteral(
        "WhisperVTT turns speech into text entirely on this PC — your voice "
        "never leaves it. It needs a Whisper model file, downloaded once.<br><br>"
        "Recommended: <b>%1</b> (%2) — %3.<br><br>"
        "Save it into the models folder:<br><code>%4</code><br><br>"
        "It is picked up automatically as soon as it arrives. Other sizes are "
        "listed <a href=\"%5\">here</a>; switch any time under Settings → Model.")
        .arg(rec.file, rec.size, rec.reason, folder.toHtmlEscaped(),
             ModelCatalog::listUrl()));

    QPushButton *get = box->addButton(QStringLiteral("Download && Open Folder"),
                                      QMessageBox::AcceptRole);
    box->addButton(QMessageBox::Close);
    box->setDefaultButton(get);

    QObject::connect(get, &QPushButton::clicked, box, [rec, modelsDir]{
        QDesktopServices::openUrl(QUrl(ModelCatalog::downloadUrl(rec.file)));
        QDesktopServices::openUrl(QUrl::fromLocalFile(modelsDir));
    });

    box->show();
    box->raise();
    box->activateWindow();
    return box;
}
