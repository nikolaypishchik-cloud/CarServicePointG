#include "core/util/DateFormats.h"
#include "ui/tabs/statistics/StatsPage.h"
#include "runtime/AsyncTaskHandle.h"
#include "runtime/AsyncThreadScope.h"
#include "core/services/AppRuntimeService.h"
#include <QScopeGuard>
#include <QCloseEvent>

#include "core/readmodel/TabularReadModel.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/AccessControlService.h"
#include "core/util/FormatUtils.h"
#include "core/services/settings/UiStateSettingsService.h"
#include "core/services/orders/OrderAttachmentService.h"
#include "ui/tabs/orders/CurrentOrdersCoordinator.h"
#include "ui/common/TabularSnapshotTableModel.h"

#include <QAbstractItemView>
#include <QByteArray>
#include <QComboBox>
#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonParseError>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableView>
#include <QTableWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QTextBrowser>
#include <QShowEvent>
#include <QTextDocument>
#include <QPdfWriter>
#include <QPainter>
#include <QFileDialog>

#include <algorithm>
#include <functional>

namespace {

using StatisticsReadService::PartTraceRow;

using StatisticsReadService::parseBusinessDate;

QString categoryText(const QString& category)
{
    if (category == QStringLiteral("session")) return QString::fromUtf8("Входи");
    if (category == QStringLiteral("work")) return QString::fromUtf8("Роботи");
    if (category == QStringLiteral("parts")) return QString::fromUtf8("Запчастини");
    if (category == QStringLiteral("inventory")) return QString::fromUtf8("Матеріали");
    if (category == QStringLiteral("payments")) return QString::fromUtf8("Оплати");
    if (category == QStringLiteral("access")) return QString::fromUtf8("Доступ");
    if (category == QStringLiteral("team")) return QString::fromUtf8("Команда");
    if (category == QStringLiteral("history")) return QString::fromUtf8("Історія");
    return QString::fromUtf8("Система");
}

QString roleText(const QString& role)
{
    if (role == QStringLiteral("owner")) return QString::fromUtf8("Власник");
    if (role == QStringLiteral("admin")) return QString::fromUtf8("Адміністратор");
    if (role == QStringLiteral("master")) return QString::fromUtf8("Майстер");
    if (role == QStringLiteral("manager")) return QString::fromUtf8("Менеджер");
    if (role == QStringLiteral("system")) return QString::fromUtf8("Система");
    return role.trimmed().isEmpty() ? QString::fromUtf8("Працівник") : role;
}

QString dateText(const QDate& date)
{
    return date.isValid() ? date.toString(QStringLiteral("dd.MM.yy")) : QString();
}

QString dateTimeText(const QDateTime& value)
{
    return value.isValid() ? value.toString(QStringLiteral("dd.MM.yy HH:mm")) : QString();
}

QString statusText(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("completed") || normalized == QStringLiteral("done")) return QString::fromUtf8("Виконано");
    if (normalized == QStringLiteral("installed")) return QString::fromUtf8("Встановлено");
    if (normalized == QStringLiteral("ordered")) return QString::fromUtf8("Замовлено");
    if (normalized == QStringLiteral("delivered")) return QString::fromUtf8("Надійшло");
    if (normalized == QStringLiteral("open")) return QString::fromUtf8("Відкрито");
    if (normalized == QStringLiteral("closed")) return QString::fromUtf8("Закрито");
    if (normalized == QStringLiteral("in_progress")) return QString::fromUtf8("В роботі");
    return value.trimmed();
}

QString entityText(const QString& entity)
{
    const QString value = entity.trimmed().toLower();
    if (value == QStringLiteral("work_order")) return QString::fromUtf8("наряд");
    if (value == QStringLiteral("work_order_service")) return QString::fromUtf8("робота в наряді");
    if (value == QStringLiteral("work_order_part") || value == QStringLiteral("part")) return QString::fromUtf8("запчастина або матеріал");
    if (value.contains(QStringLiteral("invoice"))) return QString::fromUtf8("накладна");
    if (value.contains(QStringLiteral("attachment")) || value.contains(QStringLiteral("file"))) return QString::fromUtf8("документ або вкладення");
    if (value.contains(QStringLiteral("inventory"))) return QString::fromUtf8("матеріал на складі");
    if (value.contains(QStringLiteral("staff"))) return QString::fromUtf8("обліковий запис працівника");
    if (value.contains(QStringLiteral("catalog"))) return QString::fromUtf8("каталог запчастин");
    return QString();
}

QString actionText(const QString& action, const QString& fallback = {})
{
    const QString normalized = action.trimmed().toLower();
    if (normalized == QStringLiteral("login")) return QString::fromUtf8("Вхід у застосунок");
    if (normalized == QStringLiteral("logout")) return QString::fromUtf8("Вихід із застосунку");
    if (normalized == QStringLiteral("user_switch")) return QString::fromUtf8("Зміна користувача");
    if (normalized == QStringLiteral("purchase_add")) return QString::fromUtf8("Придбано матеріал для складу");
    if (normalized == QStringLiteral("purchase_payment")) return QString::fromUtf8("Оплата / відтермінування придбання");
    if (normalized == QStringLiteral("purchase_remove")) return QString::fromUtf8("Видалено запис про придбання матеріалу");
    if (normalized == QStringLiteral("insert_service")) return QString::fromUtf8("Додано роботу до наряду");
    if (normalized == QStringLiteral("delete_services")) return QString::fromUtf8("Видалено роботи з наряду");
    if (normalized == QStringLiteral("delete_part") || normalized == QStringLiteral("remove_part")) return QString::fromUtf8("Видалено запчастину з наряду");
    if (normalized == QStringLiteral("set_service_price")) return QString::fromUtf8("Змінено ціну роботи");
    if (normalized == QStringLiteral("set_service_master")) return QString::fromUtf8("Змінено виконавця роботи");
    if (normalized == QStringLiteral("update_primary_master")) return QString::fromUtf8("Змінено основного майстра наряду");
    if (normalized == QStringLiteral("update_part_delivery_installation_dates")) return QString::fromUtf8("Оновлено дату надходження або встановлення запчастини");
    if (normalized == QStringLiteral("ensure_part_delivery_date_if_missing")) return QString::fromUtf8("Заповнено дату надходження запчастини");
    if (normalized == QStringLiteral("import_invoice_rows")) return QString::fromUtf8("Імпортовано рядки накладної в наряди");
    if (normalized == QStringLiteral("mobile_pairing_created")) return QString::fromUtf8("Створено QR-код для підключення телефону");
    if (normalized == QStringLiteral("mobile_pairing_claimed")) return QString::fromUtf8("Працівник підключив Android-телефон");
    if (normalized == QStringLiteral("mobile_device_online")) return QString::fromUtf8("Телефон працівника знову онлайн");
    if (normalized == QStringLiteral("mobile_service_completed")) return QString::fromUtf8("Роботу виконано з телефона");
    if (normalized == QStringLiteral("mobile_part_installed")) return QString::fromUtf8("Запчастину або матеріал встановлено з телефона");
    if (normalized == QStringLiteral("mobile_order_photo_added") || normalized == QStringLiteral("mobile_photo_uploaded")) return QString::fromUtf8("До наряду додано фото");
    if (normalized == QStringLiteral("mobile_recommendation_added")) return QString::fromUtf8("До наряду додано рекомендацію");
    if (normalized == QStringLiteral("mobile_customer_signature_added") || normalized == QStringLiteral("mobile_customer_signature_uploaded")) return QString::fromUtf8("До наряду додано підпис клієнта");
    if (normalized.contains(QStringLiteral("ensure_catalog")) || normalized.contains(QStringLiteral("schema"))) {
        return QString::fromUtf8("Система підготувала внутрішні дані каталогу");
    }
    return fallback.trimmed().isEmpty() ? QString::fromUtf8("Подія СТО") : fallback.trimmed();
}

QString eventTitleText(const QVariantMap& event)
{
    const QString action = event.value(QStringLiteral("action")).toString();
    const QString title = event.value(QStringLiteral("title")).toString();
    if (event.value(QStringLiteral("legacy")).toBool()) {
        return actionText(action, QString::fromUtf8("Історична системна зміна"));
    }
    return actionText(action, title);
}

