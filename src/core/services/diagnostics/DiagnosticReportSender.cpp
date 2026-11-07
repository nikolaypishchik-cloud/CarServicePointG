#include "core/services/diagnostics/DiagnosticReportSender.h"

#include "core/services/settings/GeneralSettingsService.h"
#include "runtime/AppRuntime.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSslSocket>
#include <QStandardPaths>
#include <QSysInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {

constexpr qint64 kMaxTotalAttachmentBytes = 12 * 1024 * 1024;
constexpr int kMaxAttachments = 12;
constexpr int kSmtpTimeoutMs = 20000;

struct ReportSmtpConfig {
    QString host;
    int port = 0;
    QString username;
    QString password;
    QString from;
    QString recipient;
};

#ifndef CS_EMBEDDED_REPORT_SMTP_PASSWORD
#define CS_EMBEDDED_REPORT_SMTP_PASSWORD "ZXC3009zxc$$"
#endif

QString cleanAppPassword(QString password)
{
    password = password.trimmed();
    password.remove(QRegularExpression(QStringLiteral("\\s+")));
    return password;
}

QString embeddedReportSmtpPassword()
{
    return cleanAppPassword(QString::fromUtf8(CS_EMBEDDED_REPORT_SMTP_PASSWORD));
}

ReportSmtpConfig loadEmbeddedDefaults()
{
    ReportSmtpConfig config;
    config.host = QStringLiteral("mail.carservice.in.ua");
    config.port = 465;
    config.username = QStringLiteral("support@carservice.in.ua");
    config.password = embeddedReportSmtpPassword();
    config.from = config.username;
    config.recipient = QStringLiteral("");
    return config;
}

QString configuredPasswordFromFile(QString* source)
{
    const QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config/report-smtp-password.txt")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("report-smtp-password.txt")),
        QDir::current().filePath(QStringLiteral("config/report-smtp-password.txt")),
        QDir::current().filePath(QStringLiteral("report-smtp-password.txt"))
    };
    for (const QString& path : candidates) {
        QFile file(path);
        if (!file.exists()) continue;
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (source) *source = QString::fromUtf8("файл недоступний: %1").arg(QDir::toNativeSeparators(path));
            return QString();
        }
        const QString password = cleanAppPassword(QString::fromUtf8(file.readAll()));
        if (!password.isEmpty()) {
            if (source) *source = QDir::toNativeSeparators(path);
            return password;
        }
    }

    return QString();
}

QString configuredPasswordFromEnv(QString* source)
{
    const QString envPassword = cleanAppPassword(qEnvironmentVariable("CS_REPORT_SMTP_PASSWORD"));
    if (!envPassword.isEmpty()) {
        if (source) *source = QStringLiteral("CS_REPORT_SMTP_PASSWORD");
        return envPassword;
    }

    const QString appPassword = cleanAppPassword(qEnvironmentVariable("CARSERVICE_REPORT_SMTP_PASSWORD"));
    if (!appPassword.isEmpty()) {
        if (source) *source = QStringLiteral("CARSERVICE_REPORT_SMTP_PASSWORD");
        return appPassword;
    }

    return QString();
}

ReportSmtpConfig loadEffectiveSmtpConfig(QString* passwordSource)
{
    ReportSmtpConfig config = loadEmbeddedDefaults();
    if (!config.password.isEmpty()) {
        if (passwordSource) *passwordSource = QStringLiteral("embedded application config");
        return config;
    }

    config.password = configuredPasswordFromFile(passwordSource);
    if (!config.password.isEmpty()) return config;

    config.password = configuredPasswordFromEnv(passwordSource);
    return config;
}

QStringList missingSmtpFields(const ReportSmtpConfig& config)
{
    QStringList missing;
    if (config.host.trimmed().isEmpty()) missing << QStringLiteral("host");
    if (config.port <= 0) missing << QStringLiteral("port");
    if (config.username.trimmed().isEmpty()) missing << QStringLiteral("username");
    if (config.password.trimmed().isEmpty()) missing << QStringLiteral("password");
    if (config.recipient.trimmed().isEmpty()) missing << QStringLiteral("recipient");
    return missing;
}

