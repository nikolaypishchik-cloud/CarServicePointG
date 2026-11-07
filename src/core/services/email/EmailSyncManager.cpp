#include "core/services/email/EmailSyncManager.h"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QSslError>
#include <QSslSocket>
#include <QTextStream>
#include <QVariant>
#include <QDebug>

namespace {
constexpr int kDefaultPollIntervalMs = 60000;
constexpr int kMinPollIntervalMs = 30000;
constexpr int kMaxPollIntervalMs = 600000;

QString cleanAccount(QString account)
{
    return account.trimmed().toLower();
}

QString cleanAppPassword(QString password)
{
    password = password.trimmed();
    password.remove(QRegularExpression(QStringLiteral("\\s+")));
    return password;
}

QString readPasswordFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return cleanAppPassword(QString::fromUtf8(file.readAll()));
}

QString loadAppPasswordFromLocalFiles()
{
    QStringList candidates;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cwd = QDir::currentPath();
    candidates << QDir(appDir).filePath(QStringLiteral("gmail-app-password.txt"));
    candidates << QDir(appDir).filePath(QStringLiteral("email-app-password.txt"));
    candidates << QDir(appDir).filePath(QStringLiteral("config/gmail-app-password.txt"));
    candidates << QDir(cwd).filePath(QStringLiteral("gmail-app-password.txt"));
    candidates << QDir(cwd).filePath(QStringLiteral("email-app-password.txt"));
    for (const QString& candidate : candidates) {
        const QString password = readPasswordFile(candidate);
        if (!password.isEmpty()) return password;
    }
    return {};
}

quint64 firstIntegerAfterUidNext(const QString& line)
{
    static const QRegularExpression re(QStringLiteral("\\bUIDNEXT\\s+(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch()) return 0;
    bool ok = false;
    const quint64 v = m.captured(1).toULongLong(&ok);
    return ok ? v : 0;
}

quint64 maxSearchUid(const QString& line)
{
    if (!line.startsWith(QStringLiteral("* SEARCH"), Qt::CaseInsensitive)) return 0;
    quint64 maxUid = 0;
    static const QRegularExpression re(QStringLiteral("\\b(\\d+)\\b"));
    QRegularExpressionMatchIterator it = re.globalMatch(line);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool ok = false;
        const quint64 v = m.captured(1).toULongLong(&ok);
        if (ok && v > maxUid) maxUid = v;
    }
    return maxUid;
}

QString compactSslErrorList(const QList<QSslError>& errors)
{
    QStringList parts;
    for (const QSslError& error : errors) {
        const QString text = error.errorString().trimmed();
        if (!text.isEmpty()) parts << text;
    }
    return parts.join(QStringLiteral("; "));
}
}

EmailSyncManager::EmailSyncManager(QObject* parent)
    : QObject(parent)
{
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, [this]() { syncNow(); });
}

EmailSyncManager::~EmailSyncManager()
{
    stop();
}

void EmailSyncManager::setNotificationCallback(NotificationCallback callback)
{
    m_notificationCallback = std::move(callback);
}

void EmailSyncManager::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("EmailSync"));

    m_account = cleanAccount(settings.value(QStringLiteral("account"), QStringLiteral("")).toString());
    const QString envAccount = cleanAccount(qEnvironmentVariable("CSPG_GMAIL_ACCOUNT"));
    if (!envAccount.isEmpty()) m_account = envAccount;

    m_password = cleanAppPassword(qEnvironmentVariable("CARSERVICE_GMAIL_APP_PASSWORD"));
    if (m_password.isEmpty()) m_password = cleanAppPassword(qEnvironmentVariable("CSPG_GMAIL_APP_PASSWORD"));
    if (m_password.isEmpty()) {
        m_password = cleanAppPassword(settings.value(QStringLiteral("appPassword")).toString());
    }
    if (m_password.isEmpty()) {
        m_password = loadAppPasswordFromLocalFiles();
    }

    m_lastViewedUid = settings.value(QStringLiteral("lastViewedUid"), 0).toULongLong();
    m_pollIntervalMs = settings.value(QStringLiteral("pollIntervalMs"), kDefaultPollIntervalMs).toInt();
    m_pollIntervalMs = qBound(kMinPollIntervalMs, m_pollIntervalMs, kMaxPollIntervalMs);
    m_enabled = settings.value(QStringLiteral("enabled"), true).toBool()
            && !m_account.isEmpty()
            && !m_password.isEmpty();
}