QString fieldText(const QString& key)
{
    const QString normalized = key.trimmed().toLower();
    static const QHash<QString, QString> labels = {
        {QStringLiteral("name"), QString::fromUtf8("Назва")},
        {QStringLiteral("title"), QString::fromUtf8("Назва роботи")},
        {QStringLiteral("status"), QString::fromUtf8("Статус")},
        {QStringLiteral("comment"), QString::fromUtf8("Коментар")},
        {QStringLiteral("note"), QString::fromUtf8("Примітка")},
        {QStringLiteral("description"), QString::fromUtf8("Опис")},
        {QStringLiteral("qty"), QString::fromUtf8("Кількість")},
        {QStringLiteral("quantity"), QString::fromUtf8("Кількість")},
        {QStringLiteral("unit"), QString::fromUtf8("Одиниця виміру")},
        {QStringLiteral("unit_cost"), QString::fromUtf8("Ціна за одиницю")},
        {QStringLiteral("client_price"), QString::fromUtf8("Ціна клієнту")},
        {QStringLiteral("purchase_price"), QString::fromUtf8("Закупівельна ціна")},
        {QStringLiteral("total_cost"), QString::fromUtf8("Сума")},
        {QStringLiteral("supplier"), QString::fromUtf8("Постачальник")},
        {QStringLiteral("store"), QString::fromUtf8("Місце придбання")},
        {QStringLiteral("part_number"), QString::fromUtf8("Артикул")},
        {QStringLiteral("manufacturer"), QString::fromUtf8("Виробник")},
        {QStringLiteral("ordered_at"), QString::fromUtf8("Дата замовлення")},
        {QStringLiteral("purchased_on"), QString::fromUtf8("Дата придбання")},
        {QStringLiteral("delivery_date"), QString::fromUtf8("Дата надходження")},
        {QStringLiteral("installed"), QString::fromUtf8("Встановлено")},
        {QStringLiteral("installed_at"), QString::fromUtf8("Дата встановлення")},
        {QStringLiteral("completed_at"), QString::fromUtf8("Час виконання")},
        {QStringLiteral("master_name"), QString::fromUtf8("Майстер")},
        {QStringLiteral("completed_by"), QString::fromUtf8("Виконавець")},
        {QStringLiteral("installed_by"), QString::fromUtf8("Хто встановив")},
        {QStringLiteral("invoice_number"), QString::fromUtf8("Номер накладної")},
        {QStringLiteral("invoice_date"), QString::fromUtf8("Дата накладної")},
        {QStringLiteral("file_name"), QString::fromUtf8("Документ")},
        {QStringLiteral("mime_type"), QString::fromUtf8("Тип документа")},
        {QStringLiteral("size_bytes"), QString::fromUtf8("Розмір документа")},
        {QStringLiteral("recommendations"), QString::fromUtf8("Рекомендації")},
        {QStringLiteral("car"), QString::fromUtf8("Автомобіль")},
        {QStringLiteral("client"), QString::fromUtf8("Клієнт")},
        {QStringLiteral("number"), QString::fromUtf8("Номер наряду")},
    };
    return labels.value(normalized, key.trimmed());
}

QString displayJsonValue(const QJsonValue& value)
{
    if (value.isUndefined() || value.isNull()) return QString::fromUtf8("Не вказано");
    if (value.isBool()) return value.toBool() ? QString::fromUtf8("Так") : QString::fromUtf8("Ні");
    if (value.isDouble()) return QString::number(value.toDouble(), 'f', 2).remove(QRegularExpression(QStringLiteral("\\.00$")));
    if (value.isString()) {
        const QString text = value.toString().trimmed();
        const QDateTime dateTime = DateFormats::parseDateTime(text);
        return dateTime.isValid() ? dateTime.toLocalTime().toString(QStringLiteral("dd.MM.yy HH:mm")) : statusText(text);
    }
    if (value.isArray()) {
        QStringList items;
        for (const QJsonValue& item : value.toArray()) {
            if (item.isObject()) {
                const QJsonObject object = item.toObject();
                const QString name = object.value(QStringLiteral("name")).toString(object.value(QStringLiteral("title")).toString());
                items.append(name.trimmed().isEmpty() ? QString::fromUtf8("Додано запис") : name.trimmed());
            } else {
                items.append(displayJsonValue(item));
            }
        }
        return items.isEmpty() ? QString::fromUtf8("Немає") : items.join(QStringLiteral(", "));
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString(object.value(QStringLiteral("title")).toString());
        if (!name.trimmed().isEmpty()) return name.trimmed();
        const QString fileName = object.value(QStringLiteral("file_name")).toString();
        return fileName.trimmed().isEmpty() ? QString::fromUtf8("Додано запис") : fileName.trimmed();
    }
    return QString::fromUtf8("Не вказано");
}

QJsonObject objectFromValue(const QJsonValue& value)
{
    if (value.isObject()) return value.toObject();
    if (!value.isString() || value.toString().trimmed().isEmpty()) return {};
    const QJsonDocument document = QJsonDocument::fromJson(value.toString().toUtf8());
    return document.isObject() ? document.object() : QJsonObject{};
}

QJsonObject eventPayload(const QVariantMap& event)
{
    const QJsonDocument document = QJsonDocument::fromJson(event.value(QStringLiteral("payload")).toString().toUtf8());
    return document.isObject() ? document.object() : QJsonObject{};
}

QStringList businessChangedTables(const QJsonValue& value)
{
    QStringList labels;
    for (const QJsonValue& item : value.toArray()) {
        const QString table = item.toString().toLower();
        if (table.contains(QStringLiteral("work_order_parts"))) labels.append(QString::fromUtf8("запчастини наряду"));
        else if (table.contains(QStringLiteral("work_order_services"))) labels.append(QString::fromUtf8("роботи наряду"));
        else if (table.contains(QStringLiteral("work_orders"))) labels.append(QString::fromUtf8("дані наряду"));
        else if (table.contains(QStringLiteral("attachment")) || table.contains(QStringLiteral("file"))) labels.append(QString::fromUtf8("документи та вкладення"));
        else if (table.contains(QStringLiteral("inventory"))) labels.append(QString::fromUtf8("матеріали складу"));
        else if (table.contains(QStringLiteral("catalog"))) labels.append(QString::fromUtf8("каталог запчастин"));
    }
    labels.removeDuplicates();
    return labels;
}

struct EventChange
{
    QString field;
    QString before;
    QString after;
};

QVector<EventChange> eventChanges(const QVariantMap& event)
{
    const QJsonObject payload = eventPayload(event);
    if (event.value(QStringLiteral("action")).toString() == QStringLiteral("purchase_payment")) {
        auto money=[](double cents) { return FormatUtils::money(cents/100.0,2)+QString::fromUtf8(" грн"); };
        auto date=[](const QJsonValue& value) {
            const QDate day=DateFormats::parseDate(value.toString());
            return day.isValid() ? day.toString("dd.MM.yy") : QString::fromUtf8("Не вказано");
        };
        const double before=payload.value("paid_before_cents").toDouble();
        const double after=payload.value("paid_after_cents").toDouble();
        return {
            {QString::fromUtf8("Сума закупівлі"),QString(),money(payload.value("total_cents").toDouble())},
            {QString::fromUtf8("Платіж"),QString(),money(after-before)},
            {QString::fromUtf8("Уже сплачено"),money(before),money(after)},
            {QString::fromUtf8("Залишок боргу"),QString(),money(payload.value("remaining_cents").toDouble())},
            {QString::fromUtf8("Сплатити до"),date(payload.value("due_date_before")),date(payload.value("due_date_after"))}
        };
    }
    const QJsonObject before = objectFromValue(payload.value(QStringLiteral("before")));
    QJsonObject after = objectFromValue(payload.value(QStringLiteral("after")));
    if (after.isEmpty()) after = payload;

    const QSet<QString> ignored = {
        QStringLiteral("before"), QStringLiteral("after"), QStringLiteral("server_source"),
        QStringLiteral("source"), QStringLiteral("source_module"), QStringLiteral("operation_id"),
        QStringLiteral("operation"), QStringLiteral("entity_type"), QStringLiteral("entity_id"),
        QStringLiteral("system_maintenance"), QStringLiteral("committed"), QStringLiteral("order_uid"),
        QStringLiteral("service_uid"), QStringLiteral("part_uid"), QStringLiteral("file_uid"),
        QStringLiteral("employee_name"), QStringLiteral("employee_role"), QStringLiteral("device_name"),
        QStringLiteral("device_uid"), QStringLiteral("created_by"), QStringLiteral("server_path"),
        QStringLiteral("sha256"), QStringLiteral("photos"), QStringLiteral("customer_signature_file_uid"),
        QStringLiteral("customer_signature_at"), QStringLiteral("changed_tables")
    };

    QSet<QString> keys;
    for (auto it = before.constBegin(); it != before.constEnd(); ++it) keys.insert(it.key());
    for (auto it = after.constBegin(); it != after.constEnd(); ++it) keys.insert(it.key());

    QVector<EventChange> rows;
    for (const QString& key : keys) {
        if (ignored.contains(key.toLower())) continue;
        const QJsonValue oldValue = before.value(key);
        const QJsonValue newValue = after.value(key);
        if (!oldValue.isUndefined() && !newValue.isUndefined() && oldValue == newValue) continue;
        const QString beforeText = oldValue.isUndefined() ? QString::fromUtf8("Не було") : displayJsonValue(oldValue);
        const QString afterText = newValue.isUndefined() ? QString::fromUtf8("Видалено") : displayJsonValue(newValue);
        if (beforeText == QString::fromUtf8("Не вказано") && afterText == QString::fromUtf8("Не вказано")) continue;
        rows.append({fieldText(key), beforeText, afterText});
    }

    const QStringList tables = businessChangedTables(payload.value(QStringLiteral("changed_tables")));
    if (!tables.isEmpty()) rows.append({QString::fromUtf8("Змінені дані"), QString(), tables.join(QStringLiteral(", "))});
    return rows;
}

QString eventStory(const QVariantMap& event)
{
    const QString title = eventTitleText(event);
    if (event.value(QStringLiteral("legacy")).toBool()) {
        return QString::fromUtf8("Службовий історичний запис: %1. У старих записах не зберігалися автор і точні значення полів, тому показано лише безпечний підсумок.").arg(title);
    }
    const QString actor = event.value(QStringLiteral("actor")).toString().trimmed();
    if (event.value(QStringLiteral("actor_id")).toString() == QStringLiteral("system")) {
        return QString::fromUtf8("Система автоматично виконала дію: %1.").arg(title);
    }
    return QString::fromUtf8("Працівник %1 виконав дію: %2.").arg(actor.isEmpty() ? QString::fromUtf8("не вказаний") : actor, title);
}

