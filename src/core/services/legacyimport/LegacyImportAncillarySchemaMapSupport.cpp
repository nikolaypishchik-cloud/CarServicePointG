#include "core/services/legacyimport/LegacyImportAncillarySchemaMapSupport.h"

namespace {
LegacyImportFieldMap field(const QString& targetField, const QStringList& sourceCandidates,
                           const QString& transformRule, bool required = false)
{
    LegacyImportFieldMap item;
    item.targetField = targetField;
    item.sourceCandidates = sourceCandidates;
    item.transformRule = transformRule;
    item.required = required;
    return item;
}

LegacyImportEntitySchemaMap entity(const QString& entityName, const QString& sourceTable, const QString& targetTable,
                                   const QString& strategy, bool requiresNormalization,
                                   const QVector<LegacyImportFieldMap>& fields,
                                   const QStringList& notes = {})
{
    LegacyImportEntitySchemaMap item;
    item.entityName = entityName;
    item.sourceTable = sourceTable;
    item.targetTable = targetTable;
    item.strategy = strategy;
    item.requiresNormalization = requiresNormalization;
    item.fieldMaps = fields;
    item.notes = notes;
    return item;
}
}

QVector<LegacyImportEntitySchemaMap> LegacyImportAncillarySchemaMapSupport::buildAncillarySchemaMap()
{
    return {
        entity(QStringLiteral("catalog"),
               QStringLiteral("parts_catalog"),
               QStringLiteral("parts_catalog"),
               QStringLiteral("canonical_import"),
               true,
               {
                   field(QStringLiteral("part_number"), {QStringLiteral("part_number"), QStringLiteral("oem_number"), QStringLiteral("article"), QStringLiteral("article_number")}, QStringLiteral("prefer explicit part/article number, fallback OEM number"), true),
                   field(QStringLiteral("name"), {QStringLiteral("name"), QStringLiteral("title")}, QStringLiteral("prefer explicit part name"), true),
                   field(QStringLiteral("brand"), {QStringLiteral("brand"), QStringLiteral("manufacturer")}, QStringLiteral("copy brand/manufacturer")),
                   field(QStringLiteral("price"), {QStringLiteral("price"), QStringLiteral("client_price"), QStringLiteral("sale_price")}, QStringLiteral("prefer client-facing price, fallback 0")),
                   field(QStringLiteral("vehicle_brand"), {QStringLiteral("vehicle_brand"), QStringLiteral("brand_auto")}, QStringLiteral("copy vehicle brand when present")),
                   field(QStringLiteral("vehicle_model"), {QStringLiteral("vehicle_model"), QStringLiteral("model_auto")}, QStringLiteral("copy vehicle model when present")),
                   field(QStringLiteral("year_from"), {QStringLiteral("year_from")}, QStringLiteral("copy as-is or fallback 0")),
                   field(QStringLiteral("year_to"), {QStringLiteral("year_to")}, QStringLiteral("copy as-is or fallback 0")),
                   field(QStringLiteral("fuel_type"), {QStringLiteral("fuel_type"), QStringLiteral("fuel")}, QStringLiteral("copy fuel type when present")),
                   field(QStringLiteral("group_name"), {QStringLiteral("group_name"), QStringLiteral("group_key"), QStringLiteral("kind")}, QStringLiteral("prefer group_name, fallback group_key/kind")),
                   field(QStringLiteral("supplier"), {QStringLiteral("supplier"), QStringLiteral("vendor")}, QStringLiteral("copy supplier/vendor when present")),
                   field(QStringLiteral("note"), {QStringLiteral("note"), QStringLiteral("node_tags"), QStringLiteral("comment")}, QStringLiteral("preserve note; if only node_tags exist, move them into note text"))
               },
               {
                   QStringLiteral("exact duplicate catalog rows should be reported but must not be silently removed"),
                   QStringLiteral("legacy node_tags/oem_number that have no dedicated target columns must be preserved through note/group_name transformation")
               }),

        entity(QStringLiteral("planning"),
               QStringLiteral("planning_calendar"),
               QStringLiteral("planning_calendar"),
               QStringLiteral("canonical_import"),
               true,
               {
                   field(QStringLiteral("day"), {QStringLiteral("day")}, QStringLiteral("normalize supported legacy date formats to yyyy-MM-dd"), true),
                   field(QStringLiteral("note"), {QStringLiteral("note"), QStringLiteral("text"), QStringLiteral("comment")}, QStringLiteral("preserve planning note"))
               },
               {
                   QStringLiteral("duplicate day rows must block execute mode because planning_calendar.day is the primary key")
               }),

        entity(QStringLiteral("outsource"),
               QStringLiteral("outsource_repairs"),
               QStringLiteral("outsource_repairs + outsource_jobs bridge"),
               QStringLiteral("canonical_import"),
               true,
               {
                   field(QStringLiteral("order_number"), {QStringLiteral("order_number")}, QStringLiteral("require a migrated order match"), true),
                   field(QStringLiteral("sent_at"), {QStringLiteral("sent_at"), QStringLiteral("date"), QStringLiteral("created_at")}, QStringLiteral("prefer sent_at, fallback date/created_at")),
                   field(QStringLiteral("company"), {QStringLiteral("company"), QStringLiteral("company_name")}, QStringLiteral("preserve outsource company")),
                   field(QStringLiteral("part_name"), {QStringLiteral("part_name"), QStringLiteral("name")}, QStringLiteral("preserve outsourced part/service label"), true),
                   field(QStringLiteral("repair_cost"), {QStringLiteral("repair_cost"), QStringLiteral("company_price"), QStringLiteral("cost")}, QStringLiteral("prefer company/repair cost")),
                   field(QStringLiteral("client_price"), {QStringLiteral("client_price"), QStringLiteral("price")}, QStringLiteral("preserve client-facing price")),
                   field(QStringLiteral("note"), {QStringLiteral("note"), QStringLiteral("comment")}, QStringLiteral("preserve note/comment")),
                   field(QStringLiteral("created_at"), {QStringLiteral("created_at")}, QStringLiteral("copy or fallback current timestamp")),
                   field(QStringLiteral("service_id"), {QStringLiteral("service_id")}, QStringLiteral("preserve service link only if it belongs to the same imported order"))
               },
               {
                   QStringLiteral("legacy outsource_repairs rows are also bridged into outsource_jobs when a compatible service_id exists so the new UI can view and edit them"),
                   QStringLiteral("orphan order references must block execute mode; orphan service_id becomes a warning and is normalized to 0")
               }),

        entity(QStringLiteral("expenses"),
               QStringLiteral("expenses"),
               QStringLiteral("expenses"),
               QStringLiteral("canonical_import"),
               true,
               {
                   field(QStringLiteral("period_from"), {QStringLiteral("period_from")}, QStringLiteral("normalize supported legacy date formats to yyyy-MM-dd"), true),
                   field(QStringLiteral("period_to"), {QStringLiteral("period_to")}, QStringLiteral("normalize supported legacy date formats to yyyy-MM-dd"), true),
                   field(QStringLiteral("elec_prev"), {QStringLiteral("elec_prev")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("elec_curr"), {QStringLiteral("elec_curr")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("elec_tariff"), {QStringLiteral("elec_tariff")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("cleaning"), {QStringLiteral("cleaning")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("heating"), {QStringLiteral("heating")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("rent"), {QStringLiteral("rent")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("amortization"), {QStringLiteral("amortization")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("sw_opendatabot"), {QStringLiteral("sw_opendatabot")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("sw_tecalliance"), {QStringLiteral("sw_tecalliance")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("other_amount"), {QStringLiteral("other_amount"), QStringLiteral("other_expenses")}, QStringLiteral("prefer other_amount, fallback other_expenses")),
                   field(QStringLiteral("cleaning_note"), {QStringLiteral("cleaning_note")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("software_note"), {QStringLiteral("software_note")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("heating_note"), {QStringLiteral("heating_note")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("amortization_note"), {QStringLiteral("amortization_note")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("rent_note"), {QStringLiteral("rent_note")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("other_note"), {QStringLiteral("other_note"), QStringLiteral("notes_json")}, QStringLiteral("copy note; if only notes_json exists, preserve raw JSON text in other_note")),
                   field(QStringLiteral("elec_kwh"), {QStringLiteral("elec_kwh")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("elec_cost"), {QStringLiteral("elec_cost")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("created_at"), {QStringLiteral("created_at")}, QStringLiteral("copy or fallback current timestamp"))
               },
               {
                   QStringLiteral("duplicate expense periods should be reported because the new UI edits records by period_from/period_to"),
                   QStringLiteral("legacy notes_json that cannot be structurally merged is preserved verbatim in other_note")
               })
    };
}