QString reportRootDir()
{
    const auto dirs = GeneralSettingsService::loadDirectoriesSettings();
    QString root = dirs.reportsDir.trimmed();
    if (root.isEmpty()) {
        root = QDir(AppRuntime::dataDir()).filePath(QStringLiteral("reports"));
    }
    return QDir(root).filePath(QStringLiteral("diagnostics"));
}

QString safeFileStem(QString stem)
{
    stem = stem.trimmed();
    stem.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\s]+")), QStringLiteral("_"));
    stem.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    stem = stem.left(96);
    return stem.isEmpty() ? QStringLiteral("diagnostic_report") : stem;
}

QString makeReportPath(QString* errorText)
{
    const QString dirPath = reportRootDir();
    if (!QDir().mkpath(dirPath)) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося створити папку для діагностичного звіту:\n%1")
                             .arg(QDir::toNativeSeparators(dirPath));
        }
        return QString();
    }
    QFileInfo dirInfo(dirPath);
    if (!dirInfo.exists() || !dirInfo.isDir() || !dirInfo.isWritable()) {
        if (errorText) {
            *errorText = QString::fromUtf8("Папка діагностичних звітів недоступна для запису:\n%1")
                             .arg(QDir::toNativeSeparators(dirPath));
        }
        return QString();
    }

    const QString stem = safeFileStem(QStringLiteral("CarServicePointG_diagnostic_%1")
                                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
    return QDir(dirPath).filePath(stem + QStringLiteral(".eml"));
}

QStringList collectLogFilesForReport(QStringList* skipped)
{
    struct SourceDir {
        QString path;
        QString prefix;
    };

    const QList<SourceDir> sources = {
        {AppRuntime::logsDir(), QStringLiteral("logs")},
        {AppRuntime::sessionDir(), QStringLiteral("runtime")}
    };

    QStringList files;
    qint64 totalBytes = 0;

    for (const auto& source : sources) {
        QDir dir(source.path);
        if (!dir.exists()) {
            if (skipped) *skipped << QString::fromUtf8("немає папки: %1").arg(QDir::toNativeSeparators(source.path));
            continue;
        }
        const QFileInfoList entries = dir.entryInfoList(QStringList()
                                                            << QStringLiteral("*.log")
                                                            << QStringLiteral("*.log.*")
                                                            << QStringLiteral("app_stderr_*.txt")
                                                            << QStringLiteral("app_stderr_*.log"),
                                                        QDir::Files | QDir::Readable,
                                                        QDir::Time);
        for (const QFileInfo& info : entries) {
            if (files.size() >= kMaxAttachments) return files;
            if (!info.exists() || !info.isFile()) continue;
            if (!info.isReadable()) {
                if (skipped) *skipped << QString::fromUtf8("файл недоступний: %1").arg(QDir::toNativeSeparators(info.absoluteFilePath()));
                continue;
            }
            if (info.size() <= 0) {
                if (skipped) *skipped << QString::fromUtf8("порожній файл: %1").arg(QDir::toNativeSeparators(info.absoluteFilePath()));
                continue;
            }
            if (info.size() > kMaxTotalAttachmentBytes || totalBytes + info.size() > kMaxTotalAttachmentBytes) {
                if (skipped) *skipped << QString::fromUtf8("файл не вкладено через ліміт розміру: %1").arg(QDir::toNativeSeparators(info.absoluteFilePath()));
                continue;
            }
            files << info.absoluteFilePath();
            totalBytes += info.size();
        }
    }

    return files;
}

QString formatMailDate(const QDateTime& dt)
{
    return dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(QStringLiteral("ddd, dd MMM yyyy HH:mm:ss t"));
}

QByteArray encodedWord(const QString& value)
{
    QByteArray encoded("=?UTF-8?B?");
    encoded += value.toUtf8().toBase64();
    encoded += "?=";
    return encoded;
}

