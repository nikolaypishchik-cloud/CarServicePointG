#include "PartServiceAssignmentService.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

namespace {
QString normalizeForMatch_service(QString text)
{
    text = text.toLower().trimmed();
    text.replace(QStringLiteral("ё"), QStringLiteral("е"));
    text.replace(QStringLiteral("ї"), QStringLiteral("і"));
    text.replace(QStringLiteral("й"), QStringLiteral("и"));
    text.replace(QStringLiteral("ґ"), QStringLiteral("г"));
    text.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    return text.simplified();
}

QStringList tokensForMatch_service(const QString& text)
{
    return normalizeForMatch_service(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

int localPartServiceMatchScore_service(const QString& serviceTitle, const QString& partName)
{
    const QString normService = normalizeForMatch_service(serviceTitle);
    const QString normPart = normalizeForMatch_service(partName);
    if (normService.isEmpty() || normPart.isEmpty()) return 0;

    int score = 0;
    if (normPart.contains(normService) || normService.contains(normPart)) score += 180;

    const QStringList serviceTokens = tokensForMatch_service(normService);
    const QStringList partTokens = tokensForMatch_service(normPart);
    QSet<QString> partSet(partTokens.begin(), partTokens.end());
    for (const QString& token : serviceTokens) {
        if (token.size() < 3) continue;
        if (partSet.contains(token)) score += 70;
    }
    return score;
}

bool ensureValidOpenDb_partAssign(QSqlDatabase& db, QString* errorText)
{
    if (!db.isValid() || !db.isOpen()) db = Database::db();
    if (!db.isValid()) {
        if (errorText) *errorText = QString::fromUtf8("БД недоступна для автоприв'язки запчастин");
        return false;
    }
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }
    return true;
}
}

int PartServiceAssignmentService::autoAssign(const QString& orderUid,
                                             const QList<ServiceRow>& services,
                                             const QList<PartRow>& parts,
                                             QString* errorText)
{
    QSqlDatabase db = Database::db();
    return autoAssign(db, orderUid, services, parts, errorText);
}

int PartServiceAssignmentService::autoAssign(QSqlDatabase db,
                                             const QString& orderUid,
                                             const QList<ServiceRow>& services,
                                             const QList<PartRow>& parts,
                                             QString* errorText)
{
    if (services.isEmpty() || orderUid.trimmed().isEmpty()) return 0;
    if (!ensureValidOpenDb_partAssign(db, errorText)) return 0;

    QSet<int> validServiceIds;
    for (const auto& s : services) validServiceIds.insert(s.id);

    int changedAssignments = 0;
    QSqlQuery up(db);
    up.prepare(QStringLiteral("UPDATE work_order_parts SET service_id=? WHERE id=? AND order_uid=?"));
    for (const auto& part : parts) {
        if (part.id <= 0 || part.name.trimmed().isEmpty()) continue;
        int targetServiceId = validServiceIds.contains(part.serviceId) ? part.serviceId : 0;
        if (targetServiceId <= 0) {
            int bestScore = 0;
            int secondBestScore = 0;
            for (const auto& service : services) {
                const int score = localPartServiceMatchScore_service(service.title, part.name);
                if (score > bestScore) {
                    secondBestScore = bestScore;
                    bestScore = score;
                    targetServiceId = service.id;
                } else if (score > secondBestScore) {
                    secondBestScore = score;
                }
            }
            if (bestScore <= 0 || (bestScore < 140 && (bestScore - secondBestScore) < 30)) targetServiceId = 0;
        }
        if (targetServiceId == part.serviceId) continue;
        up.addBindValue(targetServiceId);
        up.addBindValue(part.id);
        up.addBindValue(orderUid);
        if (!up.exec()) {
            if (errorText) *errorText = up.lastError().text();
            return changedAssignments;
        }
        ++changedAssignments;
        up.finish();
    }
    return changedAssignments;
}
