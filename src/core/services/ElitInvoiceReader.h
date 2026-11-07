#pragma once

#include "core/services/ElitConnectionCheck.h"
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QVector>
#include <cmath>

// Read-only staging: no database or payment service is used here.
namespace ElitInvoiceReader {
enum class Failure { None, Connection, Timeout, Access, Cancelled, Incomplete, Other };
struct Line {
    QString itemCode, reference, description;
    double quantity = 0;
    qint64 grossCents = 0;
};
struct Invoice {
    QString number, currency;
    QDate posted, due;
    qint64 amountCents = 0, amountVatCents = 0, remainingCents = 0;
    qint64 grossCents = 0;
    bool detailsComplete = false;
    QVector<Line> lines;
};
struct Result {
    bool complete = false;
    QString error;
    Failure failure = Failure::None;
    int availableCount = 0;
    QDate asOf;
    QDateTime fetchedAt;
    QVector<Invoice> invoices;
    QVector<Invoice> allHeaders;
};

struct Options {
    bool details = false;
    QSet<QString> knownNumbers;
    QMap<QString, Invoice> cachedInvoices;
};

inline bool reuseDetails(Invoice* current, const Invoice& saved)
{
    if (!saved.detailsComplete || current->number != saved.number || current->posted != saved.posted
        || current->currency != saved.currency || current->amountCents != saved.amountCents
        || current->amountVatCents != saved.amountVatCents) return false;
    current->grossCents = saved.grossCents;
    current->lines = saved.lines;
    current->detailsComplete = true;
    return true;
}

inline bool money(const QString& text, qint64* cents)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok || !std::isfinite(value) || std::abs(value) > 1.e12) return false;
    *cents = qRound64(value * 100.0);
    return true;
}

inline bool parsePage(const QByteArray& bytes, QVector<Invoice>* output, QString* error)
{
    output->clear();
    error->clear();
    QXmlStreamReader xml(bytes);
    const QString soap = QStringLiteral("http://schemas.xmlsoap.org/soap/envelope/");
    bool envelope = false, body = false, response = false, invalid = false;
    int depth = 0, bodies = 0, responses = 0;
    QMap<QString, QString> fields;
    QVector<Invoice> parsed;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::DTD) { invalid = true; break; }
        if (xml.isStartElement()) {
            ++depth;
            const QString name = xml.name().toString();
            if (depth == 1) envelope = name == "Envelope" && xml.namespaceUri() == soap;
            if (depth == 2) {
                body = envelope && name == "Body" && xml.namespaceUri() == soap;
                if (body) ++bodies;
            }
            if (depth == 3 && body) {
                response = name == "getInvoiceListResponse" && xml.namespaceUri() == "http://buyer.elit.cz/";
                if (response) ++responses;
                else invalid = true;
            }
            if (depth == 4 && response) {
                if (name != "return" || !xml.namespaceUri().isEmpty()) invalid = true;
                fields.clear();
            }
            if (depth == 5 && response) {
                if (!xml.namespaceUri().isEmpty() || fields.contains(name)) invalid = true;
                fields.insert(name, xml.readElementText());
                --depth;
            }
        } else if (xml.isEndElement()) {
            if (depth == 4 && response) {
                Invoice i;
                i.number = fields.value("invoiceNo").trimmed();
                i.currency = fields.value("currency").trimmed().toUpper();
                i.posted = QDateTime::fromString(fields.value("postingDate"), Qt::ISODate).date();
                const auto due = fields.value("dueDate");
                i.due = QDateTime::fromString(due, Qt::ISODate).date();
                if (i.number.isEmpty() || i.currency.isEmpty() || !i.posted.isValid()
                    || (!due.isEmpty() && !i.due.isValid())
                    || !money(fields.value("amount"), &i.amountCents)
                    || !money(fields.value("amountVAT"), &i.amountVatCents)
                    || !money(fields.value("remainingAmount"), &i.remainingCents)) invalid = true;
                parsed.append(i);
            }
            if (depth == 3) response = false;
            if (depth == 2) body = false;
            --depth;
        }
    }
    if (invalid || xml.hasError() || !envelope || bodies != 1 || responses != 1) {
        *error = QString::fromUtf8("Некоректна або неповна відповідь зі списком накладних Еліт. Дані не застосовано.");
        return false;
    }
    *output = parsed;
    return true;
}

