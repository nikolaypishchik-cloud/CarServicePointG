#pragma once

#include "core/services/settings/GeneralSettingsService.h"
#include <QNetworkRequest>
#include <QXmlStreamWriter>

// Preparation only: this module never sends requests or logs their secret body.
namespace ElitSoapRequest {

struct PreparedRequest {
    QNetworkRequest request;
    QByteArray body;
};

inline QUrl documentedEndpoint()
{
    return QUrl(QStringLiteral("https://econnector.elit.cz/InterCompany-1.75.0/BuyerService"));
}

inline bool prepareInvoiceRead(const GeneralSettingsService::ElitCredentials& credentials,
                               int pageNo, int pageSize, PreparedRequest* output,
                               QString* error, bool countOnly)
{
    if (error) error->clear();
    if (!output) return false;
    *output = {};
    if (credentials.company.trimmed().isEmpty() || credentials.login.trimmed().isEmpty()
        || credentials.password.isEmpty() || pageNo < 0 || pageSize <= 0) {
        if (error) *error = QString::fromUtf8("Вкажіть код компанії, логін, пароль і коректні параметри сторінки.");
        return false;
    }
    output->request = QNetworkRequest(documentedEndpoint());
    output->request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml; charset=utf-8"));
    output->request.setRawHeader("SOAPAction", "\"\"");
    output->request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    output->request.setTransferTimeout(15000);
    QXmlStreamWriter xml(&output->body);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("soap:Envelope"));
    xml.writeNamespace(QStringLiteral("http://schemas.xmlsoap.org/soap/envelope/"), QStringLiteral("soap"));
    xml.writeNamespace(QStringLiteral("http://buyer.elit.cz/"), QStringLiteral("buyer"));
    xml.writeStartElement(QStringLiteral("soap:Body"));
    xml.writeStartElement(countOnly ? QStringLiteral("buyer:getNumOfInvoices") : QStringLiteral("buyer:getInvoiceList"));
    xml.writeTextElement(QStringLiteral("company"), credentials.company.trimmed());
    xml.writeTextElement(QStringLiteral("login"), credentials.login.trimmed());
    xml.writeTextElement(QStringLiteral("password"), credentials.password);
    xml.writeTextElement(QStringLiteral("open"), QStringLiteral("true"));
    xml.writeTextElement(QStringLiteral("closed"), QStringLiteral("true"));
    xml.writeTextElement(QStringLiteral("overdue"), QStringLiteral("true"));
    if (!countOnly) {
        xml.writeTextElement(QStringLiteral("pageNo"), QString::number(pageNo));
        xml.writeTextElement(QStringLiteral("pageSize"), QString::number(pageSize));
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    return true;
}

inline bool prepareInvoiceList(const GeneralSettingsService::ElitCredentials& credentials,
                               int pageNo, int pageSize, PreparedRequest* output, QString* error = nullptr)
{
    return prepareInvoiceRead(credentials, pageNo, pageSize, output, error, false);
}

inline bool prepareAccessCheck(const GeneralSettingsService::ElitCredentials& credentials,
                               PreparedRequest* output, QString* error = nullptr)
{
    return prepareInvoiceRead(credentials, 0, 1, output, error, true);
}

inline bool prepareInvoiceDetail(const GeneralSettingsService::ElitCredentials& credentials,
                                 const QString& invoiceNumber, bool vatSummary,
                                 PreparedRequest* output, QString* error = nullptr)
{
    if (!prepareAccessCheck(credentials, output, error)) return false;
    if (invoiceNumber.trimmed().isEmpty()) {
        if (error) *error = QString::fromUtf8("Не вказано номер накладної Еліт.");
        return false;
    }
    output->body.fill('\0');
    output->body.clear();
    QXmlStreamWriter xml(&output->body);
    xml.writeStartDocument();
    xml.writeStartElement("soap:Envelope");
    xml.writeNamespace("http://schemas.xmlsoap.org/soap/envelope/", "soap");
    xml.writeNamespace("http://buyer.elit.cz/", "buyer");
    xml.writeStartElement("soap:Body");
    xml.writeStartElement(vatSummary ? "buyer:getInvoiceVatSummary" : "buyer:getInvoiceLines");
    xml.writeTextElement("company", credentials.company.trimmed());
    xml.writeTextElement("login", credentials.login.trimmed());
    xml.writeTextElement("password", credentials.password);
    xml.writeTextElement("invoiceNo", invoiceNumber);
    xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
    xml.writeEndDocument();
    return true;
}

} // namespace ElitSoapRequest
