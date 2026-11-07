#pragma once

#include <QString>
#include <QWidget>

namespace WorkPartsDialogAttachmentSupport {

QString attachmentBaseDir(int orderNumber, const QString& bucket);
bool isImageAttachment(const QString& path);
QString copiedAttachmentPath(int orderNumber, const QString& bucket, int ownerId, const QString& sourcePath, QString* errorText = nullptr);
void removeAttachmentFileIfManaged(const QString& path);


enum class AttachmentDialogAction { None, SetFile, RemoveFile };
AttachmentDialogAction runAttachmentPreviewDialog(QWidget* parent, const QString& title, const QString& currentPath, QString* selectedSourcePath);

} // namespace WorkPartsDialogAttachmentSupport
