#include "core/util/DateFormats.h"
#include "ui/common/dialogs/PartRequestsDialog.h"

#include "core/cloudsync/CloudSyncClient.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QIcon>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>
#include <memory>

namespace {

QString displayDateTime(const QString& value)
{
    const QDateTime parsed = DateFormats::parseDateTime(value.trimmed());
    return parsed.isValid() ? parsed.toLocalTime().toString(QStringLiteral("dd.MM.yy HH:mm")) : value.trimmed();
}

QString requestStatusText(const QString& status)
{
    const QString value = status.trimmed().toLower();
    if (value == QStringLiteral("pending")) return QString::fromUtf8("Очікує замовлення");
    if (value == QStringLiteral("needs_clarification")) return QString::fromUtf8("Потрібне уточнення");
    if (value == QStringLiteral("approved")) return QString::fromUtf8("Схвалено");
    if (value == QStringLiteral("ordered")) return QString::fromUtf8("Замовлено");
    if (value == QStringLiteral("cancelled")) return QString::fromUtf8("Скасовано");
    return status.trimmed();
}

QString vehicleText(const QJsonObject& item)
{
    QStringList parts;
    const QString makeModel = (item.value(QStringLiteral("vehicle_make")).toString().trimmed()
        + QLatin1Char(' ') + item.value(QStringLiteral("vehicle_model")).toString().trimmed()).trimmed();
    if (!makeModel.isEmpty()) parts.push_back(makeModel);
    const QString plate = item.value(QStringLiteral("plate")).toString().trimmed();
    if (!plate.isEmpty()) parts.push_back(plate);
    return parts.isEmpty() ? QString::fromUtf8("Авто не вказано") : parts.join(QStringLiteral(" | "));
}

QTableWidgetItem* cell(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

PartRequestsDialog::PartRequestsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("Заявки майстрів на запчастини"));
    setWindowIcon(QIcon(QStringLiteral(":/assets/app_icon.png")));
    setModal(true);
    resize(1320, 720);
    setMinimumSize(980, 520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(10);

    auto* title = new QLabel(QString::fromUtf8("Заявки майстрів на запчастини"), this);
    title->setStyleSheet(QStringLiteral("font-size:22pt; font-weight:800; color:#ffffff;"));
    root->addWidget(title);

    m_status = new QLabel(QString::fromUtf8("Завантаження заявок..."), this);
    m_status->setStyleSheet(QStringLiteral("color:#d9e5ef; font-size:11pt;"));
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(11);
    m_table->setHorizontalHeaderLabels({
        QString::fromUtf8("Дата / час"), QString::fromUtf8("Майстер"), QString::fromUtf8("Наряд"),
        QString::fromUtf8("Авто"), QString::fromUtf8("Вузол"), QString::fromUtf8("Агрегат"),
        QString::fromUtf8("Запчастина"), QString::fromUtf8("К-сть"), QString::fromUtf8("Коментар"),
        QString::fromUtf8("Статус"), QString::fromUtf8("Дія")
    });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(38);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    const QList<int> widths = {130, 170, 75, 190, 150, 170, 230, 70, 260, 145, 145};
    for (int column = 0; column < widths.size(); ++column) m_table->setColumnWidth(column, widths.at(column));
    m_table->setStyleSheet(QStringLiteral(
        "QTableWidget{background:#f8fbff;color:#172735;gridline-color:#c8d5e1;border:1px solid #90aabe;}"
        "QHeaderView::section{background:#dbe7f1;color:#172735;font-weight:800;padding:7px;border:1px solid #b7c8d6;}"
        "QTableWidget::item:selected{background:#cfe1ff;color:#172735;}"
        "QPushButton{background:#162434;color:#ffffff;border:1px solid #a7c4d8;border-radius:5px;padding:6px 10px;font-weight:800;}"
        "QPushButton:hover{background:#274d68;}"
        "QPushButton:disabled{background:#7b8792;color:#d7dce0;border-color:#9ba6ad;}"));
    root->addWidget(m_table, 1);

    auto* controls = new QHBoxLayout();
    m_refreshButton = new QPushButton(QString::fromUtf8("Оновити"), this);
    auto* closeButton = new QPushButton(QString::fromUtf8("Закрити"), this);
    controls->addWidget(m_refreshButton);
    controls->addStretch(1);
    controls->addWidget(closeButton);
    root->addLayout(controls);

    setStyleSheet(QStringLiteral("PartRequestsDialog{background:#263b4d;}"));
    connect(m_refreshButton, &QPushButton::clicked, this, &PartRequestsDialog::refreshRequests);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    refreshRequests();
}

void PartRequestsDialog::setInitialRequest(const QString& requestUid)
{
    m_initialRequestUid = requestUid.trimmed();
}

void PartRequestsDialog::refreshRequests()
{
    if (m_loading) return;
    m_loading = true;
    m_refreshButton->setEnabled(false);
    m_status->setText(QString::fromUtf8("Оновлюємо заявки з сервера..."));

    struct Result { QJsonObject body; QString error; };
    auto result = std::make_shared<Result>();
    QPointer<PartRequestsDialog> guard(this);
    auto* thread = QThread::create([result]() {
        result->body = CloudSyncClient::listPartRequestsInWorker(&result->error);
    });
    connect(thread, &QThread::finished, this, [guard, result]() {
        if (!guard) return;
        guard->m_loading = false;
        guard->m_refreshButton->setEnabled(true);
        if (!result->error.isEmpty()) {
            guard->m_status->setText(QString::fromUtf8("Не вдалося отримати заявки: %1").arg(result->error));
            return;
        }
        const QJsonArray items = result->body.value(QStringLiteral("items")).toArray();
        guard->renderRequests(items, result->body.value(QStringLiteral("can_decide")).toBool());
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void PartRequestsDialog::renderRequests(const QJsonArray& items, bool canDecide)
{
    m_table->setRowCount(0);
    int initialRow = -1;
    for (const QJsonValue& value : items) {
        if (!value.isObject()) continue;
        const QJsonObject item = value.toObject();
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        if (!m_initialRequestUid.isEmpty()
            && item.value(QStringLiteral("part_request_uid")).toString().trimmed() == m_initialRequestUid) {
            initialRow = row;
        }
        m_table->setItem(row, 0, cell(displayDateTime(item.value(QStringLiteral("created_at")).toString())));
        m_table->setItem(row, 1, cell(item.value(QStringLiteral("requested_by_name")).toString()));
        m_table->setItem(row, 2, cell(QString::fromUtf8("№%1").arg(item.value(QStringLiteral("order_number")).toString())));
        m_table->setItem(row, 3, cell(vehicleText(item)));
        m_table->setItem(row, 4, cell(item.value(QStringLiteral("node_group")).toString()));
        m_table->setItem(row, 5, cell(item.value(QStringLiteral("component")).toString()));
        m_table->setItem(row, 6, cell(item.value(QStringLiteral("part_name")).toString()));
        m_table->setItem(row, 7, cell(QString::number(item.value(QStringLiteral("quantity")).toDouble(1.0), 'f', 2)));
        const QString comment = item.value(QStringLiteral("admin_comment")).toString().trimmed().isEmpty()
            ? item.value(QStringLiteral("comment")).toString()
            : item.value(QStringLiteral("admin_comment")).toString();
        m_table->setItem(row, 8, cell(comment));
        const QString state = item.value(QStringLiteral("status")).toString();
        m_table->setItem(row, 9, cell(requestStatusText(state)));

        const bool canOrder = canDecide && (state == QStringLiteral("pending")
            || state == QStringLiteral("needs_clarification") || state == QStringLiteral("approved"));
        if (canOrder) {
            auto* orderButton = new QPushButton(QString::fromUtf8("Замовлено"), m_table);
            orderButton->setToolTip(QString::fromUtf8("Підтвердити, що адміністратор замовив запчастину"));
            const QString requestUid = item.value(QStringLiteral("part_request_uid")).toString();
            const QString partName = item.value(QStringLiteral("part_name")).toString();
            connect(orderButton, &QPushButton::clicked, this, [this, requestUid, partName]() { orderRequest(requestUid, partName); });
            m_table->setCellWidget(row, 10, orderButton);
        }
    }
    if (initialRow >= 0) {
        m_table->selectRow(initialRow);
        m_table->scrollToItem(m_table->item(initialRow, 0), QAbstractItemView::PositionAtCenter);
        m_initialRequestUid.clear();
    }
    m_status->setText(items.isEmpty()
        ? QString::fromUtf8("Нових заявок немає.")
        : QString::fromUtf8("Заявок: %1. Натисніть «Замовлено», коли позицію фактично оформлено.").arg(m_table->rowCount()));
}

void PartRequestsDialog::orderRequest(const QString& requestUid, const QString& partName)
{
    if (requestUid.trimmed().isEmpty() || m_loading) return;
    const auto answer = QMessageBox::question(this, QString::fromUtf8("Підтвердити замовлення"),
        QString::fromUtf8("Позначити запчастину «%1» як замовлену? Майстер побачить цей статус у мобільному застосунку.").arg(partName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    m_loading = true;
    m_refreshButton->setEnabled(false);
    m_status->setText(QString::fromUtf8("Фіксуємо замовлення..."));
    struct Result { bool ok = false; QString error; };
    auto result = std::make_shared<Result>();
    QPointer<PartRequestsDialog> guard(this);
    auto* thread = QThread::create([requestUid, result]() {
        result->ok = CloudSyncClient::orderPartRequestInWorker(requestUid, &result->error);
    });
    connect(thread, &QThread::finished, this, [guard, result]() {
        if (!guard) return;
        guard->m_loading = false;
        guard->m_refreshButton->setEnabled(true);
        if (!result->ok || !result->error.isEmpty()) {
            guard->m_status->setText(QString::fromUtf8("Не вдалося зафіксувати замовлення: %1").arg(result->error));
            return;
        }
        emit guard->requestOrdered();
        guard->refreshRequests();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
