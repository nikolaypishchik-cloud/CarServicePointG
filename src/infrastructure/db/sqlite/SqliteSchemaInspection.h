#pragma once

#include <QSqlDatabase>
#include <QSqlQuery>

namespace SqliteSchemaInspection {
inline bool tableAbsent(QSqlDatabase db, const QString& schema, const QString& table)
{
    QSqlQuery query(db);
    query.prepare("SELECT (SELECT COUNT(*) FROM pragma_table_list WHERE schema=? AND name=? AND type='table') FROM pragma_database_list WHERE name=?");
    query.addBindValue(schema);
    query.addBindValue(table);
    query.addBindValue(schema);
    return query.exec() && query.next() && query.value(0).toInt() == 0;
}

}