class ActivityEventDetailsDialog final : public QDialog
{
public:
    ActivityEventDetailsDialog(const QVariantMap& event, QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle(QString::fromUtf8("Деталізація події"));
        setModal(true);
        resize(900, 650);
        setStyleSheet(QStringLiteral(
            "QDialog{background:#283847;} QLabel{color:#eaf1f7;}"
            "QTableWidget{background:#f9fcff;color:#16212b;border:1px solid #b8c7d5;border-radius:4px;gridline-color:#d5dee7;}"
            "QHeaderView::section{background:#dce7f0;color:#172431;border:none;border-right:1px solid #becbd7;padding:7px;font-weight:700;}"
            "QPushButton{background:#264762;color:#ffffff;border:1px solid #aac0d1;border-radius:4px;padding:7px 14px;font-weight:700;}"
            "QPushButton:hover{background:#315d7d;}"));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(20, 18, 20, 18);
        root->setSpacing(10);

        auto* title = new QLabel(eventTitleText(event), this);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("font-size:16pt;font-weight:800;color:#ffffff;"));
        root->addWidget(title);

        auto* story = new QLabel(eventStory(event), this);
        story->setWordWrap(true);
        story->setStyleSheet(QStringLiteral("color:#d8e7f2;font-size:10.5pt;"));
        root->addWidget(story);

        auto* form = new QFormLayout();
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(5);
        const auto add = [this, form](const QString& label, const QString& value) {
            auto* text = new QLabel(value.trimmed().isEmpty() ? QString::fromUtf8("Не вказано") : value, this);
            text->setTextInteractionFlags(Qt::TextSelectableByMouse);
            text->setWordWrap(true);
            text->setStyleSheet(QStringLiteral("color:#f6f9fc;"));
            form->addRow(label, text);
        };
        add(QString::fromUtf8("Дата і час:"), dateTimeText(event.value(QStringLiteral("at")).toDateTime()));
        add(QString::fromUtf8("Працівник:"), event.value(QStringLiteral("actor")).toString());
        add(QString::fromUtf8("Роль:"), roleText(event.value(QStringLiteral("role")).toString()));
        add(QString::fromUtf8("Розділ:"), categoryText(event.value(QStringLiteral("category")).toString()));
        const int visibleOrder = event.value(QStringLiteral("visibleOrder")).toInt();
        add(QString::fromUtf8("Наряд:"), visibleOrder > 0 ? QStringLiteral("№%1").arg(visibleOrder)
            : event.value(QStringLiteral("order")).toInt() > 0 ? QStringLiteral("Номер недоступний") : QStringLiteral("Не прив’язано"));
        const QString entity = entityText(event.value(QStringLiteral("entity_type")).toString());
        const QString subject = event.value(QStringLiteral("subject")).toString().trimmed();
        if (!subject.isEmpty() && !event.value(QStringLiteral("legacy")).toBool()) {
            add(entity.isEmpty() ? QString::fromUtf8("Що стосується:") : entity.left(1).toUpper() + entity.mid(1) + QLatin1Char(':'), subject);
        }
        root->addLayout(form);

        const QVector<EventChange> changes = eventChanges(event);
        auto* changesTitle = new QLabel(changes.isEmpty()
            ? QString::fromUtf8("Результат")
            : QString::fromUtf8("Що змінено під час події"), this);
        changesTitle->setStyleSheet(QStringLiteral("font-weight:800;color:#d8e7f2;"));
        root->addWidget(changesTitle);

        if (changes.isEmpty()) {
            auto* empty = new QLabel(event.value(QStringLiteral("legacy")).toBool()
                ? QString::fromUtf8("Точні поля для цього старого запису не збереглися.")
                : QString::fromUtf8("Подію зафіксовано. Для наступних операцій журнал зберігатиме конкретні значення змінених полів, документів і матеріалів."), this);
            empty->setWordWrap(true);
            empty->setStyleSheet(QStringLiteral("background:#34485b;border:1px solid #63798d;border-radius:4px;padding:9px;color:#eef5fa;"));
            root->addWidget(empty);
        } else {
            auto* changesTable = new QTableWidget(changes.size(), 3, this);
            changesTable->setHorizontalHeaderLabels({QString::fromUtf8("Що змінено"), QString::fromUtf8("Було"), QString::fromUtf8("Стало")});
            changesTable->verticalHeader()->setVisible(false);
            changesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
            changesTable->setSelectionMode(QAbstractItemView::NoSelection);
            changesTable->setWordWrap(true);
            for (int row = 0; row < changes.size(); ++row) {
                const EventChange& change = changes.at(row);
                changesTable->setItem(row, 0, new QTableWidgetItem(change.field));
                changesTable->setItem(row, 1, new QTableWidgetItem(change.before));
                changesTable->setItem(row, 2, new QTableWidgetItem(change.after));
            }
            changesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            changesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
            changesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
            changesTable->resizeRowsToContents();
            changesTable->setMinimumHeight(qMin(260, 60 + changes.size() * 42));
            root->addWidget(changesTable, 1);
        }

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);
    }
};
QTableView* createTable(QWidget* parent, TabularSnapshotTableModel*& model)
{
    auto* table = new QTableView(parent);
    model = new TabularSnapshotTableModel(table);
    table->setModel(model);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(30);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setHighlightSections(false);
    table->setStyleSheet(QStringLiteral(
        "QTableView{background:#fbfdff;alternate-background-color:#edf3f8;color:#13202c;"
        "border:1px solid #bdcbd8;gridline-color:#d5dee7;}"
        "QHeaderView::section{background:#dce7f0;color:#172431;border:none;border-right:1px solid #becbd7;"
        "border-bottom:1px solid #b9c7d3;padding:7px 8px;font-weight:700;}"));
    return table;
}

QWidget* tablePage(QWidget* parent, QTableView*& table, TabularSnapshotTableModel*& model)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    table = createTable(page, model);
    layout->addWidget(table);
    return page;
}

class ClickableMetricCard final : public QFrame
{
public:
    explicit ClickableMetricCard(const std::function<void()>& onClick, QWidget* parent)
        : QFrame(parent)
        , m_onClick(onClick)
    {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QFrame::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && m_onClick) {
            m_onClick();
        }
    }

private:
    std::function<void()> m_onClick;
};

QFrame* metricCard(QWidget* parent,
                   const QString& caption,
                   QLabel*& value,
                   const std::function<void()>& onClick)
{
    auto* card = new ClickableMetricCard(onClick, parent);
    card->setObjectName(QStringLiteral("controlMetric"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(2);
    auto* title = new QLabel(caption, card);
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    title->setStyleSheet(QStringLiteral("color:#c2d0dc;font-size:9.5pt;font-weight:700;"));
    value = new QLabel(QStringLiteral("0"), card);
    value->setAlignment(Qt::AlignCenter);
    value->setAttribute(Qt::WA_TransparentForMouseEvents);
    value->setStyleSheet(QStringLiteral("color:#f7fbff;font-size:20pt;font-weight:800;"));
    layout->addWidget(title);
    layout->addWidget(value);
    return card;
}

class MetricTableDialog final : public QDialog
{
public:
    MetricTableDialog(const QString& title,
                      const QString& subtitle,
                      const TabularReadModel& snapshot,
                      const std::function<void(int)>& onRowActivated,
                      QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle(title);
        setModal(true);
        resize(1120, 650);
        setStyleSheet(QStringLiteral(
            "QDialog{background:#283847;} QLabel{color:#eaf1f7;}"
            "QPushButton{background:#264762;color:#ffffff;border:1px solid #aac0d1;border-radius:4px;padding:7px 14px;font-weight:700;}"
            "QPushButton:hover{background:#315d7d;}"));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(18, 16, 18, 16);
        root->setSpacing(8);

        auto* heading = new QLabel(title, this);
        heading->setStyleSheet(QStringLiteral("font-size:16pt;font-weight:800;color:#ffffff;"));
        root->addWidget(heading);
        auto* detail = new QLabel(subtitle, this);
        detail->setWordWrap(true);
        detail->setStyleSheet(QStringLiteral("color:#d7e4ee;font-weight:600;"));
        root->addWidget(detail);

        TabularSnapshotTableModel* model = nullptr;
        auto* table = createTable(this, model);
        model->setSnapshot(snapshot);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        if (onRowActivated) {
            table->setToolTip(QString::fromUtf8("Подвійний клік відкриває повну картку події."));
            connect(table, &QTableView::doubleClicked, this, [onRowActivated](const QModelIndex& index) {
                onRowActivated(index.row());
            });
        }
        root->addWidget(table, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);
    }
};

QString orderDateTimeText(const QString& raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty()) return QString();
    QDateTime dateTime = DateFormats::parseDateTime(value);
    if (!dateTime.isValid()) dateTime = QDateTime::fromString(value, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!dateTime.isValid()) dateTime = QDateTime::fromString(value, QStringLiteral("yyyy-MM-dd HH:mm"));
    if (dateTime.isValid()) return dateTime.toString(QStringLiteral("dd.MM.yy HH:mm"));
    const QDate date = parseBusinessDate(value);
    return date.isValid() ? dateText(date) : value;
}

QString invoiceText(const PartTraceRow& row)
{
    QStringList parts;
    if (!row.invoiceNumber.trimmed().isEmpty()) parts.append(QString::fromUtf8("№%1").arg(row.invoiceNumber.trimmed()));
    if (!row.invoiceDate.trimmed().isEmpty()) parts.append(orderDateTimeText(row.invoiceDate));
    if (!row.invoiceFileName.trimmed().isEmpty()) parts.append(row.invoiceFileName.trimmed());
    return parts.join(QStringLiteral(" | "));
}
void configureStatsTableHeader(QTableView* table, const QString& settingsKey)
{
    if (!table || !table->horizontalHeader()) return;

    QHeaderView* header = table->horizontalHeader();
    table->setProperty("statsHeaderSettingsKey", settingsKey);
    table->setProperty("statsHeaderRestored", false);
    table->setProperty("statsHeaderPersistenceReady", false);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setSectionsMovable(true);
    header->setFirstSectionMovable(true);
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(52);

    const auto persist = [table]() {
        if (!table || !table->property("statsHeaderPersistenceReady").toBool()) return;
        QHeaderView* currentHeader = table->horizontalHeader();
        const QString key = table->property("statsHeaderSettingsKey").toString();
        if (!currentHeader || key.isEmpty()) return;
        UiStateSettingsService::saveString(
            key,
            QString::fromLatin1(currentHeader->saveState().toBase64()));
    };
    QObject::connect(header, &QHeaderView::sectionResized, table,
                     [persist](int, int, int) { persist(); });
    QObject::connect(header, &QHeaderView::sectionMoved, table,
                     [persist](int, int, int) { persist(); });
}

void restoreStatsTableHeader(QTableView* table)
{
    if (!table || table->property("statsHeaderRestored").toBool() || !table->model() || table->model()->columnCount() <= 0) return;

    QHeaderView* header = table->horizontalHeader();
    const QString key = table->property("statsHeaderSettingsKey").toString();
    if (!header || key.isEmpty()) return;

    const QByteArray state = QByteArray::fromBase64(UiStateSettingsService::loadString(key).toUtf8());
    {
        const QSignalBlocker blocker(header);
        if (!state.isEmpty() && header->restoreState(state)) {
            header->setSectionResizeMode(QHeaderView::Interactive);
            header->setSectionsMovable(true);
            header->setFirstSectionMovable(true);
            header->setStretchLastSection(false);
        } else {
            table->resizeColumnsToContents();
        }
    }
    table->setProperty("statsHeaderRestored", true);
    table->setProperty("statsHeaderPersistenceReady", true);
}


} // namespace

