#pragma once

#include "core/services/MutationResult.h"

#include <QString>

class OrderSyncImportService
{
public:
    static MutationResult importPartsFromPendingPayloadDetailed(int orderNumber,
                                                                const QString& payloadFilePath,
                                                                bool importParts,
                                                                int* importedPartsCount = nullptr,
                                                                QString* errorText = nullptr);
    static bool importPartsFromPendingPayload(int orderNumber,
                                              const QString& payloadFilePath,
                                              bool importParts,
                                              int* importedPartsCount = nullptr,
                                              QString* errorText = nullptr);
};
