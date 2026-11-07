#pragma once
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QRegularExpression>
#include <initializer_list>

namespace cs::tests {
inline QString projectRoot()
{
    QDir dir(QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir());
    for (int i = 0; i < 12; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("CMakeLists.txt"))) && QFileInfo::exists(dir.filePath(QStringLiteral("src")))) return dir.absolutePath();
        if (!dir.cdUp()) break;
    }
    return QDir::currentPath();
}
inline QString readSourceFile(const char* logicalPath)
{
    const QString absolute = projectRoot() + QLatin1Char('/') + QString::fromUtf8(logicalPath);
    QFile file(absolute);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTest::qFail(qPrintable(QStringLiteral("Unable to open source file: %1").arg(absolute)), __FILE__, __LINE__);
        return {};
    }
    return QString::fromUtf8(file.readAll());
}
inline void requireFileExists(const char* logicalPath)
{
    const QString absolute = projectRoot() + QLatin1Char('/') + QString::fromUtf8(logicalPath);
    QVERIFY2(QFileInfo::exists(absolute), qPrintable(QStringLiteral("Expected file to exist: %1").arg(absolute)));
}

inline bool sourceIncludesDirective(const QString& entryPoint, const QString& path)
{
    const QString directive = QStringLiteral("#include \"") + path + QLatin1Char('"');
    for (const auto& line : entryPoint.split(QLatin1Char('\n')))
        if (line.trimmed() == directive) return true;
    return false;
}

inline bool cmakeIncludesModule(const QString& entry, const QString& module)
{
    return QRegularExpression(QStringLiteral("(?m)^\\s*include\\(\\s*")
        + QRegularExpression::escape(module) + QStringLiteral("\\s*\\)\\s*$")).match(entry).hasMatch();
}

inline bool cmakeListsSource(const QString& inventory, const QString& source)
{
    return QRegularExpression(QStringLiteral("(?m)^\\s*") + QRegularExpression::escape(source)
        + QStringLiteral("\\s*$")).match(inventory).hasMatch();
}

inline QString compiledSourceInventory()
{
    const QString entry = readSourceFile("CMakeLists.txt");
    QString result;
    for (const char* layer : {"Core", "Infrastructure", "Integrations", "Ui"}) {
        const QString path = QStringLiteral("cmake/modules/CarServiceSources") + QString::fromLatin1(layer) + QStringLiteral(".cmake");
        if (!cmakeIncludesModule(entry, path)) {
            QTest::qFail(qPrintable("Disconnected CMake source inventory: " + path), __FILE__, __LINE__);
            return {};
        }
        result += readSourceFile(path.toUtf8().constData());
    }
    return result;
}

inline bool invoiceEngineIncludesFragment(const QString& entryPoint, const QString& fragment)
{
    return sourceIncludesDirective(entryPoint, QStringLiteral("core/import/internal/") + fragment);
}

inline QString invoiceEngineImplementation()
{
    QString source = readSourceFile("src/core/import/InvoiceImportEngine.cpp");
    const QString entryPoint = source;
    for (const char* fragment : {"PdfTextExtractionStage.ipp", "ExcelExtractionStage.ipp", "InvoiceVendorDetector.ipp", "InvoiceRowsNormalizer.ipp", "InvoicePriceColumnResolver.ipp", "InvoiceImportPersistenceStage.ipp", "InvoiceImportDiagnostics.ipp"}) {
        const QString path = QStringLiteral("src/core/import/internal/") + QString::fromLatin1(fragment);
        if (!invoiceEngineIncludesFragment(entryPoint, QString::fromLatin1(fragment))) {
            QTest::qFail(qPrintable("Engine does not include " + path), __FILE__, __LINE__);
            return {};
        }
        source += readSourceFile(path.toUtf8().constData());
    }
    return source;
}