StatsPage::StatsPage(QWidget* parent, View view)
    : QWidget(parent), m_view(view)
{
    buildUi();
    QTimer::singleShot(0, this, &StatsPage::refresh);
}

StatsPage::~StatsPage()
{
    AsyncTaskHandle::cancel(m_refreshCancelToken);
}
void StatsPage::closeEvent(QCloseEvent* event)
{
    AsyncTaskHandle::cancel(m_refreshCancelToken);
    m_refreshQueued=false;m_dirty=true;m_closed=true;
    if(m_refreshTimer)m_refreshTimer->stop();
    QWidget::closeEvent(event);
}

void StatsPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_closed=false;
    if(m_dirty || m_view==View::WorkLog) {
        markDirty();
        scheduleRefresh();
    }
}

void StatsPage::buildUi()
{
    setObjectName(QStringLiteral("operationalStatsPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "#operationalStatsPage{background:transparent;}"
        "#operationalStatsPage QLabel{color:#f3f7fb;}"
        "#operationalStatsPage QDateEdit,#operationalStatsPage QComboBox,#operationalStatsPage QLineEdit{"
        "background:#ffffff;color:#17212b;border:1px solid #b7c6d4;border-radius:4px;padding:5px 7px;}"
        "#operationalStatsPage QPushButton{background:#243b51;color:#ffffff;border:1px solid #a7b9c9;border-radius:4px;"
        "padding:6px 11px;font-weight:700;}"
        "#operationalStatsPage QPushButton:hover{background:#31516d;}"
        "#operationalStatsPage QTabWidget::pane{border:none;background:transparent;}"
        "#operationalStatsPage QTabBar::tab{color:#f5f8fb;padding:9px 15px;}"
        "#operationalStatsPage QTabBar::tab:selected{color:#17212b;background:#eef4f9;border:1px solid #ffffff;font-weight:800;}"
        "#controlMetric{background:#273849;border:1px solid #63798d;border-radius:5px;}"
        "#controlMetric:hover{background:#30465a;border-color:#b7cedf;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* filters = new QWidget(this);
    auto* filterRows = new QVBoxLayout(filters);
    filterRows->setContentsMargins(0,0,0,0);filterRows->setSpacing(4);
    auto* filterLayout = new QHBoxLayout;
    filterRows->addLayout(filterLayout);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(4);
    auto makeTile = [filters, filterLayout](const QString& name, const QString& title, int kind) {
        auto* tile = new QFrame(filters);
        tile->setObjectName(name);
        tile->setStyleSheet(QStringLiteral("QFrame#%1{background:transparent;border:1px solid #101820;border-radius:5px;}").arg(name));
        auto* layout = new QVBoxLayout(tile);
        layout->setContentsMargins(12,8,12,8);
        layout->setSpacing(6);
        auto* heading = new QHBoxLayout;
        QPixmap pixmap(28,28); pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#59d9f1"),2));
        if(kind==0) {
            painter.drawRoundedRect(3,5,22,20,2,2); painter.drawLine(3,11,25,11);
            painter.drawLine(9,2,9,8); painter.drawLine(19,2,19,8);
            for(int y=15;y<=21;y+=6) for(int x=8;x<=20;x+=6) painter.drawPoint(x,y);
        } else if(kind==1) {
            painter.drawEllipse(3,3,22,22); painter.drawLine(14,6,14,14); painter.drawLine(14,14,20,18);
    } else if(kind==4) {
        painter.drawEllipse(3,3,16,16);
        painter.drawLine(17,17,25,25);
    } else if(kind==3) {
            painter.drawEllipse(9,2,10,10);
            painter.drawArc(4,14,20,18,0,180*16);
            painter.drawLine(4,23,24,23);
        } else {
            painter.drawEllipse(6,6,16,16); painter.drawEllipse(11,11,6,6);
            for(int angle=0;angle<360;angle+=45) {
                painter.save();painter.translate(14,14);painter.rotate(angle);
                painter.drawLine(0,-8,0,-12);painter.restore();
            }
        }
        painter.end();
        auto* icon=new QLabel(tile);icon->setPixmap(pixmap);heading->addWidget(icon);
        auto* label=new QLabel(title,tile);auto font=label->font();font.setBold(true);label->setFont(font);
        heading->addWidget(label,1);layout->addLayout(heading);
        auto* controls=new QHBoxLayout;controls->setSpacing(8);layout->addLayout(controls);
        filterLayout->addWidget(tile,1);
        return controls;
    };
    auto* periodControls=makeTile("statsPeriodTile",QString::fromUtf8("Період"),0);
    auto* searchControls=makeTile("statsSearchTile",QString::fromUtf8("Пошук"),4);
    auto* searchTile=filters->findChild<QWidget*>("statsSearchTile");
    auto* quickControls=makeTile("statsQuickTile",QString::fromUtf8("Швидкий період"),1);
    auto* masterControls=makeTile("statsMasterTile",QString::fromUtf8("Майстер"),3);
    auto* masterTile=filters->findChild<QWidget*>("statsMasterTile");
    auto* actionControls=makeTile("statsActionsTile",QString::fromUtf8("Дії"),2);

    m_from = new QDateEdit(QDate::currentDate().addDays(1-QDate::currentDate().dayOfWeek()), filters);
    m_to = new QDateEdit(QDate::currentDate(), filters);
    for (QDateEdit* editor : {m_from, m_to}) {
        editor->setDisplayFormat(QStringLiteral("dd.MM.yy"));
        editor->setCalendarPopup(true);
        editor->setKeyboardTracking(false);
        editor->setMinimumHeight(32);
        editor->setMinimumWidth(105);
    }
    m_actor = new QComboBox(filters);
    m_actor->setMinimumWidth(145);
    m_category = new QComboBox(filters);
    m_category->setMinimumWidth(130);
    m_category->addItem(QString::fromUtf8("Усі події"), QString());
    m_category->addItem(QString::fromUtf8("Входи"), QStringLiteral("session"));
    m_category->addItem(QString::fromUtf8("Роботи"), QStringLiteral("work"));
    m_category->addItem(QString::fromUtf8("Запчастини"), QStringLiteral("parts"));
    m_category->addItem(QString::fromUtf8("Матеріали"), QStringLiteral("inventory"));
    m_category->addItem(QString::fromUtf8("Оплати"), QStringLiteral("payments"));
    m_category->addItem(QString::fromUtf8("Доступ"), QStringLiteral("access"));
    m_category->addItem(QString::fromUtf8("Історія"), QStringLiteral("history"));
    m_search = new QLineEdit(filters);
    m_search->setMinimumWidth(175);
    m_search->setPlaceholderText(QString::fromUtf8("Наряд, деталь, дія"));
    m_search->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));

    auto* today = new QPushButton(QString::fromUtf8("Сьогодні"), filters);
    auto* week = new QPushButton(QString::fromUtf8("7 днів"), filters);
    auto* month = new QPushButton(QString::fromUtf8("Місяць"), filters);
    auto* reload = new QPushButton(QString::fromUtf8("Оновити"), filters);
    periodControls->addWidget(m_from,1);
    periodControls->addWidget(m_to,1);
    auto* extraFilters=new QHBoxLayout;
    extraFilters->addWidget(m_actor);
    extraFilters->addWidget(m_category);
    filterRows->addLayout(extraFilters);
    searchControls->addWidget(m_search,1);
    for(auto* button:{today,week,month})quickControls->addWidget(button,1);
    actionControls->addWidget(reload,1);
    for(auto* editor:{m_from,m_to}) {
        editor->setMinimumWidth(124);
        editor->ensurePolished();
        editor->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
        editor->setMinimumWidth(editor->fontMetrics().horizontalAdvance(QStringLiteral("00.00.00"))+40);
    }
    root->addWidget(filters);

    m_summary = new QLabel(this);
    m_summary->setContentsMargins(26, 0, 26, 0);
    m_summary->setStyleSheet(QStringLiteral("color:#d5e3ef;font-weight:600;"));
    root->addWidget(m_summary);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    if (m_tabs->tabBar()) m_tabs->tabBar()->setDrawBase(false);

    auto makeReport = [this](const QString& name) {
        auto* report = new QTextBrowser(m_tabs);
        report->setObjectName(name);
        report->setOpenLinks(false);
        report->setStyleSheet("QTextBrowser{background:#f5f7f9;color:#21343e;border:1px solid #bdc8d0;border-radius:5px;padding:8px;}");
        connect(report, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
            if (url.scheme() == "master") {
                const auto id = QUrl::fromPercentEncoding(url.path().toUtf8());
                const int index = m_master->findData(id);
                if (index >= 0) { m_master->setCurrentIndex(index); m_tabs->setCurrentIndex(1); }
            } else if (url.scheme() == "order") {
                const int number = url.path().toInt();
                if (number > 0 && AccessControlService::canAccessOrder( number)) {
                    QString uid;
                    for (const auto& value : property("performanceOrders").toList()) {
                        const auto row = value.toMap();
                        if (row.value("number").toInt() == number) { uid = row.value("uid").toString(); break; }
                    }
                    CurrentOrdersCoordinator::openExistingOrderEditor(this, number, uid);
                    markDirty(); refresh();
                }
            } else if (url.scheme() == "warehouse") {
                QMetaObject::invokeMethod(window(),"gotoPage",Qt::QueuedConnection,Q_ARG(int,13));
            }
        });
        return report;
    };
    m_overview = makeReport("statsOverview");
    m_tabs->addTab(m_overview, QString::fromUtf8("Огляд СТО"));
    auto* masterPage = new QWidget(m_tabs);
    auto* masterLayout = new QVBoxLayout(masterPage);
    masterLayout->setContentsMargins(0, 0, 0, 0);
    m_master = new QComboBox(masterTile);
    m_master->setObjectName("statsMaster");
    m_master->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
    masterControls->addWidget(m_master,1);
    m_masterReport = makeReport("statsMasterReport");
    masterLayout->addWidget(m_masterReport, 1);
    m_tabs->addTab(masterPage, QString::fromUtf8("Майстри"));
    connect(m_master, qOverload<int>(&QComboBox::currentIndexChanged), this, &StatsPage::renderPerformance);
    auto* pdf = new QPushButton(QString::fromUtf8("Експорт PDF"), filters);
    actionControls->addWidget(pdf,1);
    for(auto* button:{today,week,month,reload,pdf})
        button->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
    connect(pdf, &QPushButton::clicked, this, [this]() {
        auto* report = m_tabs->currentIndex() == 1 ? m_masterReport : m_overview;
        const auto path = QFileDialog::getSaveFileName(this, QString::fromUtf8("Статистика СТО"), "statistics.pdf", "PDF (*.pdf)");
        if (path.isEmpty()) return;
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageOrientation(QPageLayout::Landscape);
        report->document()->print(&writer);
    });
    connect(m_tabs, &QTabWidget::currentChanged, pdf, [pdf](int index) { pdf->setEnabled(index < 2); });
    auto updateFilters=[this,masterTile,searchTile](int index) {
        masterTile->setVisible(m_view==View::Performance && index==1);
        m_actor->setVisible(m_view==View::WorkLog);
        m_category->setVisible(m_view==View::WorkLog);
        m_search->setVisible(index>=2);
        searchTile->setVisible(index>=2);
        m_summary->setVisible(m_view==View::WorkLog);
        if(m_view==View::Performance && index>=2 && !m_dirty) applyFilters();
    };
    connect(m_tabs,&QTabWidget::currentChanged,this,updateFilters);
    updateFilters(0);

    auto* dashboard = new QWidget(m_tabs);
    auto* dashLayout = new QVBoxLayout(dashboard);
    dashLayout->setContentsMargins(0, 8, 0, 0);
    dashLayout->setSpacing(8);
    auto* metrics = new QWidget(dashboard);
    auto* metricLayout = new QGridLayout(metrics);
    metricLayout->setContentsMargins(0, 0, 0, 0);
    metricLayout->setHorizontalSpacing(8);
    metricLayout->addWidget(metricCard(metrics, QString::fromUtf8("Подій за період"), m_eventsMetric,
                                       [this]() { showEventsMetricDetails(); }), 0, 0);
    metricLayout->addWidget(metricCard(metrics, QString::fromUtf8("Активних працівників"), m_staffMetric,
                                       [this]() { showTeamMetricDetails(); }), 0, 1);
    metricLayout->addWidget(metricCard(metrics, QString::fromUtf8("Запчастин в очікуванні"), m_waitingMetric,
                                       [this]() { showWaitingPartsMetricDetails(); }), 0, 2);
    metricLayout->addWidget(metricCard(metrics, QString::fromUtf8("На складі понад 7 днів"), m_agingMetric,
                                       [this]() { showAgingPartsMetricDetails(); }), 0, 3);
    for (int column = 0; column < 4; ++column) metricLayout->setColumnStretch(column, 1);
    dashLayout->addWidget(metrics);

    m_alertText = new QLabel(dashboard);
    m_alertText->setWordWrap(true);
    m_alertText->setStyleSheet(QStringLiteral("background:#34485b;border:1px solid #879aaa;border-radius:4px;padding:8px 10px;color:#eef5fa;font-weight:600;"));
    dashLayout->addWidget(m_alertText);

    auto* activityTitle = new QLabel(QString::fromUtf8("Останні зафіксовані дії"), dashboard);
    activityTitle->setStyleSheet(QStringLiteral("font-size:12pt;font-weight:800;"));
    dashLayout->addWidget(activityTitle);
    m_dashboardTable = createTable(dashboard, m_dashboardModel);
    dashLayout->addWidget(m_dashboardTable, 1);

    m_tabs->addTab(dashboard, QString::fromUtf8("Центр контролю"));
    m_tabs->addTab(tablePage(m_tabs, m_activityTable, m_activityModel), QString::fromUtf8("Операційний журнал"));
    m_tabs->addTab(tablePage(m_tabs, m_teamTable, m_teamModel), QString::fromUtf8("Команда"));
    m_tabs->addTab(tablePage(m_tabs, m_partsTable, m_partsModel), QString::fromUtf8("Рух запчастин"));
    for(int index=0;index<m_tabs->count();++index) {
        const bool journalTab=index>=2 && index<=4;
        m_tabs->setTabVisible(index,(m_view==View::WorkLog)==journalTab);
    }
    m_tabs->setCurrentIndex(m_view==View::WorkLog?3:0);
    pdf->setVisible(m_view==View::Performance);
    root->addWidget(m_tabs, 1);

    configureStatsTableHeader(m_dashboardTable, QStringLiteral("ui.statistics.dashboard.header_state.v1"));
    configureStatsTableHeader(m_activityTable, QStringLiteral("ui.statistics.activity.header_state.v1"));
    configureStatsTableHeader(m_teamTable, QStringLiteral("ui.statistics.team.header_state.v1"));
    configureStatsTableHeader(m_partsTable, QStringLiteral("ui.statistics.parts.header_state.v1"));
    m_dashboardTable->setToolTip(QString::fromUtf8("Подвійний клік відкриває деталі події."));
    m_activityTable->setToolTip(QString::fromUtf8("Подвійний клік відкриває деталі події."));
    m_teamTable->setToolTip(QString::fromUtf8("Подвійний клік відкриває історію роботи працівника за вибраний період."));
    m_partsTable->setToolTip(QString::fromUtf8("Подвійний клік відкриває наряд із цією запчастиною."));

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    connect(m_refreshTimer, &QTimer::timeout, this, &StatsPage::refresh);
    connect(m_from, &QDateEdit::dateChanged, this, [this](const QDate&) { markDirty(); scheduleRefresh(100); });
    connect(m_to, &QDateEdit::dateChanged, this, [this](const QDate&) { markDirty(); scheduleRefresh(100); });
    connect(m_actor, qOverload<int>(&QComboBox::currentIndexChanged), this, &StatsPage::applyFilters);
    connect(m_category, qOverload<int>(&QComboBox::currentIndexChanged), this, &StatsPage::applyFilters);
    connect(m_search, &QLineEdit::textChanged, this, &StatsPage::applyFilters);
    connect(m_activityTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        showEventDetails(index.row());
    });
    connect(m_dashboardTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        showEventDetails(index.row());
    });
    connect(m_teamTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        showTeamMemberDetails(index.row());
    });
    connect(m_partsTable, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        openPartOrder(index.row());
    });
    connect(today, &QPushButton::clicked, this, &StatsPage::setToday);
    connect(week, &QPushButton::clicked, this, &StatsPage::setWeek);
    connect(month, &QPushButton::clicked, this, &StatsPage::setMonth);
    connect(reload, &QPushButton::clicked, this, [this]() { m_dirty = true; refresh(); });
}