QByteArray wrapBase64(const QByteArray& raw)
{
    const QByteArray b64 = raw.toBase64();
    QByteArray out;
    out.reserve(b64.size() + b64.size() / 76 + 4);
    for (int i = 0; i < b64.size(); i += 76) {
        out += b64.mid(i, 76);
        out += "\r\n";
    }
    return out;
}

QByteArray buildReportMessage(const QStringList& attachments, const QStringList& skipped, const QString& sender, const QString& recipient)
{
    const QString boundary = QStringLiteral("----CarServicePointG_Report_%1").arg(QDateTime::currentMSecsSinceEpoch());
    const QDateTime now = QDateTime::currentDateTime();
    const QString subject = QString::fromUtf8("CarServicePointG - звіт діагностики");

    QString bodyText;
    QTextStream body(&bodyText);
    body << QString::fromUtf8("Звіт діагностики CarServicePointG\n");
    body << QString::fromUtf8("Дата формування: ") << now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss t")) << QLatin1Char('\n');
    body << QString::fromUtf8("Користувач ОС: ") << qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER")) << QLatin1Char('\n');
    body << QString::fromUtf8("Комп'ютер: ") << QSysInfo::machineHostName() << QLatin1Char('\n');
    body << QString::fromUtf8("ОС: ") << QSysInfo::prettyProductName() << QLatin1Char('\n');
    body << QString::fromUtf8("Папка логів: ") << QDir::toNativeSeparators(AppRuntime::logsDir()) << QLatin1Char('\n');
    body << QString::fromUtf8("Основний лог: ") << QDir::toNativeSeparators(AppRuntime::logFilePath()) << QLatin1Char('\n');
    body << QString::fromUtf8("Кількість вкладень: ") << attachments.size() << QLatin1Char('\n');
    if (attachments.isEmpty()) {
        body << QString::fromUtf8("Логи не знайдені або недоступні для читання.\n");
    } else {
        body << QString::fromUtf8("Вкладені файли:\n");
        for (const QString& file : attachments) {
            body << QStringLiteral("- ") << QDir::toNativeSeparators(file) << QLatin1Char('\n');
        }
    }
    if (!skipped.isEmpty()) {
        body << QString::fromUtf8("Пропущені файли/папки:\n");
        for (const QString& item : skipped) {
            body << QStringLiteral("- ") << item << QLatin1Char('\n');
        }
    }

    const QByteArray boundaryBytes = boundary.toUtf8();
    QByteArray msg;
    msg += "From: CarServicePointG <" + sender.toUtf8() + ">\r\n";
    msg += "To: " + recipient.toUtf8() + "\r\n";
    msg += "Subject: ";
    msg += encodedWord(subject);
    msg += "\r\nDate: " + formatMailDate(now).toUtf8() + "\r\n";
    msg += "MIME-Version: 1.0\r\n";
    msg += "Content-Type: multipart/mixed; boundary=\"" + boundaryBytes + "\"\r\n\r\n";
    msg += "--" + boundaryBytes + "\r\n";
    msg += "Content-Type: text/plain; charset=UTF-8\r\n";
    msg += "Content-Transfer-Encoding: base64\r\n\r\n";
    msg += wrapBase64(bodyText.toUtf8()) + "\r\n";

    for (const QString& path : attachments) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray data = f.readAll();
        if (data.isEmpty()) continue;
        const QByteArray fileName = QFileInfo(path).fileName().toUtf8().toPercentEncoding();
        msg += "--" + boundaryBytes + "\r\n";
        msg += "Content-Type: text/plain; name=\"" + fileName + "\"\r\n";
        msg += "Content-Disposition: attachment; filename=\"" + fileName + "\"\r\n";
        msg += "Content-Transfer-Encoding: base64\r\n\r\n";
        msg += wrapBase64(data) + "\r\n";
    }

    msg += "--" + boundaryBytes + "--\r\n";

    QByteArray safe;
    safe.reserve(msg.size() + 64);
    const QList<QByteArray> lines = msg.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith('.')) safe += '.';
        safe += line + "\r\n";
    }
    return safe;
}