inline QString inlineEditorImplementation()
{
    QString source = readSourceFile("src/ui/tabs/orders/WorkPartsInlineEditorController.h");
    const QString entry = readSourceFile("src/ui/tabs/orders/WorkPartsInlineEditorControllerImpl.cpp");
    source += entry;
    for (const char* fragment : {"WorkPartsEditorRowHelpers.ipp", "WorkPartsEditorNavigation.ipp", "WorkPartsEditorCommitGuard.ipp", "WorkPartsEditorDelegateState.ipp"}) {
        const QString relative = QStringLiteral("ui/tabs/orders/internal/") + QString::fromLatin1(fragment);
        if (!sourceIncludesDirective(entry, relative)) {
            QTest::qFail(qPrintable("Controller does not include " + relative), __FILE__, __LINE__);
            return {};
        }
        source += readSourceFile((QStringLiteral("src/") + relative).toUtf8().constData());
    }
    return source;
}

inline bool importedDatesRemainIndependent(const QString& policy)
{
    return policy.contains("!importedInvoiceRow && invoiceNumber.isEmpty() && invoiceDate.isEmpty()")
        && policy.contains("const bool needsManualDeliveryAnchor = manualWithoutInvoiceMeta")
        && policy.contains("if (needsManualDeliveryAnchor)")
        && policy.contains("state.deliveryDate = WorkPartsQueryService::loadPartDeliveryDate(partId)")
        && !QRegularExpression(R"(state\.deliveryDate\s*=\s*invoiceDate)").match(policy).hasMatch();
}

inline QString updateSupportImplementation()
{
    QString source = readSourceFile("src/app/update/UpdateServiceSupport.cpp");
    const auto unit = source;
    for (const char* name : {"UpdateReleaseResolver.ipp", "UpdateDownloader.ipp", "UpdateInstallerRunner.ipp", "UpdateCleanupGuard.ipp", "UpdateVersionDetector.ipp"}) {
        const auto include = QStringLiteral("app/update/internal/") + QString::fromLatin1(name);
        if (!sourceIncludesDirective(unit, include)) {
            QTest::qFail("Disconnected updater implementation fragment", __FILE__, __LINE__);
            return {};
        }
        source += readSourceFile(("src/" + include).toUtf8().constData());
    }
    return source;
}

inline QString workPartsDialogSupportImplementation()
{
    QString source = readSourceFile("src/ui/tabs/orders/WorkPartsDialogSupport.cpp");
    const auto unit = source;
    for (const char* name : {"WorkPartsDialogBackupAndImport.ipp", "WorkPartsDialogRowState.ipp", "WorkPartsDialogPersistence.ipp"}) {
        const auto include = QStringLiteral("ui/tabs/orders/internal/") + QString::fromLatin1(name);
        if (!sourceIncludesDirective(unit, include)) {
            QTest::qFail("Disconnected WorkParts dialog support fragment", __FILE__, __LINE__);
            return {};
        }
        source += readSourceFile(("src/" + include).toUtf8().constData());
    }
    return source;
}

inline QString partEditServicesImplementation()
{
    const auto unit = readSourceFile("src/ui/tabs/orders/WorkPartsPartEditServices.cpp");
    const auto prelude = readSourceFile("src/ui/tabs/orders/WorkPartsDialogModulePrelude.h");
    if (!sourceIncludesDirective(unit, "ui/tabs/orders/WorkPartsDialogModulePrelude.h")
        || !sourceIncludesDirective(prelude, "ui/tabs/orders/WorkPartsPartEditServices.h")) {
        QTest::qFail("Disconnected part edit policy", __FILE__, __LINE__);
        return {};
    }
    return readSourceFile("src/ui/tabs/orders/WorkPartsPartEditServices.h");
}

inline bool invoiceOwnerCancellationWired(const QString& caller, const QString& progress, const QString& job)
{
    return sourceIncludesDirective(caller, "ui/common/widgets/InvoiceImportProgressDialog.h")
        && caller.contains("new InvoiceImportProgressDialog(owner, job,")
        && caller.contains("QPointer<InvoiceImport::ImportJob> jobGuard")
        && caller.contains("if (!guard) return;")
        && caller.contains("[guard, jobGuard, onDone = std::move(onDone)]")
        && caller.contains("&QObject::destroyed, job, [jobGuard] { if (jobGuard) jobGuard->cancel(); }")
        && progress.contains("if (!m_active || attempt != m_attempt) return;")
        && progress.contains("m_active = false; hide();")
        && progress.contains("if (m_job) m_job->cancel();")
        && progress.contains("~InvoiceImportProgressDialog() override { if (m_job && m_active)")
        && progress.contains("disconnect(m_job, nullptr, this, nullptr); m_job->cancel();")
        && job.contains("ImportJob::~ImportJob()\n{\n    detachWorkerForOwnerShutdown();")
        && job.contains("QObject::disconnect(m_thread, nullptr, this, nullptr)")
        && job.contains("m_thread->requestInterruption()")
        && job.contains("++m_generation");
}