void EmailSyncManager::start()
{
    loadSettings();
    if (!m_enabled) {
        QSettings settings;
        settings.beginGroup(QStringLiteral("EmailSync"));
        const bool enabledSetting = settings.value(QStringLiteral("enabled"), true).toBool();
        qInfo() << "EmailSyncManager disabled"
                << "account_configured=" << !m_account.isEmpty()
                << "password_configured=" << !m_password.isEmpty()
                << "enabled_setting=" << enabledSetting;
        return;
    }

    m_timer.setInterval(m_pollIntervalMs);
    if (!m_timer.isActive()) m_timer.start();
    qInfo() << "EmailSyncManager started" << m_account << "poll_ms=" << m_pollIntervalMs;
    QTimer::singleShot(2500, this, [this]() { syncNow(); });
}

void EmailSyncManager::stop()
{
    m_timer.stop();
    m_enabled = false;
    resetSocket();
}

void EmailSyncManager::syncNow()
{
    loadSettings();
    if (!m_enabled || m_requestInFlight) return;

    m_timer.setInterval(m_pollIntervalMs);
    openSocket();
}

void EmailSyncManager::markViewed(quint64 uid)
{
    if (uid == 0) return;
    storeLastViewedUid(uid);
    if (m_postponedUid <= uid) {
        m_postponedUid = 0;
        m_postponedUntil = QDateTime();
    }
}

void EmailSyncManager::postpone(quint64 uid, int minutes)
{
    if (uid == 0) return;
    m_postponedUid = uid;
    m_postponedUntil = QDateTime::currentDateTimeUtc().addSecs(qBound(1, minutes, 240) * 60);
}

void EmailSyncManager::storeLastViewedUid(quint64 uid)
{
    if (uid <= m_lastViewedUid) return;
    m_lastViewedUid = uid;
    QSettings settings;
    settings.beginGroup(QStringLiteral("EmailSync"));
    settings.setValue(QStringLiteral("account"), m_account.isEmpty() ? QStringLiteral("") : m_account);
    settings.setValue(QStringLiteral("lastViewedUid"), QVariant::fromValue<qulonglong>(static_cast<qulonglong>(uid)));
}

void EmailSyncManager::resetSocket()
{
    if (!m_socket) return;
    QSslSocket* socket = m_socket;
    m_socket = nullptr;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}

void EmailSyncManager::openSocket()
{
    resetSocket();
    m_requestInFlight = true;
    m_stage = Stage::WaitingGreeting;
    m_rx.clear();
    m_uidNext = 0;
    m_candidateUid = 0;
    m_currentTag.clear();

    m_socket = new QSslSocket(this);
    connect(m_socket, &QSslSocket::readyRead, this, [this]() { processIncomingData(); });
    connect(m_socket, &QSslSocket::encrypted, this, []() {
        qInfo() << "EmailSyncManager TLS session established";
    });
    connect(m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError>& errors) {
        failCycle(QStringLiteral("TLS error: %1").arg(compactSslErrorList(errors)));
    });
    connect(m_socket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        const QString errorText = m_socket ? m_socket->errorString() : QStringLiteral("socket error");
        failCycle(errorText);
    });

    qInfo() << "EmailSyncManager connecting to Gmail IMAP" << m_account;
    m_socket->connectToHostEncrypted(QStringLiteral("imap.gmail.com"), 993);
}

void EmailSyncManager::processIncomingData()
{
    if (!m_socket) return;
    m_rx += m_socket->readAll();

    while (true) {
        int lf = m_rx.indexOf('\n');
        if (lf < 0) break;
        QByteArray raw = m_rx.left(lf);
        m_rx.remove(0, lf + 1);
        if (raw.endsWith('\r')) raw.chop(1);
        const QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty()) processLine(line);
    }
}

void EmailSyncManager::processLine(const QString& line)
{
    if (line.startsWith(QStringLiteral("* STATUS"), Qt::CaseInsensitive)) {
        const quint64 uidNext = firstIntegerAfterUidNext(line);
        if (uidNext > 0) m_uidNext = uidNext;
    } else if (line.startsWith(QStringLiteral("* SEARCH"), Qt::CaseInsensitive)) {
        const quint64 uid = maxSearchUid(line);
        if (uid > m_candidateUid) m_candidateUid = uid;
    }

    if (m_stage == Stage::WaitingGreeting && line.startsWith(QStringLiteral("* OK"), Qt::CaseInsensitive)) {
        sendCommand(QStringLiteral("LOGIN %1 %2").arg(quoteImapValue(m_account), quoteImapValue(m_password)), Stage::Login);
        return;
    }

    if (!m_currentTag.isEmpty() && line.startsWith(m_currentTag + QLatin1Char(' '))) {
        processTaggedResponse(line);
    }
}