inline bool parseDetail(const QByteArray& bytes, Invoice* invoice, bool vatSummary, QString* error)
{
    QXmlStreamReader xml(bytes);
    QVector<QMap<QString, QString>> rows;
    QMap<QString, QString> fields;
    int depth = 0, bodies = 0, responses = 0;
    bool envelope = false, body = false, response = false, invalid = false;
    const QString soap = "http://schemas.xmlsoap.org/soap/envelope/";
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::DTD) { invalid = true; break; }
        if (xml.isStartElement()) {
            ++depth;
            const auto name = xml.name().toString();
            if (depth == 1) envelope = name == "Envelope" && xml.namespaceUri() == soap;
            if (depth == 2) { body = envelope && name == "Body" && xml.namespaceUri() == soap; if (body) ++bodies; }
            if (depth == 3 && body) {
                response = name == (vatSummary ? "getInvoiceVatSummaryResponse" : "getInvoiceLinesResponse")
                    && xml.namespaceUri() == "http://buyer.elit.cz/";
                if (response) ++responses; else invalid = true;
            }
            if (depth == 4 && response) {
                if (name != "return" || !xml.namespaceUri().isEmpty()) invalid = true;
                fields.clear();
            }
            if (depth == 5 && response) {
                if (!xml.namespaceUri().isEmpty() || fields.contains(name)) invalid = true;
                fields.insert(name, xml.readElementText()); --depth;
            }
        } else if (xml.isEndElement()) {
            if (depth == 4 && response) rows.append(fields);
            if (depth == 3) response = false;
            if (depth == 2) body = false;
            --depth;
        }
    }
    if (xml.hasError() || !envelope || bodies != 1 || responses != 1 || rows.isEmpty()) invalid = true;
    qint64 gross = 0;
    QVector<Line> lines;
    for (const auto& row : rows) {
        qint64 amount = 0;
        if (vatSummary) {
            qint64 base = 0, vat = 0;
            if (!money(row.value("inclVat"), &amount) || !money(row.value("vatBase"), &base)
                || !money(row.value("vatAmount"), &vat) || qAbs(amount - base - vat) > 2) invalid = true;
        } else {
            bool ok = false;
            Line line;
            line.itemCode = row.value("no").trimmed();
            line.reference = row.value("crossReferenceNo").trimmed();
            line.description = row.value("description").trimmed();
            line.quantity = row.value("quantity").toDouble(&ok);
            if (!ok || !std::isfinite(line.quantity) || qAbs(line.quantity) > 1.e9
                || row.value("invoiceNo").trimmed() != invoice->number
                || !money(row.value("amountIncludingVat"), &amount)) invalid = true;
            line.grossCents = amount;
            lines.append(line);
        }
        if (rows.size() > 10000 || qAbs(amount) > 100000000000000LL
            || qAbs(gross) > 100000000000000LL) invalid = true;
        gross += amount;
    }
    if (vatSummary && qAbs(gross - invoice->grossCents) > 2) invalid = true;
    if (invalid) {
        *error = QString::fromUtf8("Неповні або суперечливі позиції / суми накладної Еліт. Попередні дані збережено.");
        return false;
    }
    if (vatSummary) invoice->detailsComplete = true;
    else { invoice->lines = lines; invoice->grossCents = gross; }
    return true;
}

struct Collection {
    int expected = 0;
    QVector<Invoice> all;
    QSet<QString> identities;
    bool append(const QVector<Invoice>& page, QString* error) {
        if (page.isEmpty() || page.size() > 100 || all.size() + page.size() > expected) {
            *error = QString::fromUtf8("Кількість накладних не відповідає відповіді Еліт. Повторіть отримання.");
            return false;
        }
        auto next = identities;
        for (const auto& invoice : page) {
            const auto key = invoice.number + QChar(0x1f) + invoice.posted.toString(Qt::ISODate);
            if (next.contains(key)) {
                *error = QString::fromUtf8("Еліт повернув повторні накладні. Повноту списку не підтверджено.");
                return false;
            }
            next.insert(key);
        }
        identities = next;
        all += page;
        return true;
    }
    QVector<Invoice> period(const QDate& today) const {
        QVector<Invoice> result;
        const QDate first(today.year(), today.month(), 1);
        for (const auto& i : all)
            if ((i.posted >= first && i.posted <= today) || (i.posted < first && i.remainingCents > 0)) result.append(i);
        return result;
    }
};