void StatsPage::refresh()
{
    if (!m_dirty) return;
    reloadData();
}

void StatsPage::renderPerformance()
{
    if(!m_overview || !m_masterReport) return;
    if(property("performanceScope").isValid()
        && property("performanceScope").toString()!=AccessControlService::currentScopeKey()) {
        reloadData();
        return;
    }
    const auto staff=property("performanceStaff").toList();
    const auto orders=property("performanceOrders").toList();
    const auto stock=property("performanceStock").toList();
    auto esc=[](const QString& text){return text.toHtmlEscaped();};
    auto cell=[&](const QString& text){return "<td>"+esc(text)+"</td>";};
    auto bar=[&](const QString& title,int value,int maximum,const QString& href=QString()) {
        const int fill=qBound(1,maximum>0?qRound(100.0*value/maximum):1,100);
        QString label=esc(title);
        if(!href.isEmpty()) label="<a href='"+href+"'>"+label+"</a>";
        QString result="<tr><td width='28%'>"+label+"</td><td width='62%'><table width='100%' cellspacing='0' cellpadding='0'><tr>";
        if(value>0) result+=QString("<td width='%1%' bgcolor='#259a63'>&nbsp;</td>").arg(fill);
        if(value==0 || fill<100) result+=QString("<td width='%1%' bgcolor='#e4ebef'>&nbsp;</td>").arg(value==0?100:100-fill);
        return result+"</tr></table></td><td align='right'><b>"+QString::number(value)+"</b></td></tr>";
    };
    auto render=[&](bool individual) {
        const auto selected=m_master->currentData().toString();
        const QString start="<html><head><style>body{font-family:'Times New Roman';font-size:12pt;color:#202c35;}"
            "h2{font-size:20pt;margin-bottom:8px;}h3{font-size:14pt;margin-top:14px;}th{background:#dfe6eb;}"
            "td,th{padding:7px;}a{color:#176d4c;}p{color:#54616c;}</style></head><body>";
        QString html=start+"<h2>"+(individual?esc(m_master->currentText()):QString::fromUtf8("Огляд СТО"))+"</h2>";
        html+="<p>"+m_from->date().toString("dd.MM.yyyy")+" - "+m_to->date().toString("dd.MM.yyyy")+"</p>";
        if(!property("performanceError").toString().isEmpty())
            html+="<p style='color:#b52b33;'>"+esc(property("performanceError").toString())+"</p>";
        QMap<QString,int> completed, open, stored;
        int count=0,inWork=0;
        for(const auto& value:orders) {
            const auto row=value.toMap();const auto id=row.value("master").toString();
            if(row.value("completed").toBool()) ++completed[id]; else ++open[id];
            if(!individual || id==selected) {
                if(row.value("completed").toBool()) ++count; else ++inWork;
            }
        }
        for(const auto& value:stock) {const auto row=value.toMap();if(row.value("balance").toDouble()>0) ++stored[row.value("master").toString()];}
        html+="<table width='100%' cellspacing='6' cellpadding='9'><tr>";
        const QStringList labels={QString::fromUtf8("Виконано нарядів"),QString::fromUtf8("Майстрів у табелі"),
            individual?QString::fromUtf8("Робочий час, год"):QString::fromUtf8("Завантаження постів"),QString::fromUtf8("Простій, год")};
        const QStringList values={QString::number(count),QString::number(property("currentMasterCount").toInt()),
            QString::fromUtf8("Немає обліку"),QString::fromUtf8("Немає обліку")};
        for(int i=0;i<4;++i) html+="<td width='25%' bgcolor='#ffffff' style='border:1px solid #bdc8d0;'>"+labels[i]
            +"<p align='center'><span style='font-size:24pt;font-weight:700;color:"+QString(i<2?"#147744":"#69747e")+";'>"+values[i]+"</span></p></td>";
        html+="</tr></table>";
        html+=QString::fromUtf8("<p>Персонал: чинні майстри з табеля, без адміністраторів. Наряди: за основним майстром; завершені за датою закриття.</p>");
        html+="<table width='100%' cellspacing='6'><tr><td width='60%' bgcolor='#ffffff' valign='top'><h3>"
            +(individual?QString::fromUtf8("Завершені наряди за днями"):QString::fromUtf8("Виконані наряди за майстрами"))+"</h3><table width='100%'>";
        if(individual) {
            QMap<QDate,int> daily;
            for(const auto& value:orders) {const auto row=value.toMap();if(row.value("master").toString()==selected && row.value("completed").toBool()) ++daily[row.value("closed").toDate()];}
            int max=1;for(auto n:daily) max=qMax(max,n);
            if(daily.isEmpty()) html+="<tr>"+cell(QString::fromUtf8("За цей період немає завершених нарядів."))+"</tr>";
            for(auto it=daily.cbegin();it!=daily.cend();++it) html+=bar(it.key().toString("dd.MM.yyyy"),it.value(),max);
        } else {
            int max=1;for(auto n:completed) max=qMax(max,n);
            for(const auto& value:staff) {const auto row=value.toMap();const auto id=row.value("id").toString();
                html+=bar(row.value("name").toString(),completed.value(id),max,"master:"+QString::fromLatin1(QUrl::toPercentEncoding(id)));}
        }
        html+="</table></td><td width='40%' bgcolor='#ffffff' valign='top'><h3>"+QString::fromUtf8("Розподіл часу")+"</h3><p>"
            +QString::fromUtf8("Немає зафіксованих інтервалів роботи та пауз. Години простою й відсоток завантаження не розраховуються з тривалості відкритого наряду.")
            +"</p><h3>"+QString::fromUtf8("У роботі: %1 нарядів").arg(inWork)+"</h3></td></tr></table>";
        if(!individual) {
            html+=QString::fromUtf8("<h3>Показники майстрів</h3><table width='100%' cellspacing='1' bgcolor='#c9d2d9'><tr><th>Майстер</th><th>Завершено</th><th>У роботі</th><th>Позицій на складі зараз</th></tr>");
            for(const auto& value:staff) {const auto row=value.toMap();const auto id=row.value("id").toString();
                html+="<tr bgcolor='#ffffff'><td><a href='master:"+QString::fromLatin1(QUrl::toPercentEncoding(id))+"'>"+esc(row.value("name").toString())+"</a></td>"
                    +cell(QString::number(completed.value(id)))+cell(QString::number(open.value(id)))+cell(QString::number(stored.value(id)))+"</tr>";}
        } else {
            html+=QString::fromUtf8("<h3>Наряди майстра</h3><table width='100%' cellspacing='1' bgcolor='#c9d2d9'><tr><th>Наряд</th><th>Авто</th><th>Відкрито</th><th>Стан</th></tr>");
            for(const auto& value:orders) {const auto row=value.toMap();if(row.value("master").toString()!=selected) continue;
                html+="<tr bgcolor='#ffffff'><td><a href='order:"+row.value("number").toString()+"'>"+row.value("display").toString()+"</a></td>"
                    +cell(row.value("car").toString())+cell(row.value("opened").toDate().toString("dd.MM.yy"))
                    +cell(row.value("completed").toBool()?QString::fromUtf8("Закрито ")+row.value("closed").toDate().toString("dd.MM.yy"):QString::fromUtf8("У роботі"))+"</tr>";}
        }
        html+="</table><h3>"+QString::fromUtf8("Запчастини під авто: поточний склад")+"</h3><p>"
            +QString::fromUtf8("Дні від останнього фактичного приймання; не є простроченням оплати. Залишки на сьогодні, незалежно від періоду нарядів.")+"</p>"
            +QString::fromUtf8("<table width='100%' cellspacing='1' bgcolor='#c9d2d9'><tr><th>Запчастина / OEM</th><th>Авто</th><th>Очікується, шт.</th><th>Залишок, шт.</th><th>Прийнято</th><th>Днів</th></tr>");
        int stockCount=0;
        for(const auto& value:stock) {const auto row=value.toMap();if(individual && row.value("master").toString()!=selected) continue;
            ++stockCount;const auto days=row.value("days").toInt();
            html+="<tr bgcolor='"+QString(days>7?"#ffe4e4":days>=3?"#fff3d5":"#ffffff")+"'>"
                +cell(row.value("name").toString())+cell(row.value("car").toString())+cell(row.value("waiting").toString())
                +cell(row.value("balance").toString())+cell(row.value("received").toDate().toString("dd.MM.yy"))
                +cell(days<0?QString::fromUtf8("Не прийнято"):QString::number(days))+"</tr>";}
        html+="</table>";
        if(!stockCount) html+=QString::fromUtf8("<p>Немає запчастин в очікуванні або на складі.</p>");
        html+=QString::fromUtf8("<p><a href='warehouse:open'>Перейти до складу СТО</a></p>");
        html.replace("width='100%'","width='99%'");
        return html+"</body></html>";
    };
    m_overview->setHtml(render(false));
    m_masterReport->setHtml(render(true));
}

