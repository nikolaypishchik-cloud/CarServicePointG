#include "core/services/legacyimport/LegacyImportSchemaMapSupport.h"
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

QVector<LegacyImportEntitySchemaMap> LegacyImportSchemaMapSupport::buildSchemaMap()
{
    QVector<LegacyImportEntitySchemaMap> maps = {
        entity(QStringLiteral("orders"),
               QStringLiteral("work_orders"),
               QStringLiteral("work_orders"),
               QStringLiteral("canonical_import"),
               true,
               {
                   field(QStringLiteral("number"), {QStringLiteral("number")}, QStringLiteral("preserve legacy number as internal order key"), true),
                   field(QStringLiteral("order_uid"), {QStringLiteral("order_uid"), QStringLiteral("number")}, QStringLiteral("reuse legacy order_uid if non-empty, otherwise generate deterministic archive_<number> uid"), true),
                   field(QStringLiteral("order_number"), {QStringLiteral("order_number"), QStringLiteral("number")}, QStringLiteral("prefer legacy visible order_number, fallback to number"), true),
                   field(QStringLiteral("order_year"), {QStringLiteral("order_year"), QStringLiteral("opened_at")}, QStringLiteral("prefer explicit order_year, otherwise derive from opened_at"), true),
                   field(QStringLiteral("opened_at"), {QStringLiteral("opened_at")}, QStringLiteral("copy as-is; required for timeline/history"), true),
                   field(QStringLiteral("closed_at"), {QStringLiteral("closed_at")}, QStringLiteral("copy as-is or keep empty")),
                   field(QStringLiteral("status"), {QStringLiteral("status")}, QStringLiteral("copy as-is, fallback open")),
                   field(QStringLiteral("plate"), {QStringLiteral("plate")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("plate_norm"), {QStringLiteral("plate_norm"), QStringLiteral("plate")}, QStringLiteral("prefer normalized plate, fallback raw plate")),
                   field(QStringLiteral("vin"), {QStringLiteral("vin")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("car_year"), {QStringLiteral("car_year")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("odometer"), {QStringLiteral("odometer")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("engine_type"), {QStringLiteral("engine_type")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("engine_volume"), {QStringLiteral("engine_volume")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("fuel_type"), {QStringLiteral("fuel_type")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("car"), {QStringLiteral("car")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("client"), {QStringLiteral("client"), QStringLiteral("owner_name")}, QStringLiteral("prefer explicit client, fallback owner_name")),
                   field(QStringLiteral("client_phone"), {QStringLiteral("client_phone")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("master_id"), {QStringLiteral("master_id")}, QStringLiteral("copy legacy master_id, fallback to first valid master if missing"), true),
                   field(QStringLiteral("works_sum"), {QStringLiteral("works_sum")}, QStringLiteral("copy legacy header value; verify against services sum separately")),
                   field(QStringLiteral("parts_sum"), {QStringLiteral("parts_sum")}, QStringLiteral("copy legacy header value")),
                   field(QStringLiteral("discount"), {QStringLiteral("discount")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("master_name_closed"), {QStringLiteral("master_name_closed")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("car_model"), {QStringLiteral("car_model")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("car_brand"), {QStringLiteral("car_brand")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("car_kind"), {QStringLiteral("car_kind")}, QStringLiteral("copy as-is")),
                   field(QStringLiteral("color"), {QStringLiteral("color"), QStringLiteral("car_color")}, QStringLiteral("prefer color, fallback car_color")),
                   field(QStringLiteral("car_color"), {QStringLiteral("car_color"), QStringLiteral("color")}, QStringLiteral("prefer car_color, fallback color")),
                   field(QStringLiteral("owner_name"), {QStringLiteral("owner_name"), QStringLiteral("client")}, QStringLiteral("prefer owner_name, fallback client"))
               },
               {
                   QStringLiteral("order_uid/order_registry/workspace state are expected to be rebuilt by the new application runtime after import"),
                   QStringLiteral("legacy duplicate order numbers must block execute mode because work_orders.number remains the canonical internal key")
               }),

        entity(QStringLiteral("services"),
               QStringLiteral("work_order_services"),
               QStringLiteral("work_order_services"),
               QStringLiteral("canonical_import"),
               true,
               {
                   field(QStringLiteral("id"), {QStringLiteral("id")}, QStringLiteral("preserve legacy service id for service_masters/parts/outsource linkage"), true),
                   field(QStringLiteral("order_uid"), {QStringLiteral("order_uid"), QStringLiteral("order_number")}, QStringLiteral("prefer legacy order_uid, otherwise resolve from imported work_orders via order_number"), true),
                   field(QStringLiteral("order_number"), {QStringLiteral("order_number")}, QStringLiteral("copy as-is and require matching imported order"), true),
                   field(QStringLiteral("title"), {QStringLiteral("title"), QStringLiteral("name")}, QStringLiteral("prefer title, fallback name"), true),
                   field(QStringLiteral("qty"), {QStringLiteral("qty")}, QStringLiteral("copy qty, fallback 1")),
                   field(QStringLiteral("price"), {QStringLiteral("price")}, QStringLiteral("copy price, fallback 0")),
                   field(QStringLiteral("amount"), {QStringLiteral("amount")}, QStringLiteral("copy amount, otherwise calculate qty*price")),
                   field(QStringLiteral("note"), {QStringLiteral("note")}, QStringLiteral("copy service note")),
                   field(QStringLiteral("paid"), {QStringLiteral("paid")}, QStringLiteral("copy payment flag, fallback paid=1 for legacy rows without explicit state")),
                   field(QStringLiteral("parent_id"), {QStringLiteral("parent_id")}, QStringLiteral("copy parent_id but normalize 0/empty to NULL; invalid non-existing parent ids must be reported"))
               },
               {
                   QStringLiteral("orphan services without a matching order_number must block execute mode"),
                   QStringLiteral("parent_id=0 is treated as legacy root marker and should normalize to NULL during import")
               }),

        entity(QStringLiteral("parts"),
               QStringLiteral("work_order_parts"),
               QStringLiteral("work_order_parts"),
               QStringLiteral("canonical_import"),
               true,
               {
                   field(QStringLiteral("id"), {QStringLiteral("id")}, QStringLiteral("preserve legacy part id"), true),
                   field(QStringLiteral("order_uid"), {QStringLiteral("order_uid"), QStringLiteral("order_number")}, QStringLiteral("resolve via imported work_orders when order_uid is absent"), true),
                   field(QStringLiteral("order_number"), {QStringLiteral("order_number"), QStringLiteral("work_order"), QStringLiteral("work_order_id"), QStringLiteral("work_order_number")}, QStringLiteral("map legacy order reference to imported order"), true),
                   field(QStringLiteral("ordered_at"), {QStringLiteral("ordered_at"), QStringLiteral("date"), QStringLiteral("created_at")}, QStringLiteral("prefer ordered_at, fallback date/created_at")),
                   field(QStringLiteral("delivery_date"), {QStringLiteral("delivery_date"), QStringLiteral("received_at"), QStringLiteral("arrival_date")}, QStringLiteral("preserve delivery date when available")),
                   field(QStringLiteral("store"), {QStringLiteral("store"), QStringLiteral("shop")}, QStringLiteral("copy store/shop label")),
                   field(QStringLiteral("part_number"), {QStringLiteral("part_number"), QStringLiteral("oem"), QStringLiteral("oem_number"), QStringLiteral("article"), QStringLiteral("article_number")}, QStringLiteral("preserve article or OEM number when present")),
                   field(QStringLiteral("name"), {QStringLiteral("name"), QStringLiteral("title"), QStringLiteral("part_name")}, QStringLiteral("prefer explicit part name"), true),
                   field(QStringLiteral("note"), {QStringLiteral("note"), QStringLiteral("comment"), QStringLiteral("notes"), QStringLiteral("customer_note")}, QStringLiteral("preserve note/comment")),
                   field(QStringLiteral("qty"), {QStringLiteral("qty"), QStringLiteral("quantity")}, QStringLiteral("copy qty, fallback 1")),
                   field(QStringLiteral("price"), {QStringLiteral("price"), QStringLiteral("client_price"), QStringLiteral("sale_price"), QStringLiteral("purchase_price")}, QStringLiteral("prefer client-facing price")),
                   field(QStringLiteral("amount"), {QStringLiteral("amount"), QStringLiteral("sum"), QStringLiteral("total"), QStringLiteral("client_amount"), QStringLiteral("purchase_amount")}, QStringLiteral("prefer explicit amount, otherwise calculate qty*price")),
                   field(QStringLiteral("service_id"), {QStringLiteral("service_id")}, QStringLiteral("preserve service link when it points to a migrated service from the same order")),
                   field(QStringLiteral("ordered_by_admin"), {QStringLiteral("ordered_by_admin"), QStringLiteral("is_ordered")}, QStringLiteral("copy order flag, fallback 1")),
                   field(QStringLiteral("paid"), {QStringLiteral("paid"), QStringLiteral("is_paid")}, QStringLiteral("copy payment flag, fallback 0"))
               },
               {
                   QStringLiteral("parts with invalid service_id are imported, but the service link is normalized to 0 if it does not point to a migrated service of the same order"),
                   QStringLiteral("blank part_number is allowed for legacy consumables and must not block execute mode")
               }),

        entity(QStringLiteral("service_masters"),
               QStringLiteral("service_masters"),
               QStringLiteral("service_masters"),
               QStringLiteral("canonical_import"),
               true,
               {
                   field(QStringLiteral("service_id"), {QStringLiteral("service_id")}, QStringLiteral("preserve legacy linkage to imported service id"), true),
                   field(QStringLiteral("master_id"), {QStringLiteral("master_id")}, QStringLiteral("preserve legacy master linkage"), true),
                   field(QStringLiteral("share_percent"), {QStringLiteral("share_percent")}, QStringLiteral("copy as-is, normalize <=0 to 100")),
                   field(QStringLiteral("role"), {QStringLiteral("role")}, QStringLiteral("copy as-is, normalize blank to master")),
                   field(QStringLiteral("master_name_closed"), {QStringLiteral("master_name_closed")}, QStringLiteral("preserve historical closed-master label when available"))
               },
               {
                   QStringLiteral("invalid links to missing services or personnel must block execute mode during pre-audit"),
                   QStringLiteral("if a migrated service has no service_masters rows but order.master_id exists, a default 100% assignment is auto-generated")
               }),

        entity(QStringLiteral("personnel"),
               QStringLiteral("masters"),
               QStringLiteral("masters"),
               QStringLiteral("canonical_import"),
               true,
               {
                   field(QStringLiteral("id"), {QStringLiteral("id")}, QStringLiteral("copy as-is"), true),
                   field(QStringLiteral("name"), {QStringLiteral("name")}, QStringLiteral("copy as-is; blank names stay editable after import"), true),
                   field(QStringLiteral("role"), {QStringLiteral("role")}, QStringLiteral("copy as-is, normalize blank to master")),
                   field(QStringLiteral("admin_percent"), {QStringLiteral("admin_percent")}, QStringLiteral("copy as-is, fallback 0")),
                   field(QStringLiteral("active"), {QStringLiteral("active")}, QStringLiteral("preserve active/inactive state; fallback active=1"))
               },
               {
                   QStringLiteral("inactive personnel must be preserved because historical orders can still reference them"),
                   QStringLiteral("blank or duplicate personnel ids must block execute mode")
               }),

    };
    QVector<LegacyImportEntitySchemaMap> ancillary = LegacyImportAncillarySchemaMapSupport::buildAncillarySchemaMap();
    for (const LegacyImportEntitySchemaMap& item : ancillary) maps.push_back(item);
    return maps;
}
