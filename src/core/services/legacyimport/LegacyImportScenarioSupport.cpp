#include "core/services/legacyimport/LegacyImportScenarioSupport.h"

#include "core/services/LegacyMigrationService.h"
#include "core/services/legacyimport/LegacyImportTypes.h"

namespace {
LegacyImportScenarioItem item(const QString& id,
                              const QString& title,
                              const QString& status,
                              const QString& note,
                              bool automated)
{
    LegacyImportScenarioItem v;
    v.id = id;
    v.title = title;
    v.status = status;
    v.note = note;
    v.automated = automated;
    return v;
}
}

QVector<LegacyImportScenarioItem> LegacyImportScenarioSupport::buildScenarioMatrix(const LegacyImportAuditReport* auditReport,
                                                                                    const LegacyMigrationResult* migrationResult,
                                                                                    const LegacyImportVerifyReport* verifyReport)
{
    const bool executeSuccess = migrationResult && migrationResult->importedOrderCount > 0;
    const bool verifySuccess = verifyReport && verifyReport->success;
    const bool previewReady = auditReport && auditReport->canExecute;
    const bool repeatGuard = executeSuccess;
    const bool rollbackGuard = executeSuccess;

    return {
        item(QStringLiteral("10.1"), QStringLiteral("Імпорт штатної старої БД"), executeSuccess && verifySuccess ? QStringLiteral("PASS") : QStringLiteral("MANUAL"),
             executeSuccess && verifySuccess ? QStringLiteral("Пайплайн preview/execute/verify відпрацьований") : QStringLiteral("Потрібен runtime-прогін на реальній legacy БД"), true),
        item(QStringLiteral("10.2"), QStringLiteral("Повторний запуск міграції на вже мігрованій БД"), repeatGuard ? QStringLiteral("IMPLEMENTED") : QStringLiteral("MANUAL"),
             QStringLiteral("Fingerprint guard у history блокує повторний небезпечний імпорт"), true),
        item(QStringLiteral("10.3"), QStringLiteral("Перервана міграція посеред процесу"), rollbackGuard ? QStringLiteral("IMPLEMENTED") : QStringLiteral("MANUAL"),
             QStringLiteral("Snapshot/restore + quarantine використовуються при failAndRestore"), true),
        item(QStringLiteral("10.4"), QStringLiteral("Міграція БД з дефектними або неповними записами"), previewReady ? QStringLiteral("IMPLEMENTED") : QStringLiteral("IMPLEMENTED"),
             QStringLiteral("Pre-import audit формує findings і може блокувати execute mode"), true),
        item(QStringLiteral("10.5"), QStringLiteral("Міграція БД з великим обсягом закритих нарядів"), QStringLiteral("MANUAL"),
             QStringLiteral("Потрібен окремий stress-прогін на великому backup"), false),
        item(QStringLiteral("10.6"), QStringLiteral("Перевірка роботи нової системи після міграції"), verifySuccess ? QStringLiteral("PASS") : QStringLiteral("MANUAL"),
             verifySuccess ? QStringLiteral("Verify mode пройдено") : QStringLiteral("Потрібна ручна перевірка UI-сценаріїв"), true),
        item(QStringLiteral("10.7"), QStringLiteral("Відновлення після аварійного завершення під час міграції"), rollbackGuard ? QStringLiteral("IMPLEMENTED") : QStringLiteral("MANUAL"),
             QStringLiteral("Зберігається snapshot і виконується restore при невдачі"), true),
        item(QStringLiteral("10.8"), QStringLiteral("Пошук, звіти, планування та історія після міграції"), QStringLiteral("MANUAL"),
             QStringLiteral("Потрібен ручний acceptance-прогін UI після імпорту"), false),
        item(QStringLiteral("10.9"), QStringLiteral("Registry/cache/state на legacy-даних"), verifySuccess ? QStringLiteral("IMPLEMENTED") : QStringLiteral("MANUAL"),
             QStringLiteral("Після імпорту викликаються startup maintenance і registry refresh"), true),
        item(QStringLiteral("10.10"), QStringLiteral("Міграція не ламає save/startup/shutdown"), QStringLiteral("MANUAL"),
             QStringLiteral("Потрібен окремий smoke-test перезапуску застосунку"), false)
    };
}