void StatsPage::markDirty()
{
    m_dirty = true;
}

void StatsPage::scheduleRefresh(int delayMs)
{
    if (m_refreshTimer) m_refreshTimer->start(qMax(0, delayMs));
}

void StatsPage::setToday()
{
    m_from->setDate(QDate::currentDate());
    m_to->setDate(QDate::currentDate());
}

void StatsPage::setWeek()
{
    m_from->setDate(QDate::currentDate().addDays(-6));
    m_to->setDate(QDate::currentDate());
}

void StatsPage::setMonth()
{
    m_from->setDate(QDate::currentDate().addDays(-29));
    m_to->setDate(QDate::currentDate());
}

void StatsPage::reloadData()
{
    if(m_closed)return;
    if(m_refreshWorker) {
        AsyncTaskHandle::cancel(m_refreshCancelToken);
        m_refreshQueued=true;
        return;
    }
    const bool performance=m_view==View::Performance;
    const auto request=StatisticsReadService::capture(m_from->date(),m_to->date(),performance);
    if(!request.allowed) {
        for(const auto* key:{"events","filteredEvents","teamRows","partRows"})setProperty(key,QVariantList());
        m_loadedParts.clear();
        for(auto* model:{m_dashboardModel,m_activityModel,m_teamModel,m_partsModel})model->setSnapshot({});
        updateSummary(0,0,0,0);m_summary->setText(QString::fromUtf8("Немає дозволу на перегляд журналу робіт."));
        m_dirty=false;return;
    }
    const auto scopeKey=request.scopeKey;
    const auto from=request.from,to=request.to;
    const auto cancel=AsyncTaskHandle::renewCancellationToken(m_refreshCancelToken);
    auto result=std::make_shared<StatisticsReadService::Snapshot>();
    setProperty("statisticsLoading",true);
    auto* worker=AsyncThreadScope::startWorker(this,"statistics_refresh",cancel,[result,cancel,request] {
        *result=StatisticsReadService::run(request,cancel);
    });
    m_refreshWorker=worker;
    connect(worker,&QThread::finished,this,[this,result,cancel,scopeKey,performance,from,to] {
        m_refreshWorker=nullptr;setProperty("statisticsLoading",false);
        if(m_refreshQueued) {m_refreshQueued=false;reloadData();return;}
        if(cancel->load() || scopeKey!=AccessControlService::currentScopeKey() || from!=m_from->date() || to!=m_to->date())return;
        setProperty("statisticsError",result->error);
        if(!result->error.isEmpty()) {
            m_loadedParts.clear();
            for(const auto* key:{"events","filteredEvents","teamRows","partRows","performanceStaff","performanceOrders","performanceStock"})setProperty(key,QVariantList());
            for(auto* model:{m_dashboardModel,m_activityModel,m_teamModel,m_partsModel})model->setSnapshot({});
            updateSummary(0,0,0,0);
            m_summary->setText(result->error);
            if(performance) {
                m_overview->setPlainText(result->error);
                m_masterReport->setPlainText(result->error);
            }
            return;
        }
        m_dirty=false;m_loadedParts=result->parts;
        if(performance) {
            const auto& staff=result->staff;const auto& orders=result->orders;const auto& stock=result->stock;
            const QStringList errors;
    setProperty("performanceStaff",staff);
    setProperty("performanceOrders",orders);
    setProperty("performanceStock",stock);
    setProperty("currentMasterCount",staff.size());
    setProperty("performanceError",errors.join(' '));
    setProperty("performanceScope",AccessControlService::currentScopeKey());
    {
        QSignalBlocker block(m_master);
        const auto previous=m_master->currentData().toString();
        m_master->clear();
        for(const auto& value:staff) {
            const auto row=value.toMap();
            m_master->addItem(row.value("name").toString(),row.value("id"));
        }
        const int index=m_master->findData(previous);
        m_master->setCurrentIndex(index>=0?index:(staff.isEmpty()?-1:0));
    }
    renderPerformance();

        } else {
            setProperty("events",result->events);populateActors();applyFilters();
        }
    });
    worker->start();
}

