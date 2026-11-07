#include "core/io/SupplierInvoiceTemplateStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QLockFile>

QString SupplierInvoiceTemplateStore::defaultPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.trimmed().isEmpty()) base = QDir::tempPath();
    return QDir(base).filePath(QStringLiteral("invoice_templates/supplier_invoice_templates.json"));
}

QJsonObject SupplierInvoiceTemplateStore::read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

bool SupplierInvoiceTemplateStore::update(const QString& path,
                                         const std::function<void(QJsonObject&)>& mutation,
                                         QString* errorText)
{
    if (errorText) errorText->clear();
    const auto fail = [errorText](const QString& message) {
        if (errorText) *errorText = message;
        return false;
    };
    if (path.trimmed().isEmpty() || !mutation)
        return fail(QStringLiteral("Invalid template store update"));
    const QFileInfo target(path);
    if (!QDir().mkpath(target.absolutePath()))
        return fail(QStringLiteral("Cannot create template store directory"));
    const QString resolvedPath = QDir(QFileInfo(target.absolutePath()).canonicalFilePath())
                                     .filePath(target.fileName());
    QLockFile lock(resolvedPath + QStringLiteral(".lock"));
    // Never steal a lock from a live writer because a slow write exceeded a timeout.
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return fail(QStringLiteral("Template store is busy or its lock is unavailable"));
    QJsonObject root;
    QFile existing(resolvedPath);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly)) return fail(existing.errorString());
        const QByteArray bytes = existing.readAll();
        if (existing.error() != QFileDevice::NoError) return fail(existing.errorString());
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return fail(QStringLiteral("Invalid template JSON; original file preserved"));
        root = document.object();
        existing.close();
    }
    mutation(root);
    QSaveFile file(resolvedPath);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return fail(file.errorString());
    }
    if (!file.commit()) return fail(file.errorString());
    return true;
}