class Job final : public QObject {
public:
    using Callback = std::function<void(Result)>;
    Job(QObject* owner, GeneralSettingsService::ElitCredentials credentials, Callback complete, Options options = {})
        : QObject(owner), credentials_(std::move(credentials)), complete_(std::move(complete)), manager_(this), options_(std::move(options)) {}
    void cancel() { finish(QString::fromUtf8("Синхронізацію з сервером Еліт скасовано."), Failure::Cancelled); }
    void start() {
        asOf_ = QDate::currentDate();
        auto* timeout = new QTimer(this);
        timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, this, [this] {
            finish(QString::fromUtf8("Отримання накладних перевищило 120 секунд. Часткові дані не застосовано."), Failure::Timeout);
        });
        timeout->start(120000);
        requestCount(false);
    }
private:
    GeneralSettingsService::ElitCredentials credentials_;
    Callback complete_;
    QNetworkAccessManager manager_;
    Collection collection_;
    QDate asOf_;
    int page_ = 1;
    bool finished_ = false;
    Options options_;
    QVector<Invoice> selected_;
    int detailIndex_ = 0;
    Collection verification_;
    int verificationPage_ = 1;

    void finish(const QString& error = {}, Failure failure = Failure::Incomplete) {
        if (finished_) return;
        finished_ = true;
        Result result;
        result.error = error;
        result.failure = error.isEmpty() ? Failure::None : failure;
        result.complete = error.isEmpty();
        result.asOf = asOf_;
        if (result.complete) {
            result.availableCount = collection_.expected;
            result.invoices = options_.details ? selected_ : collection_.period(asOf_);
            result.allHeaders = collection_.all;
            result.fetchedAt = QDateTime::currentDateTime();
        }
        for (auto* reply : manager_.findChildren<QNetworkReply*>()) reply->abort();
        auto callback = std::move(complete_);
        deleteLater();
        callback(result);
    }
    void send(ElitSoapRequest::PreparedRequest prepared, std::function<void(QByteArray)> receive) {
        if (finished_) return;
        auto* reply = manager_.post(prepared.request, prepared.body);
        prepared.body.fill('\0');
        auto bytes = std::make_shared<QByteArray>();
        auto* deadline = new QTimer(reply);
        deadline->setSingleShot(true);
        connect(deadline, &QTimer::timeout, this, [this] {
            finish(QString::fromUtf8("Еліт не відповів за 15 секунд. Дані не застосовано."), Failure::Timeout);
        });
        connect(reply, &QNetworkReply::readyRead, this, [this, reply, bytes] {
            if (finished_) return;
            *bytes += reply->read(4 * 1024 * 1024 + 1 - bytes->size());
            if (bytes->size() > 4 * 1024 * 1024)
                finish(QString::fromUtf8("Відповідь Еліт завелика. Отримання зупинено."));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, deadline, receive] {
            deadline->stop();
            reply->deleteLater();
            if (finished_) return;
            *bytes += reply->readAll();
            if (bytes->size() > 4 * 1024 * 1024) { finish(QString::fromUtf8("Відповідь Еліт завелика.")); return; }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || status != 200) {
                finish(QString::fromUtf8("Не вдалося отримати накладні Еліт (HTTP %1, мережева помилка %2). Перевірте доступ і підключення. Дані не застосовано.")
                    .arg(status).arg(int(reply->error())),
                    status == 401 || status == 403 ? Failure::Access
                    : reply->error() == QNetworkReply::TimeoutError ? Failure::Timeout
                    : status == 0 ? Failure::Connection : Failure::Other);
                return;
            }
            receive(*bytes);
        });
        deadline->start(15000);
    }
    void requestCount(bool finalCheck) {
        ElitSoapRequest::PreparedRequest prepared;
        QString error;
        if (!ElitSoapRequest::prepareAccessCheck(credentials_, &prepared, &error)) { finish(error,Failure::Other); return; }
        send(prepared, [this, finalCheck](const QByteArray& bytes) {
            const auto checked = ElitConnectionCheck::parseResponse(bytes, 200, credentials_);
            if (!checked.accessible) { finish(checked.message,Failure::Other); return; }
            QXmlStreamReader xml(bytes);
            int count = -1;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == "return") count = xml.readElementText().toInt();
            }
            if (count < 0 || count > 10000 || (finalCheck && count != collection_.expected)) {
                finish(QString::fromUtf8("Список накладних змінився або перевищує ліміт 10000 документів. Дані не застосовано.")); return;
            }
            if (finalCheck) {
                if (!options_.details) { finish(); return; }
                QSet<QString> uniqueNumbers;
                for (const auto& invoice : collection_.all) {
                    if (uniqueNumbers.contains(invoice.number)) {
                        finish(QString::fromUtf8("Еліт повернув неоднозначні номери накладних. Деталізацію не застосовано.")); return;
                    }
                    uniqueNumbers.insert(invoice.number);
                }
                selected_ = collection_.all;
                QSet<QString> selectedNumbers;
                for (const auto& i : selected_) selectedNumbers.insert(i.number);
                for (const auto& i : collection_.all)
                    if (options_.knownNumbers.contains(i.number) && !selectedNumbers.contains(i.number)) {
                        selected_.append(i); selectedNumbers.insert(i.number);
                    }
                nextDetail(false); return;
            }
            collection_.expected = count;
            if (count == 0) requestCount(true);
            else nextPage();
        });
    }
    void nextPage() {
        ElitSoapRequest::PreparedRequest prepared;
        QString error;
        if (!ElitSoapRequest::prepareInvoiceList(credentials_, page_, 100, &prepared, &error)) { finish(error); return; }
        send(prepared, [this](const QByteArray& bytes) {
            QVector<Invoice> page;
            QString error;
            if (!parsePage(bytes, &page, &error) || !collection_.append(page, &error)) { finish(error); return; }
            if (collection_.all.size() == collection_.expected) requestCount(true);
            else { ++page_; nextPage(); }
        });
    }
    void nextDetail(bool vat) {
        while (!vat && detailIndex_ < selected_.size()
               && options_.cachedInvoices.contains(selected_[detailIndex_].number)) {
            if (!reuseDetails(&selected_[detailIndex_], options_.cachedInvoices.value(selected_[detailIndex_].number))) {
                finish(QString::fromUtf8("Змінилися реквізити збереженої накладної Еліт %1. Потрібна звірка; повторне завантаження не виконано.").arg(selected_[detailIndex_].number));
                return;
            }
            ++detailIndex_;
        }
        if (detailIndex_ >= selected_.size()) {
            verification_.expected = collection_.expected;
            verifyHeaders(); return;
        }
        ElitSoapRequest::PreparedRequest prepared;
        QString error;
        if (!ElitSoapRequest::prepareInvoiceDetail(credentials_, selected_[detailIndex_].number, vat, &prepared, &error)) {
            finish(error); return;
        }
        send(prepared, [this, vat](const QByteArray& bytes) {
            QString error;
            if (!parseDetail(bytes, &selected_[detailIndex_], vat, &error)) { finish(error); return; }
            if (vat) { ++detailIndex_; nextDetail(false); }
            else nextDetail(true);
        });
    }
    void verifyHeaders() {
        if (verification_.all.size() == collection_.all.size()) {
            QMap<QString, Invoice> original;
            for (const auto& i : collection_.all) original.insert(i.number + i.posted.toString(Qt::ISODate), i);
            for (const auto& i : verification_.all) {
                const auto before = original.value(i.number + i.posted.toString(Qt::ISODate));
                if (before.number != i.number || before.currency != i.currency || before.due != i.due
                    || before.amountCents != i.amountCents || before.amountVatCents != i.amountVatCents
                    || before.remainingCents != i.remainingCents) {
                    finish(QString::fromUtf8("Накладні або оплати Еліт змінилися під час отримання. Повторіть синхронізацію.")); return;
                }
            }
            ElitSoapRequest::PreparedRequest prepared; QString error;
            if (!ElitSoapRequest::prepareAccessCheck(credentials_, &prepared, &error)) { finish(error); return; }
            send(prepared, [this](const QByteArray& bytes) {
                const auto checked = ElitConnectionCheck::parseResponse(bytes, 200, credentials_);
                if (!checked.accessible) { finish(checked.message); return; }
                QXmlStreamReader xml(bytes); int count = -1;
                while (!xml.atEnd()) {
                    xml.readNext();
                    if (xml.isStartElement() && xml.name() == "return") count = xml.readElementText().toInt();
                }
                if (count != collection_.expected) { finish(QString::fromUtf8("Список Еліт змінився. Повторіть синхронізацію.")); return; }
                finish();
            });
            return;
        }
        ElitSoapRequest::PreparedRequest prepared; QString error;
        if (!ElitSoapRequest::prepareInvoiceList(credentials_, verificationPage_, 100, &prepared, &error)) { finish(error); return; }
        send(prepared, [this](const QByteArray& bytes) {
            QVector<Invoice> page; QString error;
            if (!parsePage(bytes, &page, &error) || !verification_.append(page, &error)) { finish(error); return; }
            ++verificationPage_; verifyHeaders();
        });
    }
};
inline Job* start(QObject* owner, const GeneralSettingsService::ElitCredentials& credentials, Job::Callback complete, Options options = {})
{
    auto* job = new Job(owner, credentials, std::move(complete), std::move(options));
    job->start();
    return job;
}
} // namespace ElitInvoiceReader
