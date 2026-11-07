#include "infrastructure/db/sqlite/DbSourceFingerprintSupport.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

QString legacySourceFingerprint(const QString& path)
{
    const QFileInfo fi(path);
    if (!fi.exists()) return QString();

    QFile file(fi.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("open-error|%1|%2")
            .arg(fi.absoluteFilePath())
            .arg(fi.size());
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray chunk;
    chunk.resize(1024 * 1024);
    while (true) {
        const qint64 read = file.read(chunk.data(), chunk.size());
        if (read < 0) {
            return QStringLiteral("read-error|%1|%2")
                .arg(fi.absoluteFilePath())
                .arg(fi.size());
        }
        if (read == 0) break;
        hash.addData(chunk.constData(), read);
    }

    return QStringLiteral("%1|%2|%3")
        .arg(fi.absoluteFilePath())
        .arg(fi.size())
        .arg(QString::fromLatin1(hash.result().toHex()));
}

bool copyAuditedDatabase(const QString& source,const QString& destination,const QString& identity,QString* error)
{
    const auto fail=[&](const QString& message) { if (error) *error=message; return false; };
    const auto hasJournal=[&] { return QFileInfo(source+"-wal").size()>0 || QFileInfo(source+"-journal").size()>0; };
    if (hasJournal()) return fail("Import requires a checkpointed source backup");
    if (!QFile::copy(source,destination)) return fail("Cannot copy audited import source");
    QFile input(destination);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!input.open(QIODevice::ReadOnly) || !hash.addData(&input) || input.error()!=QFileDevice::NoError)
        return fail("Cannot read back audited import source");
    const auto actual=QStringLiteral("%1|%2|%3").arg(QFileInfo(source).absoluteFilePath()).arg(input.size()).arg(QString::fromLatin1(hash.result().toHex()));
    if (actual!=identity || hasJournal()) return fail("Import source differs from audited file");
    return true;
}