void StatsPage::populateActors()
{
    const QVariantList events = property("events").toList();
    const QString previous = m_actor->currentData().toString();
    QSignalBlocker blocker(m_actor);
    m_actor->clear();
    m_actor->addItem(QString::fromUtf8("Усі працівники"), QString());
    QSet<QString> seen;
    for (const QVariant& value : events) {
        const QVariantMap map = value.toMap();
        const QString id = map.value(QStringLiteral("actor_id")).toString();
        const QString name = map.value(QStringLiteral("actor")).toString();
        if (id.isEmpty() || id == QStringLiteral("system") || id == QStringLiteral("legacy") || seen.contains(id)) continue;
        seen.insert(id);
        m_actor->addItem(name, id);
    }
    const int index = m_actor->findData(previous);
    m_actor->setCurrentIndex(index >= 0 ? index : 0);
}

void StatsPage::applyFilters()
{
    // Performance cards do not consume the hidden operational journal or parts table.
    if(m_view==View::Performance && m_tabs->currentIndex()<2) return;
    const QVariantList all = property("events").toList();
    const QString actorId = m_actor->currentData().toString();
    const QString category = m_category->currentData().toString();
    const QString search = m_search->text().trimmed();

    QVariantList events;
    for (const QVariant& value : all) {
        const QVariantMap map = value.toMap();
        if (!actorId.isEmpty() && map.value(QStringLiteral("actor_id")).toString() != actorId) continue;
        if (!category.isEmpty() && map.value(QStringLiteral("category")).toString() != category) continue;
        const QString probe = map.value(QStringLiteral("title")).toString() + QLatin1Char(' ')
            + map.value(QStringLiteral("subject")).toString() + QLatin1Char(' ')
            + map.value(QStringLiteral("visibleOrder")).toString();
        if (!search.isEmpty() && !probe.contains(search, Qt::CaseInsensitive)) continue;
        events.append(map);
    }
    setProperty("filteredEvents", events);
    populateTables();
}

void StatsPage::populateTables()
{
    const QVariantList events = property("filteredEvents").toList();
    TabularReadModel activity;
    activity.headers = {QString::fromUtf8("Дата / час"), QString::fromUtf8("Працівник"), QString::fromUtf8("Роль"),
                        QString::fromUtf8("Розділ"), QString::fromUtf8("Подія"), QString::fromUtf8("Наряд"), QString::fromUtf8("Об'єкт")};
    TabularReadModel dashboard = activity;
    dashboard.rows.clear();

    struct TeamAggregate {
        QString accountId;
        QString name;
        QString role;
        int logins = 0;
        int actions = 0;
        int works = 0;
        int parts = 0;
        int inventory = 0;
        QDateTime lastAction;
    };
    QHash<QString, TeamAggregate> team;
    int activeStaff = 0;
    QSet<QString> uniqueStaff;
    for (const QVariant& value : events) {
        const QVariantMap map = value.toMap();
        const QString actorId = map.value(QStringLiteral("actor_id")).toString();
        const QString actor = map.value(QStringLiteral("actor")).toString();
        const QString category = map.value(QStringLiteral("category")).toString();
        const QDateTime when = map.value(QStringLiteral("at")).toDateTime();
        activity.rows.append({dateTimeText(when), actor, roleText(map.value(QStringLiteral("role")).toString()), categoryText(category),
                              eventTitleText(map),
                              map.value(QStringLiteral("visibleOrder")).toInt() > 0 ? map.value(QStringLiteral("visibleOrder")).toString() : QString(),
                              map.value(QStringLiteral("subject")).toString()});
        if (dashboard.rows.size() < 18) dashboard.rows.append(activity.rows.constLast());

        if (actorId.isEmpty() || actorId == QStringLiteral("system") || actorId == QStringLiteral("legacy")) continue;
        uniqueStaff.insert(actorId);
        TeamAggregate& row = team[actorId];
        row.accountId = actorId;
        row.name = actor;
        row.role = map.value(QStringLiteral("role")).toString();
        row.actions += 1;
        if (category == QStringLiteral("session")) ++row.logins;
        if (category == QStringLiteral("work")) ++row.works;
        if (category == QStringLiteral("parts")) ++row.parts;
        if (category == QStringLiteral("inventory")) ++row.inventory;
        if (!row.lastAction.isValid() || when > row.lastAction) row.lastAction = when;
    }
    activeStaff = uniqueStaff.size();
    m_activityModel->setSnapshot(activity);
    m_dashboardModel->setSnapshot(dashboard);

    TabularReadModel teamModel;
    teamModel.headers = {QString::fromUtf8("Працівник"), QString::fromUtf8("Роль"), QString::fromUtf8("Входи"),
                         QString::fromUtf8("Дії"), QString::fromUtf8("Роботи"), QString::fromUtf8("Запчастини"),
                         QString::fromUtf8("Матеріали"), QString::fromUtf8("Остання активність")};
    QList<TeamAggregate> aggregates = team.values();
    std::sort(aggregates.begin(), aggregates.end(), [](const TeamAggregate& left, const TeamAggregate& right) {
        return left.lastAction > right.lastAction;
    });
    QVariantList teamRows;
    for (const TeamAggregate& row : aggregates) {
        teamModel.rows.append({row.name, roleText(row.role), QString::number(row.logins), QString::number(row.actions),
                               QString::number(row.works), QString::number(row.parts), QString::number(row.inventory),
                               dateTimeText(row.lastAction)});
        QVariantMap detail;
        detail.insert(QStringLiteral("accountId"), row.accountId);
        detail.insert(QStringLiteral("name"), row.name);
        detail.insert(QStringLiteral("role"), row.role);
        detail.insert(QStringLiteral("logins"), row.logins);
        detail.insert(QStringLiteral("actions"), row.actions);
        detail.insert(QStringLiteral("works"), row.works);
        detail.insert(QStringLiteral("parts"), row.parts);
        detail.insert(QStringLiteral("inventory"), row.inventory);
        teamRows.append(detail);
    }
    setProperty("teamRows", teamRows);
    m_teamModel->setSnapshot(teamModel);

    QVector<PartTraceRow> parts;
    for(const auto& row:m_loadedParts) {
        const QString probe=QString::number(row.visibleOrderNumber)+' '+row.name+' '+row.partNumber+' '+row.car+' '+row.client;
        if(m_search->text().trimmed().isEmpty() || probe.contains(m_search->text().trimmed(),Qt::CaseInsensitive))parts.append(row);
    }
    TabularReadModel partsModel;
    partsModel.headers = {QString::fromUtf8("Наряд"), QString::fromUtf8("Запчастина / артикул"), QString::fromUtf8("Авто / клієнт"),
                          QString::fromUtf8("Замовлено"), QString::fromUtf8("Магазин"), QString::fromUtf8("Накладна"),
                          QString::fromUtf8("Надійшло"), QString::fromUtf8("Встановлено"), QString::fromUtf8("Днів на складі"),
                          QString::fromUtf8("Статус"), QString::fromUtf8("Відповідальний")};
    QVariantList partRows;
    int waitingParts = 0;
    int agingParts = 0;
    for (const PartTraceRow& row : parts) {
        if (row.status == QString::fromUtf8("Очікується")) ++waitingParts;
        if (row.status == QString::fromUtf8("На складі") && row.storageDays >= 7) ++agingParts;
        QStringList subject;
        if (!row.car.isEmpty()) subject.append(row.car);
        if (!row.client.isEmpty()) subject.append(row.client);
        const QString part = row.partNumber.isEmpty() ? row.name : QStringLiteral("%1 | %2").arg(row.name, row.partNumber);
        partsModel.rows.append({row.visibleOrderNumber > 0 ? QString::number(row.visibleOrderNumber) : QString(), part, subject.join(QStringLiteral(" | ")),
                                orderDateTimeText(row.orderedAt), row.store, invoiceText(row), dateText(row.deliveredOn), dateText(row.installedOn),
                                row.deliveredOn.isValid() && !row.installedOn.isValid() ? QString::number(row.storageDays) : QString(),
                                row.status, row.responsible});
        QVariantMap detail;
        detail.insert(QStringLiteral("id"), row.id);
        detail.insert(QStringLiteral("orderNumber"), row.orderNumber);
        detail.insert(QStringLiteral("visibleOrderNumber"), row.visibleOrderNumber);
        detail.insert(QStringLiteral("orderUid"), row.orderUid);
        detail.insert(QStringLiteral("part"), part);
        detail.insert(QStringLiteral("orderedAt"), orderDateTimeText(row.orderedAt));
        detail.insert(QStringLiteral("store"), row.store);
        detail.insert(QStringLiteral("invoice"), invoiceText(row));
        partRows.append(detail);
    }
    setProperty("partRows", partRows);
    m_partsModel->setSnapshot(partsModel);

    restoreStatsTableHeader(m_dashboardTable);
    restoreStatsTableHeader(m_activityTable);
    restoreStatsTableHeader(m_teamTable);
    restoreStatsTableHeader(m_partsTable);
    updateSummary(events.size(), activeStaff, waitingParts, agingParts);
}

