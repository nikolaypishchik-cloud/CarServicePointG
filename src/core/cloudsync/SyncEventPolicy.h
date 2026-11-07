#pragma once

#include <QJsonObject>
#include <QStringList>

namespace SyncEventPolicy {
inline bool isLocalMetadata(const QString& type, const QJsonObject& envelope)
{
    // Only old audit envelopes with no business data may be retired automatically.
    static const QStringList auditTypes = {
        "schema", "registry", "router_cache", "workspace", "workspace_order_state",
        "catalog", "order", "order_graph", "work_order_batch", "part_attachment",
        "work_order_parts_batch", "work_order_parts_invoice_batch", "repair_ai_approved_parts"
    };
    const bool emptyDraftAudit = type == "work_order"
        && envelope.value("entity_uid").toString() == "-1"
        && envelope.value("source_module").toString() == "OrderWorkspaceService"
        && envelope.value("payload_complete").isBool()
        && !envelope.value("payload_complete").toBool();
    return (auditTypes.contains(type) || emptyDraftAudit)
        && envelope.value("payload_source").toString() == "mutation_metadata_only"
        && envelope.value("payload").isObject() && envelope.value("payload").toObject().isEmpty()
        && !envelope.contains("data") && !envelope.contains("row")
        && (!envelope.contains("related") || (envelope.value("related").isObject()
            && envelope.value("related").toObject().isEmpty()));
}
}
