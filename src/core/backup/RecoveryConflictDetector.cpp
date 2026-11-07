#include "core/backup/RecoveryConflictDetector.h"

QString RecoveryConflictDetector::initialConflictReason(const DraftPayload& draft)
{
    if (!draft.validationState.trimmed().isEmpty()
        && draft.validationState.compare(QStringLiteral("valid"), Qt::CaseInsensitive) != 0) {
        return QStringLiteral("Draft пошкоджений або невалідний і не може бути відновлений автоматично.");
    }

    const QString persistedState = draft.conflictState.trimmed().toLower();
    if (persistedState == QStringLiteral("conflict")) {
        return QStringLiteral("Recovery draft для %1/%2 позначений як конфліктний.")
            .arg(draft.formType, draft.entityId);
    }

    return QString();
}

QString RecoveryConflictDetector::effectiveConflictState(const DraftPayload& draft)
{
    if (!draft.validationState.trimmed().isEmpty()
        && draft.validationState.compare(QStringLiteral("valid"), Qt::CaseInsensitive) != 0) {
        return QStringLiteral("invalid");
    }

    const QString persistedState = draft.conflictState.trimmed().toLower();
    if (persistedState == QStringLiteral("conflict")) {
        return QStringLiteral("conflict");
    }
    if (persistedState == QStringLiteral("none")) {
        return QStringLiteral("none");
    }
    return QStringLiteral("unknown");
}

QString RecoveryConflictDetector::runtimeConflictReason(const DraftPayload& draft, const QString& currentCommittedVersion)
{
    const QString draftBase = draft.committedBaseVersion.trimmed();
    const QString runtimeBase = currentCommittedVersion.trimmed();
    const bool isNewEntityDraft = draft.entityId.trimmed().isEmpty()
        || draft.entityId == QStringLiteral("new")
        || draft.entityId == QStringLiteral("-1")
        || draft.entityId == QStringLiteral("0");
    if (isNewEntityDraft || draftBase.isEmpty() || runtimeBase.isEmpty() || draftBase == runtimeBase) {
        return QString();
    }
    return QStringLiteral("Конфлікт recovery для %1/%2: committed стан у БД вже інший. Draft не було накладено автоматично.")
        .arg(draft.formType, draft.entityId);
}

RecoveryConflictCheck RecoveryConflictDetector::checkRuntimeConflict(const DraftPayload& draft, const QString& currentCommittedVersion)
{
    RecoveryConflictCheck out;
    out.currentCommittedVersion = currentCommittedVersion.trimmed();
    out.warning = runtimeConflictReason(draft, currentCommittedVersion);
    out.conflict = !out.warning.isEmpty();
    return out;
}