void StatsPage::showEventDetails(int sourceRow)
{
    const QVariantList events = property("filteredEvents").toList();
    if (sourceRow < 0 || sourceRow >= events.size()) return;
    ActivityEventDetailsDialog dialog(events.at(sourceRow).toMap(), this);
    dialog.exec();
}

void StatsPage::showTeamMemberDetails(int sourceRow)
{
    const QVariantList teamRows = property("teamRows").toList();
    if (sourceRow < 0 || sourceRow >= teamRows.size()) return;

    const QVariantMap member = teamRows.at(sourceRow).toMap();
    const QString actorId = member.value(QStringLiteral("accountId")).toString();
    if (actorId.trimmed().isEmpty()) return;

    const QVariantList allEvents = property("filteredEvents").toList();
    QVariantList memberEvents;
    TabularReadModel activity;
    activity.headers = {QString::fromUtf8("Дата / час"), QString::fromUtf8("Працівник"), QString::fromUtf8("Роль"),
                        QString::fromUtf8("Розділ"), QString::fromUtf8("Подія"), QString::fromUtf8("Наряд"), QString::fromUtf8("Об'єкт")};
    for (const QVariant& value : allEvents) {
        const QVariantMap event = value.toMap();
        if (event.value(QStringLiteral("actor_id")).toString() != actorId) continue;

        memberEvents.append(event);
        const int orderNumber = event.value(QStringLiteral("visibleOrder")).toInt();
        activity.rows.append({dateTimeText(event.value(QStringLiteral("at")).toDateTime()),
                              event.value(QStringLiteral("actor")).toString(),
                              roleText(event.value(QStringLiteral("role")).toString()),
                              categoryText(event.value(QStringLiteral("category")).toString()),
                              eventTitleText(event),
                              orderNumber > 0 ? QString::number(orderNumber) : QString(),
                              event.value(QStringLiteral("subject")).toString()});
    }

    const QString name = member.value(QStringLiteral("name")).toString();
    if (memberEvents.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("Команда"),
                                 QString::fromUtf8("За вибраний період для працівника «%1» дій не знайдено.").arg(name));
        return;
    }

    const QString subtitle = QString::fromUtf8("%1: дій %2, входів %3, робіт %4, запчастин %5, матеріалів %6. "
                                                "Подвійний клік по рядку відкриває повну деталізацію дії.")
        .arg(name)
        .arg(member.value(QStringLiteral("actions")).toInt())
        .arg(member.value(QStringLiteral("logins")).toInt())
        .arg(member.value(QStringLiteral("works")).toInt())
        .arg(member.value(QStringLiteral("parts")).toInt())
        .arg(member.value(QStringLiteral("inventory")).toInt());
    MetricTableDialog dialog(
        QString::fromUtf8("Історія роботи: %1").arg(name),
        subtitle,
        activity,
        [memberEvents, this](int row) {
            if (row < 0 || row >= memberEvents.size()) return;
            ActivityEventDetailsDialog eventDialog(memberEvents.at(row).toMap(), this);
            eventDialog.exec();
        },
        this);
    dialog.exec();
}

void StatsPage::openPartOrder(int sourceRow)
{
    const QVariantList partRows = property("partRows").toList();
    if (sourceRow < 0 || sourceRow >= partRows.size()) return;

    const QVariantMap part = partRows.at(sourceRow).toMap();
    const int orderNumber = part.value(QStringLiteral("orderNumber")).toInt();
    if (orderNumber <= 0) {
        QMessageBox::information(this, QString::fromUtf8("Рух запчастин"),
                                 QString::fromUtf8("Для цієї запчастини номер наряду не збережено."));
        return;
    }

    const CurrentOrdersCoordinator::DialogRunResult result = CurrentOrdersCoordinator::openExistingOrderEditor(
        this, orderNumber, part.value(QStringLiteral("orderUid")).toString());
    if (!result.launched) {
        QMessageBox::warning(this, QString::fromUtf8("Не вдалося відкрити наряд"),
                             result.errorText.trimmed().isEmpty()
                                 ? QString::fromUtf8("Наряд №%1 недоступний.").arg(part.value(QStringLiteral("visibleOrderNumber")).toInt())
                                 : result.errorText);
        return;
    }

    m_dirty = true;
    refresh();
}

void StatsPage::showEventsMetricDetails()
{
    const QVariantList events = property("filteredEvents").toList();
    MetricTableDialog dialog(
        QString::fromUtf8("Події за період"),
        QString::fromUtf8("Повний перелік подій за поточним періодом і фільтрами. Подвійний клік відкриває картку події."),
        m_activityModel->snapshot(),
        [events, this](int row) {
            if (row < 0 || row >= events.size()) return;
            ActivityEventDetailsDialog eventDialog(events.at(row).toMap(), this);
            eventDialog.exec();
        },
        this);
    dialog.exec();
}

void StatsPage::showTeamMetricDetails()
{
    MetricTableDialog dialog(
        QString::fromUtf8("Активні працівники"),
        QString::fromUtf8("Активність кожного працівника за вибраний період: входи, операції, роботи, запчастини та матеріали. "
                           "Подвійний клік відкриває історію роботи працівника."),
        m_teamModel->snapshot(),
        [this](int row) { showTeamMemberDetails(row); },
        this);
    dialog.exec();
}

void StatsPage::showWaitingPartsMetricDetails()
{
    const TabularReadModel& source = m_partsModel->snapshot();
    TabularReadModel details;
    details.headers = source.headers;
    for (const QVector<QVariant>& row : source.rows) {
        if (row.size() > 9 && row.at(9).toString() == QString::fromUtf8("Очікується")) {
            details.rows.append(row);
        }
    }
    MetricTableDialog dialog(
        QString::fromUtf8("Запчастини в очікуванні"),
        QString::fromUtf8("Позиції, які замовлені для нарядів, але ще не надійшли на склад."),
        details,
        {},
        this);
    dialog.exec();
}

void StatsPage::showAgingPartsMetricDetails()
{
    const TabularReadModel& source = m_partsModel->snapshot();
    TabularReadModel details;
    details.headers = source.headers;
    for (const QVector<QVariant>& row : source.rows) {
        if (row.size() > 9 && row.at(9).toString() == QString::fromUtf8("На складі") && row.at(8).toInt() >= 7) {
            details.rows.append(row);
        }
    }
    MetricTableDialog dialog(
        QString::fromUtf8("Запчастини на складі понад 7 днів"),
        QString::fromUtf8("Позиції, які надійшли на склад, але ще не були встановлені в наряд."),
        details,
        {},
        this);
    dialog.exec();
}

void StatsPage::updateSummary(int visibleEvents, int activeStaff, int waitingParts, int agingParts)
{
    m_eventsMetric->setText(QString::number(visibleEvents));
    m_staffMetric->setText(QString::number(activeStaff));
    m_waitingMetric->setText(QString::number(waitingParts));
    m_agingMetric->setText(QString::number(agingParts));
    m_summary->setText(QString::fromUtf8("Операційний контроль: %1 дій користувачів за вибраний період.")
                       .arg(visibleEvents));
    m_alertText->setText(
        agingParts > 0
            ? QString::fromUtf8("Увага: %1 запчастин перебувають на складі понад 7 днів. Відкрийте «Рух запчастин» для деталей.")
                  .arg(agingParts)
            : (waitingParts > 0
                ? QString::fromUtf8("У роботі є %1 запчастин зі статусом «Очікується».").arg(waitingParts)
                : QString::fromUtf8("Контроль запчастин: прострочених позицій на складі немає.")));
}
