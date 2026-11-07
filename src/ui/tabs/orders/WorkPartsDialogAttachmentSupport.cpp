#include "ui/tabs/orders/WorkPartsDialogAttachmentSupport.h"
#include "core/io/SafeFileOps.h"
#include "app/App.h"
#include "core/services/storage/StorageDirectoriesGuard.h"
#include "core/services/orders/OrderAttachmentService.h"


#include <QDesktopServices>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>
#include <QUuid>

namespace WorkPartsDialogAttachmentSupport {


QString attachmentBaseDir(int orderNumber, const QString& bucket)
{
    QString errorText;
    const auto kind = bucket == QStringLiteral("services")
        ? StorageDirectoriesGuard::StorageKind::OrderPhoto
        : StorageDirectoriesGuard::StorageKind::Invoice;
    return StorageDirectoriesGuard::managedSubdir(kind, orderNumber, bucket, &errorText);
}

bool isImageAttachment(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QStringLiteral("png") || ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg") ||
           ext == QStringLiteral("bmp") || ext == QStringLiteral("webp") || ext == QStringLiteral("gif");
}

QString copiedAttachmentPath(int orderNumber, const QString& bucket, int ownerId, const QString& sourcePath, QString* errorText)
{
    if (ownerId <= 0 || sourcePath.trimmed().isEmpty()) return QString();
    QFileInfo fi(sourcePath);
    if (!fi.exists() || !fi.isFile()) {
        if (errorText) *errorText = QString::fromUtf8("Файл не знайдено: %1").arg(sourcePath);
        return QString();
    }
    const auto kind = bucket == QStringLiteral("services")
        ? StorageDirectoriesGuard::StorageKind::OrderPhoto
        : StorageDirectoriesGuard::StorageKind::Invoice;
    return StorageDirectoriesGuard::copyFileToManagedStorage(kind, orderNumber, bucket, ownerId, fi.absoluteFilePath(), errorText);
}

void removeAttachmentFileIfManaged(const QString& path)
{
    if (path.trimmed().isEmpty()) return;
    OrderAttachmentService::removeUnreferencedManagedFile(path);
}


AttachmentDialogAction runAttachmentPreviewDialog(QWidget* parent, const QString& title, const QString& currentPath, QString* selectedSourcePath)
{
    if (selectedSourcePath) selectedSourcePath->clear();
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setModal(true);
    dlg.resize(920, 680);

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto* head = new QLabel(title, &dlg);
    QFont hf = head->font();
    hf.setPointSizeF(qMax<qreal>(8.0, (hf.pointSizeF() > 0.0 ? hf.pointSizeF() : 10.0) + 3.0));
    hf.setBold(true);
    head->setFont(hf);
    root->addWidget(head);

    auto* hint = new QLabel(currentPath.trimmed().isEmpty()
        ? QString::fromUtf8("Файл ще не прикріплено.")
        : QString::fromUtf8("Перегляд прикріпленого файлу."), &dlg);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* sc = new QScrollArea(&dlg);
    sc->setWidgetResizable(true);
    auto* holder = new QWidget(sc);
    auto* holderLay = new QVBoxLayout(holder);
    holderLay->setContentsMargins(8, 8, 8, 8);
    auto* imageLabel = new QLabel(holder);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setMinimumSize(620, 420);
    imageLabel->setStyleSheet(QStringLiteral("QLabel { background: rgba(255,255,255,0.96); border: 1px solid rgba(40,45,54,0.32); border-radius: 10px; color: #111111; }"));
    imageLabel->setWordWrap(true);
    holderLay->addWidget(imageLabel);
    holder->setLayout(holderLay);
    sc->setWidget(holder);
    root->addWidget(sc, 1);

    const QString absPath = QFileInfo(currentPath).exists() ? QFileInfo(currentPath).absoluteFilePath() : QString();
    if (!absPath.isEmpty() && isImageAttachment(absPath)) {
        QPixmap pm(absPath);
        if (!pm.isNull()) imageLabel->setPixmap(pm.scaled(840, 560, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        else imageLabel->setText(QString::fromUtf8("Не вдалося відкрити зображення.\n%1").arg(absPath));
    } else if (!absPath.isEmpty()) {
        imageLabel->setText(QString::fromUtf8("Файл прикріплено:\n%1").arg(absPath));
    } else {
        imageLabel->setText(QString::fromUtf8("Файл не вибрано."));
    }

    AttachmentDialogAction action = AttachmentDialogAction::None;
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* btnOpen = new QPushButton(QString::fromUtf8("Відкрити файл"), &dlg);
    auto* btnChoose = new QPushButton(absPath.isEmpty() ? QString::fromUtf8("Додати файл") : QString::fromUtf8("Замінити файл"), &dlg);
    auto* btnRemove = new QPushButton(QString::fromUtf8("Видалити"), &dlg);
    auto* btnClose = new QPushButton(QString::fromUtf8("Закрити"), &dlg);
    btnOpen->setEnabled(!absPath.isEmpty());
    btnRemove->setEnabled(!absPath.isEmpty());
    buttons->addWidget(btnOpen);
    buttons->addWidget(btnChoose);
    buttons->addWidget(btnRemove);
    buttons->addWidget(btnClose);
    root->addLayout(buttons);

    QObject::connect(btnOpen, &QPushButton::clicked, &dlg, [absPath]() {
        if (!absPath.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(absPath));
        }
    });
    QObject::connect(btnChoose, &QPushButton::clicked, &dlg, [&]() {
        const QString source = QFileDialog::getOpenFileName(&dlg, QString::fromUtf8("Вибрати файл"), QString(),
            QString::fromUtf8("Підтримувані файли (*.pdf *.xlsx *.xls *.xlsm *.csv *.txt *.png *.jpg *.jpeg *.bmp *.webp *.gif);;PDF (*.pdf);;Excel/CSV (*.xlsx *.xls *.xlsm *.csv *.txt);;Зображення (*.png *.jpg *.jpeg *.bmp *.webp *.gif);;Усі файли (*.*)"));
        if (source.trimmed().isEmpty()) return;
        if (selectedSourcePath) *selectedSourcePath = source;
        action = AttachmentDialogAction::SetFile;
        dlg.accept();
    });
    QObject::connect(btnRemove, &QPushButton::clicked, &dlg, [&]() { action = AttachmentDialogAction::RemoveFile; dlg.accept(); });
    QObject::connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);

    dlg.exec();
    return action;
}

} // namespace WorkPartsDialogAttachmentSupport