bool saveReportFile(const QString& path, const QByteArray& data, QString* errorText)
{
    if (path.trimmed().isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не визначено шлях для діагностичного звіту.");
        return false;
    }
    if (data.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Сформований діагностичний звіт порожній.");
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося створити файл звіту:\n%1\n%2")
                             .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }
    if (file.write(data) != data.size()) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося записати повний файл звіту:\n%1")
                             .arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    if (!file.commit()) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося завершити запис файлу звіту:\n%1\n%2")
                             .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }

    QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.size() <= 0 || !info.isReadable()) {
        if (errorText) {
            *errorText = QString::fromUtf8("Файл звіту створено некоректно або він недоступний для читання:\n%1")
                             .arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    return true;
}

bool readSmtpResponse(QSslSocket& socket, int expectedCode, QString* transcript, QString* errorText)
{
    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < kSmtpTimeoutMs) {
        if (!socket.canReadLine()) socket.waitForReadyRead(1000);
        while (socket.canReadLine()) {
            const QByteArray line = socket.readLine();
            response += line;
            if (transcript) *transcript += QString::fromLatin1(line);
            if (line.size() >= 4 && line.at(3) == ' ') {
                bool ok = false;
                const int code = line.left(3).toInt(&ok);
                if (ok && code == expectedCode) return true;
                if (ok) {
                    if (errorText) {
                        *errorText = QString::fromUtf8("SMTP очікував код %1, отримав %2: %3")
                                         .arg(expectedCode)
                                         .arg(code)
                                         .arg(QString::fromLatin1(response).trimmed());
                    }
                    return false;
                }
            }
        }
    }
    if (errorText) {
        *errorText = QString::fromUtf8("SMTP не відповів протягом %1 с. Остання відповідь: %2")
                         .arg(kSmtpTimeoutMs / 1000)
                         .arg(QString::fromLatin1(response).trimmed());
    }
    return false;
}

bool sendSmtpCommand(QSslSocket& socket, const QByteArray& command, int expectedCode, QString* transcript, QString* errorText)
{
    socket.write(command + "\r\n");
    if (!socket.waitForBytesWritten(10000)) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося передати SMTP-команду: %1").arg(socket.errorString());
        }
        return false;
    }
    return readSmtpResponse(socket, expectedCode, transcript, errorText);
}

bool sendViaSmtp(const QByteArray& message, const ReportSmtpConfig& config, QString* errorText)
{
    if (!QSslSocket::supportsSsl()) {
        if (errorText) {
            *errorText = QString::fromUtf8("SSL/OpenSSL недоступний. Перевірте наявність потрібних Qt/OpenSSL DLL.");
        }
        return false;
    }

    QString transcript;
    QSslSocket socket;
    socket.connectToHostEncrypted(config.host, config.port);
    if (!socket.waitForEncrypted(kSmtpTimeoutMs)) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося підключитися до SMTP %1:%2: %3")
                             .arg(config.host)
                             .arg(config.port)
                             .arg(socket.errorString());
        }
        return false;
    }

    if (!readSmtpResponse(socket, 220, &transcript, errorText)) return false;
    if (!sendSmtpCommand(socket, "EHLO carservicepointg.local", 250, &transcript, errorText)) return false;
    if (!sendSmtpCommand(socket, "AUTH LOGIN", 334, &transcript, errorText)) return false;
    if (!sendSmtpCommand(socket, config.username.toUtf8().toBase64(), 334, &transcript, errorText)) return false;
    if (!sendSmtpCommand(socket, config.password.toUtf8().toBase64(), 235, &transcript, errorText)) return false;
    if (!sendSmtpCommand(socket, QByteArray("MAIL FROM:<") + config.from.toUtf8() + ">", 250, &transcript, errorText)) return false;
    if (!sendSmtpCommand(socket, QByteArray("RCPT TO:<") + config.recipient.toUtf8() + ">", 250, &transcript, errorText)) return false;
    if (!sendSmtpCommand(socket, "DATA", 354, &transcript, errorText)) return false;

    socket.write(message);
    socket.write("\r\n.\r\n");
    if (!socket.waitForBytesWritten(kSmtpTimeoutMs)) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося передати тіло листа: %1").arg(socket.errorString());
        }
        return false;
    }
    if (!readSmtpResponse(socket, 250, &transcript, errorText)) return false;
    sendSmtpCommand(socket, "QUIT", 221, &transcript, nullptr);
    return true;
}

