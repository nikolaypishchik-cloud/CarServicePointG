#pragma once

#include "core/repository/PartRepository.h"

#include <QList>
#include <QString>

namespace SyncPayloadUtils {

struct SyncAttachmentRef {
    QString key;
    QString uri;
    QString localPath;
    bool isValid() const;
};

struct SyncPayload {
    QString sourceFile;
    QString schemaVersion;
    QString module;
    QString transport;
    QString supplier;
    QString targetType;
    QString targetId;
    QString plate;
    QString vin;
    QString car;
    QString client;
    QString phone;
    QString notes;
    QString documentNumber;
    QString documentDate;
    QList<WorkOrderPartRow> parts;
    QList<SyncAttachmentRef> attachments;

    bool isValid() const;
    bool targetsNewOrder() const;
    int targetOrderNumber() const;
};

QString defaultSyncInboxDir();
QString syncInboxDir();
bool syncModuleEnabled();

SyncPayload loadPayloadFromFile(const QString& filePath);
SyncPayload loadLatestPendingPayload();
QList<SyncPayload> listPendingPayloads(int limit = 100);

bool markPayloadProcessed(const QString& sourceFile);
bool processPayloadIntoOrder(const SyncPayload& payload,
                             int orderNumber,
                             QString* errorText = nullptr,
                             QString* infoText = nullptr);

QString payloadDisplayLabel(const SyncPayload& payload);
QString payloadPreviewText(const SyncPayload& payload);

} // namespace SyncPayloadUtils
