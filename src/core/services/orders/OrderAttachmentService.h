#pragma once

#include <QHash>
#include <QList>
#include <QString>

class QSqlDatabase;

namespace OrderAttachmentService {
void removeUnreferencedManagedFile(const QString& path);


struct AttachmentInfo {
    QString filePath;
    QString displayName;
    QString invoiceNumber;
    QString invoiceDate;
};

QString serviceAttachmentPath(QSqlDatabase db, int serviceId, QString* displayName = nullptr);
QString partAttachmentPath(QSqlDatabase db,
                           int partId,
                           QString* displayName = nullptr,
                           QString* invoiceNumber = nullptr,
                           QString* invoiceDate = nullptr);

QHash<int, AttachmentInfo> serviceAttachmentMap(QSqlDatabase db, const QList<int>& serviceIds);
QHash<int, AttachmentInfo> partAttachmentMap(QSqlDatabase db, const QList<int>& partIds);
void removeUnreferencedManagedFile(QSqlDatabase db, const QString& path);

bool setServiceAttachment(QSqlDatabase db,
                          int serviceId,
                          const QString& filePath,
                          const QString& displayName,
                          QString* errorText = nullptr);

// An invoice number may be stored without a file; empty file and number remove the link.
bool setPartAttachment(QSqlDatabase db,
                       int partId,
                       const QString& filePath,
                       const QString& displayName,
                       const QString& invoiceNumber = QString(),
                       const QString& invoiceDate = QString(),
                       QString* errorText = nullptr, bool confirmedImport = false, double confirmedAmount = -1);

} // namespace OrderAttachmentService