inline bool invoiceCancellationRejectsBrokenLinks(QString caller, QString progress, QString job)
{
    if (!invoiceOwnerCancellationWired(caller, progress, job)) return false;
    const auto rejects = [&](QString c, QString p, QString j, const QString& needle, int target) {
        QString* text = target == 0 ? &c : target == 1 ? &p : &j;
        if (!text->contains(needle)) return false;
        text->replace(needle, "REMOVED_BY_NEGATIVE_TEST");
        return !invoiceOwnerCancellationWired(c, p, j);
    };
    return rejects(caller, progress, job, "if (!guard) return;", 0)
        && rejects(caller, progress, job, "if (jobGuard) jobGuard->cancel();", 0)
        && rejects(caller, progress, job, "if (!m_active || attempt != m_attempt) return;", 1)
        && rejects(caller, progress, job, "if (m_job) m_job->cancel();", 1)
        && rejects(caller, progress, job, "detachWorkerForOwnerShutdown();", 2)
        && rejects(caller, progress, job, "m_thread->requestInterruption()", 2);
}

inline QByteArray readBinaryFixture(const char* logicalPath)
{
    const QString absolute = projectRoot() + QLatin1Char('/') + QString::fromUtf8(logicalPath);
    QFile file(absolute);
    if (!file.open(QIODevice::ReadOnly)) {
        QTest::qFail(qPrintable(QStringLiteral("Unable to open binary file: %1").arg(absolute)), __FILE__, __LINE__);
        return {};
    }
    return file.readAll();
}

inline void requireFileStartsWith(const char* logicalPath, const QByteArray& magic)
{
    const QByteArray data = readBinaryFixture(logicalPath);
    QVERIFY2(data.startsWith(magic), qPrintable(QStringLiteral("Expected binary magic for %1").arg(QString::fromUtf8(logicalPath))));
}

inline void requireFileMinSize(const char* logicalPath, qsizetype minSize)
{
    const QByteArray data = readBinaryFixture(logicalPath);
    QVERIFY2(data.size() >= minSize, qPrintable(QStringLiteral("Expected %1 to be at least %2 bytes, got %3")
                                                    .arg(QString::fromUtf8(logicalPath))
                                                    .arg(minSize)
                                                    .arg(data.size())));
}

inline void requireContains(const char* logicalPath, const char* needle)
{
    const QString content = readSourceFile(logicalPath);
    QVERIFY2(content.contains(QString::fromUtf8(needle)), qPrintable(QStringLiteral("Expected '%1' inside %2").arg(QString::fromUtf8(needle), QString::fromUtf8(logicalPath))));
}
inline void requireAnyContains(const char* logicalPath, std::initializer_list<const char*> needles)
{
    const QString content = readSourceFile(logicalPath);
    for (const char* needle : needles) if (content.contains(QString::fromUtf8(needle))) return;
    QString expected;
    for (const char* needle : needles) { if (!expected.isEmpty()) expected += QStringLiteral(", "); expected += QString::fromUtf8(needle); }
    QVERIFY2(false, qPrintable(QStringLiteral("Expected one of [%1] inside %2").arg(expected, QString::fromUtf8(logicalPath))));
}

inline void requireNotContains(const char* logicalPath, const char* needle)
{
    const QString content = readSourceFile(logicalPath);
    QVERIFY2(!content.contains(QString::fromUtf8(needle)), qPrintable(QStringLiteral("Did not expect '%1' inside %2").arg(QString::fromUtf8(needle), QString::fromUtf8(logicalPath))));
}
}