void EmailSyncManager::processTaggedResponse(const QString& line)
{
    const bool ok = line.contains(QStringLiteral(" OK"), Qt::CaseInsensitive);
    const bool noOrBad = line.contains(QStringLiteral(" NO"), Qt::CaseInsensitive)
            || line.contains(QStringLiteral(" BAD"), Qt::CaseInsensitive);
    if (noOrBad && !ok) {
        failCycle(QStringLiteral("IMAP command failed: %1").arg(line.left(220)));
        return;
    }
    if (!ok) return;

    switch (m_stage) {
    case Stage::Login:
        sendCommand(QStringLiteral("STATUS \"INBOX\" (UIDNEXT)"), Stage::Status);
        break;
    case Stage::Status:
        applyUidNext();
        break;
    case Stage::Select:
        if (m_lastViewedUid + 1 >= m_uidNext) {
            requestLogout();
        } else {
            sendCommand(QStringLiteral("UID SEARCH UID %1:*").arg(QString::number(m_lastViewedUid + 1)), Stage::Search);
        }
        break;
    case Stage::Search:
        publishNotification(m_candidateUid);
        break;
    case Stage::Logout:
        completeCycle();
        break;
    case Stage::Idle:
    case Stage::WaitingGreeting:
        break;
    }
}

void EmailSyncManager::applyUidNext()
{
    if (m_uidNext == 0) {
        requestLogout();
        return;
    }

    if (m_lastViewedUid == 0) {
        const quint64 newestExistingUid = m_uidNext > 1 ? m_uidNext - 1 : 0;
        if (newestExistingUid == 0) {
            requestLogout();
            return;
        }
        // First configured run: notify about the newest message instead of silently
        // advancing the baseline. This prevents missed service-site emails when the
        // app was launched after the site had already delivered a booking letter.
        m_lastViewedUid = newestExistingUid - 1;
        sendCommand(QStringLiteral("SELECT \"INBOX\""), Stage::Select);
        return;
    }

    if (m_lastViewedUid + 1 >= m_uidNext) {
        requestLogout();
        return;
    }

    sendCommand(QStringLiteral("SELECT \"INBOX\""), Stage::Select);
}

void EmailSyncManager::publishNotification(quint64 uid)
{
    if (uid <= m_lastViewedUid) {
        requestLogout();
        return;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (m_postponedUid == uid && m_postponedUntil.isValid() && nowUtc < m_postponedUntil) {
        requestLogout();
        return;
    }

    qInfo() << "EmailSyncManager new message detected" << "uid=" << uid << "account=" << m_account;
    if (m_notificationCallback) {
        EmailNotification notification;
        notification.uid = uid;
        notification.account = m_account;
        m_notificationCallback(notification);
    }
    requestLogout();
}

void EmailSyncManager::sendCommand(const QString& command, Stage nextStage)
{
    if (!m_socket) return;
    ++m_tagCounter;
    m_currentTag = QStringLiteral("A%1").arg(m_tagCounter, 4, 10, QLatin1Char('0'));
    m_stage = nextStage;
    const QByteArray payload = (m_currentTag + QLatin1Char(' ') + command + QStringLiteral("\r\n")).toUtf8();
    m_socket->write(payload);
}

void EmailSyncManager::requestLogout()
{
    if (!m_socket) {
        completeCycle();
        return;
    }
    sendCommand(QStringLiteral("LOGOUT"), Stage::Logout);
}

void EmailSyncManager::completeCycle()
{
    m_requestInFlight = false;
    m_stage = Stage::Idle;
    m_currentTag.clear();
    resetSocket();
}

void EmailSyncManager::failCycle(const QString& reason)
{
    if (!m_requestInFlight && !m_socket) return;
    qWarning() << "EmailSyncManager failed" << reason;
    m_requestInFlight = false;
    m_stage = Stage::Idle;
    m_currentTag.clear();
    resetSocket();
}

QString EmailSyncManager::quoteImapValue(const QString& value) const
{
    QString s = value;
    s.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    s.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + s + QLatin1Char('"');
}