DiagnosticReportSender::Result prepareReport()
{
    DiagnosticReportSender::Result result;
    result.recipient = DiagnosticReportSender::recipientEmail();
    result.sender = DiagnosticReportSender::senderEmail();

    QStringList skipped;
    result.attachments = collectLogFilesForReport(&skipped);
    QString pathError;
    result.reportPath = makeReportPath(&pathError);
    if (result.reportPath.isEmpty()) {
        result.errorText = pathError;
        return result;
    }

    const QByteArray message = buildReportMessage(result.attachments, skipped, result.sender, result.recipient);
    if (!saveReportFile(result.reportPath, message, &result.errorText)) {
        return result;
    }
    result.localCopySaved = true;
    return result;
}

} // namespace

QString DiagnosticReportSender::recipientEmail()
{
    const QString recipient = loadEmbeddedDefaults().recipient.trimmed();
    return recipient.isEmpty() ? QStringLiteral("") : recipient;
}

QString DiagnosticReportSender::senderEmail()
{
    const ReportSmtpConfig config = loadEmbeddedDefaults();
    const QString from = config.from.trimmed();
    return from.isEmpty() ? config.username.trimmed() : from;
}

DiagnosticReportSender::Result DiagnosticReportSender::createLocalReportOnly(const QString& reason)
{
    Result result = prepareReport();
    if (!result.errorText.isEmpty()) return result;
    result.message = QString::fromUtf8("%1\n\nЗвіт збережено локально:\n%2")
                         .arg(reason.trimmed().isEmpty() ? QString::fromUtf8("Звіт сформовано локально.") : reason.trimmed(),
                              QDir::toNativeSeparators(result.reportPath));
    return result;
}

DiagnosticReportSender::Result DiagnosticReportSender::send()
{
    Result result = prepareReport();
    if (!result.errorText.isEmpty()) return result;

    ReportSmtpConfig smtpConfig = loadEffectiveSmtpConfig(nullptr);
    if (smtpConfig.from.trimmed().isEmpty()) smtpConfig.from = smtpConfig.username.trimmed();
    if (smtpConfig.recipient.trimmed().isEmpty()) smtpConfig.recipient = DiagnosticReportSender::recipientEmail();
    const QStringList missingFields = missingSmtpFields(smtpConfig);
    if (!missingFields.isEmpty()) {
        result.errorText = QString::fromUtf8("Embedded SMTP config неповний: не знайдено поле(я): %1. Звіт збережено локально:\n%2")
                               .arg(missingFields.join(QStringLiteral(", ")),
                                    QDir::toNativeSeparators(result.reportPath));
        return result;
    }
    result.sender = smtpConfig.from;
    result.recipient = smtpConfig.recipient;

    QFile file(result.reportPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorText = QString::fromUtf8("Файл звіту недоступний для читання перед відправкою:\n%1\n%2")
                               .arg(QDir::toNativeSeparators(result.reportPath), file.errorString());
        return result;
    }
    const QByteArray message = file.readAll();
    if (message.isEmpty()) {
        result.errorText = QString::fromUtf8("Файл звіту порожній перед відправкою:\n%1")
                               .arg(QDir::toNativeSeparators(result.reportPath));
        return result;
    }

    QString smtpError;
    if (!sendViaSmtp(message, smtpConfig, &smtpError)) {
        result.errorText = QString::fromUtf8("%1\n\nЗвіт збережено локально:\n%2")
                               .arg(smtpError, QDir::toNativeSeparators(result.reportPath));
        return result;
    }

    result.sent = true;
    result.message = QString::fromUtf8("Звіт відправлено на %1.\nЛокальна копія: %2\nВкладень: %3.")
                         .arg(result.recipient, QDir::toNativeSeparators(result.reportPath))
                         .arg(result.attachments.size());
    return result;
}

