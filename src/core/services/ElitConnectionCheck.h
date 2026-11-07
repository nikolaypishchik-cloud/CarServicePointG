#pragma once

#include "core/services/ElitSoapRequest.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QXmlStreamReader>
#include <functional>
#include <memory>

namespace ElitConnectionCheck {
struct Result {
    bool accessible = false;
    QString message;
};

inline Result parseResponse(const QByteArray& bytes, int httpStatus,
                            const GeneralSettingsService::ElitCredentials& credentials)
{
    QXmlStreamReader xml(bytes);
    QString fault;
    bool faultSeen = false, response = false, validCount = false;
    int depth = 0, bodies = 0, responses = 0, returns = 0;
    bool envelope = false, body = false;
    const QString soap = QStringLiteral("http://schemas.xmlsoap.org/soap/envelope/");
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::DTD) {
            return {false, QString::fromUtf8("Відповідь Еліт містить неприпустимий DTD.")};
        }
        if (xml.isStartElement()) {
            ++depth;
            const auto name = xml.name();
            if (depth == 1) envelope = name == QStringLiteral("Envelope") && xml.namespaceUri() == soap;
            if (depth == 2) {
                body = envelope && name == QStringLiteral("Body") && xml.namespaceUri() == soap;
                if (body) ++bodies;
            }
            if (depth == 3 && body) {
                response = name == QStringLiteral("getNumOfInvoicesResponse")
                    && xml.namespaceUri() == QStringLiteral("http://buyer.elit.cz/");
                if (response) ++responses;
                if (name == QStringLiteral("Fault") && xml.namespaceUri() == soap) faultSeen = true;
            }
            if (depth == 4 && response && name == QStringLiteral("return") && xml.namespaceUri().isEmpty()) {
                const QString value = xml.readElementText().trimmed();
                --depth;
                bool ok = false;
                const int count = value.toInt(&ok);
                validCount = ok && count >= 0;
                ++returns;
            } else if (faultSeen && (name == QStringLiteral("faultstring") || name == QStringLiteral("message"))) {
                const QString text = xml.readElementText();
                --depth;
                if (!text.isEmpty()) fault += (fault.isEmpty() ? QString() : QStringLiteral("\n")) + text;
            }
        } else if (xml.isEndElement()) {
            if (depth == 3) response = false;
            if (depth == 2) body = false;
            --depth;
        }
    }
    if (!xml.hasError() && faultSeen) {
        for (const auto& secret : {credentials.password, credentials.login, credentials.apiKey}) {
            if (!secret.isEmpty()) {
                fault.replace(secret, QStringLiteral("[приховано]"));
                fault.replace(secret.toHtmlEscaped(), QStringLiteral("[приховано]"));
            }
        }
        return {false, QString::fromUtf8("Доступ не підтверджено. SOAP-помилка Еліт:\n%1")
            .arg(fault.isEmpty() ? QString::fromUtf8("Сервер не надав опису причини.") : fault.left(1200))};
    }
    if (httpStatus == 401 || httpStatus == 403)
        return {false, QString::fromUtf8("Доступ відхилено (HTTP %1). Перевірте облікові дані та активацію API. Сервер не уточнив, чи причина в коді компанії, паролі або обмеженні IP.").arg(httpStatus)};
    if (httpStatus >= 300 && httpStatus < 400)
        return {false, QString::fromUtf8("Сервер перенаправляє запит. Перенаправлення заблоковано, щоб не передавати пароль іншій адресі.")};
    if (httpStatus == 429)
        return {false, QString::fromUtf8("Перевищено ліміт запитів Еліт (HTTP 429). Спробуйте пізніше.")};
    if (httpStatus < 200 || httpStatus >= 300)
        return {false, QString::fromUtf8("Помилка сервера Еліт (HTTP %1). Доступ не перевірено.").arg(httpStatus)};
    if (xml.hasError() || !envelope || bodies != 1 || responses != 1 || returns != 1 || !validCount)
        return {false, QString::fromUtf8("Сервер повернув неочікувану відповідь замість кількості накладних. Доступ не підтверджено.")};
    return {true, QString::fromUtf8("Доступ до читання накладних Еліт підтверджено (getNumOfInvoices).\nЗамовлення не створювалися, робоча база не змінювалася.")};
}

// Owned by the settings page: closing it cancels the outstanding request.
inline void start(QObject* owner, const GeneralSettingsService::ElitCredentials& credentials,
                  std::function<void(Result)> complete)
{
    ElitSoapRequest::PreparedRequest prepared;
    QString error;
    if (!ElitSoapRequest::prepareAccessCheck(credentials, &prepared, &error)) {
        complete({false, error});
        return;
    }
    auto* manager = new QNetworkAccessManager(owner);
    auto* reply = manager->post(prepared.request, prepared.body);
    prepared.body.fill('\0');
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    struct State { QByteArray body; bool timedOut = false; bool tooLarge = false; };
    auto state = std::make_shared<State>();
    QObject::connect(deadline, &QTimer::timeout, reply, [reply, state]() {
        state->timedOut = true;
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, state]() {
        state->body += reply->read(65537 - state->body.size());
        if (state->body.size() > 65536) {
            state->tooLarge = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, owner, [=]() {
        deadline->stop();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Result result;
        if (state->timedOut || reply->error() == QNetworkReply::TimeoutError)
            result.message = QString::fromUtf8("Еліт не відповів за 15 секунд. Перевірте інтернет і повторіть пізніше.");
        else if (state->tooLarge)
            result.message = QString::fromUtf8("Відповідь Еліт перевищила допустимий розмір. Перевірку зупинено.");
        else if (reply->error() == QNetworkReply::SslHandshakeFailedError)
            result.message = QString::fromUtf8("Помилка захищеного TLS-з'єднання. Сертифікат не підтверджено; захист не вимикався.");
        else if (reply->error() != QNetworkReply::NoError && (status == 0 || (status >= 200 && status < 300)))
            result.message = QString::fromUtf8("Мережева помилка (%1): неможливо завершити запит до Еліт. Перевірте інтернет, DNS або проксі.").arg(int(reply->error()));
        else
            result = parseResponse(state->body, status, credentials);
        manager->deleteLater();
        complete(result);
    });
    deadline->start(15000);
}
} // namespace ElitConnectionCheck
