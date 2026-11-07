#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <cmath>

namespace OrderPartIssueEvidence {
// One query per order, never one query per part. Missing/error evidence fails closed.
inline QJsonArray attach(QSqlDatabase db, const QJsonArray& rows) {
    QSet<QString> orders;
    for (const auto& value : rows) orders.insert(value.toObject().value("order_uid").toString());
    orders.remove(QString());
    QHash<QString, QJsonObject> evidence;
    QSqlQuery exists(db);
    const bool available = exists.exec("SELECT 1 FROM sqlite_master WHERE type='table' AND name='workshop_spare_movements'") && exists.next();
    if (available) for (const auto& order : orders) {
        QSqlQuery query(db);
        query.prepare("SELECT p.part_uid,m.id,m.quantity FROM workshop_spare_movements m "
                      "JOIN work_order_parts p ON p.id=m.part_id AND p.order_number=m.order_number "
                      "WHERE p.order_uid=? AND m.operation='issue' ORDER BY m.id");
        query.addBindValue(order);
        if (!query.exec()) continue;
        QHash<QString, QJsonObject> current;
        QSet<QString> invalid;
        while (query.next()) {
            const QString uid = query.value(0).toString();
            const double qty = query.value(2).toDouble();
            if (uid.isEmpty() || !std::isfinite(qty) || qty <= 0) { invalid.insert(uid); continue; }
            auto item = current.value(uid, QJsonObject{{"version",1},{"source","workshop_spare_movements"},
                {"part_uid",uid},{"order_uid",order},{"issued_quantity",0.0},{"movement_ids",QJsonArray{}}});
            auto ids = item.value("movement_ids").toArray();
            ids.append(QString::number(query.value(1).toLongLong()));
            item.insert("movement_ids", ids);
            const double total = item.value("issued_quantity").toDouble() + qty;
            if (!std::isfinite(total)) { invalid.insert(uid); continue; }
            item.insert("issued_quantity", total);
            current.insert(uid, item);
        }
        for (auto it = current.cbegin(); it != current.cend(); ++it)
            if (!invalid.contains(it.key())) evidence.insert(it.key(), it.value());
    }
    QJsonArray result;
    for (const auto& value : rows) {
        auto row = value.toObject();
        const auto found = evidence.constFind(row.value("part_uid").toString());
        row.insert("closure_issue_evidence", found == evidence.cend()
            ? QJsonValue(QJsonValue::Null) : QJsonValue(found.value()));
        result.append(row);
    }
    return result;
}
}
