// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

// extracted from WorkPartsDialog.cpp as part of p.4 decomposition
#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"
#include "ui/tabs/orders/WorkPartsDialog.h"
#include "core/services/orders/PartMarkupPolicyService.h"
#include "core/services/orders/OrderMutationFacade.h"
#include "ui/tabs/orders/BayOccupancyPage.h"
#include "core/cloudsync/CloudSyncClient.h"
#include "core/services/orders/HistoryPartsAutofillService.h"
#include "ui/tabs/orders/HistoryPartsDraftRoles.h"
#include <QPersistentModelIndex>
#include <QScrollArea>
#include "core/services/PurchasePaymentService.h"
#include "core/services/AccessControlService.h"

void WorkPartsDialog::openRepairAdvisorDialog()
{
    QString orderError;
    const QString orderUid = currentOrderUid(&orderError).trimmed();
    if (orderUid.isEmpty()) {
        QMessageBox::warning(this,
                             QString::fromUtf8("ШІ-підбір"),
                             QString::fromUtf8("Не вдалося визначити наряд для ШІ-підбору. Збережіть наряд і повторіть дію."));
        return;
    }

    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QString::fromUtf8("ШІ-підбір робіт і запчастин"));
    dialog->setObjectName("repairAiDialog");
    dialog->setMinimumSize(AdaptiveUi::px(dialog, 980), AdaptiveUi::px(dialog, 680));
    const QSize referenceSize = size().expandedTo(
        QSize(AdaptiveUi::px(dialog, 1120), AdaptiveUi::px(dialog, 760)));
    dialog->resize(qRound(referenceSize.width() * 0.90),
                   qRound(referenceSize.height() * 0.82));

    const bool lightUi = ThemeManager::currentTheme() == QStringLiteral("light");
    dialog->setStyleSheet(lightUi
        ? QStringLiteral(
            "QDialog { background: #f6f8fb; }"
            "QLabel { color: #101418; }"
            "QComboBox, QPlainTextEdit { background: #273848; color: #f8fbff; border: 1px solid #40576c; border-radius: 8px; padding: 6px 10px; selection-background-color: #4b6e8d; selection-color: #ffffff; }"
            "QComboBox::drop-down { border: 0; width: 30px; }"
            "QComboBox QAbstractItemView { background: #273848; color: #f8fbff; selection-background-color: #4b6e8d; selection-color: #ffffff; }"
            "QPlainTextEdit::placeholder { color: #c4cfdb; }"
        )
        : QStringLiteral(
            "QDialog { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #26394c, stop:1 #172432); }"
            "QLabel { color: #f5f7fa; }"
            "QComboBox, QPlainTextEdit { background: #ffffff; color: #101418; border: 1px solid #b9c5d1; border-radius: 8px; padding: 6px 10px; selection-background-color: #dce9f7; selection-color: #101418; }"
            "QComboBox::drop-down { border: 0; width: 30px; }"
            "QComboBox QAbstractItemView { background: #ffffff; color: #101418; selection-background-color: #dce9f7; selection-color: #101418; }"
            "QPlainTextEdit::placeholder { color: #5c6877; }"
        ));

    auto* viewportLayout = new QVBoxLayout(dialog);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    auto* content = new QWidget;
    content->setObjectName("repairAiContent");
    content->setStyleSheet(QStringLiteral("QWidget#repairAiContent { background: transparent; }"));
    auto* root = new QVBoxLayout(content);
    root->setSizeConstraint(QLayout::SetMinimumSize);
    scroll->setWidget(content);
    viewportLayout->addWidget(scroll);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(10);

    auto* title = new QLabel(QString::fromUtf8("ШІ-підбір для наряду №%1").arg(m_orderNumber), dialog);
    QFont titleFont = interfaceHeaderFont(2);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);

    auto* notice = new QLabel(QString::fromUtf8("ШІ формує лише пропозиції. Перевірте сумісність за VIN/OEM, відредагуйте потрібні позиції та зафіксуйте рішення майстра."), dialog);
    notice->setWordWrap(true);
    root->addWidget(notice);

    QStringList orderWorkTitles;
    if (m_servicesTable) {
        for (int row = 0; row < m_servicesTable->rowCount(); ++row) {
            const auto* item = m_servicesTable->item(row, ServiceColTitle);
            const QString title = item ? item->text().trimmed() : QString();
            if (!title.isEmpty() && !orderWorkTitles.contains(title, Qt::CaseInsensitive)) orderWorkTitles.append(title);
        }
    }

    auto* workContext = new QLabel(dialog);
    workContext->setWordWrap(true);
    workContext->setText(orderWorkTitles.isEmpty()
        ? QString::fromUtf8("У таблиці «Виконання робіт» ще немає робіт: вузол і агрегат оберіть вручну.")
        : QString::fromUtf8("Враховано роботи з таблиці «Виконання робіт»: %1").arg(orderWorkTitles.join(QStringLiteral("; "))));
    root->addWidget(workContext);
    const QMap<QString, QStringList> componentsByNode = {
        {QString::fromUtf8("Двигун"), {QString::fromUtf8("ГРМ"), QString::fromUtf8("Головка блока і клапани"), QString::fromUtf8("Поршнева група"), QString::fromUtf8("Система запалювання"), QString::fromUtf8("Навісне обладнання двигуна"), QString::fromUtf8("Опори двигуна")}},
        {QString::fromUtf8("Система мащення"), {QString::fromUtf8("Моторна олива"), QString::fromUtf8("Масляний фільтр"), QString::fromUtf8("Масляний насос"), QString::fromUtf8("Прокладки та ущільнення")}},
        {QString::fromUtf8("Система охолодження"), {QString::fromUtf8("Радіатор"), QString::fromUtf8("Водяний насос"), QString::fromUtf8("Термостат"), QString::fromUtf8("Патрубки"), QString::fromUtf8("Антифриз")}},
        {QString::fromUtf8("Паливна система"), {QString::fromUtf8("Паливний насос"), QString::fromUtf8("Паливний фільтр"), QString::fromUtf8("Форсунки"), QString::fromUtf8("Паливна рампа")}},
        {QString::fromUtf8("Трансмісія"), {QString::fromUtf8("Зчеплення"), QString::fromUtf8("КПП"), QString::fromUtf8("Приводи і піввісі"), QString::fromUtf8("ШРУС")}},
        {QString::fromUtf8("Гальмівна система"), {QString::fromUtf8("Гальмівні колодки і диски"), QString::fromUtf8("Супорти"), QString::fromUtf8("Гальмівні шланги"), QString::fromUtf8("Гальмівна рідина"), QString::fromUtf8("ABS")}},
        {QString::fromUtf8("Підвіска"), {QString::fromUtf8("Амортизатори"), QString::fromUtf8("Пружини"), QString::fromUtf8("Важелі та сайлентблоки"), QString::fromUtf8("Шарові опори"), QString::fromUtf8("Стабілізатор")}},
        {QString::fromUtf8("Рульове керування"), {QString::fromUtf8("Рульова рейка"), QString::fromUtf8("Рульовий насос"), QString::fromUtf8("Рульові тяги і наконечники"), QString::fromUtf8("Рульове колесо")}},
        {QString::fromUtf8("Електрообладнання"), {QString::fromUtf8("Акумулятор"), QString::fromUtf8("Генератор"), QString::fromUtf8("Стартер"), QString::fromUtf8("Датчики"), QString::fromUtf8("Освітлення")}},
        {QString::fromUtf8("Кліматична система"), {QString::fromUtf8("Компресор кондиціонера"), QString::fromUtf8("Радіатор кондиціонера"), QString::fromUtf8("Фільтр салону"), QString::fromUtf8("Вентилятор пічки")}},
        {QString::fromUtf8("Вихлопна система"), {QString::fromUtf8("Каталізатор"), QString::fromUtf8("Лямбда-зонд"), QString::fromUtf8("Глушник"), QString::fromUtf8("Гофра вихлопної системи")}},
        {QString::fromUtf8("Кузов"), {QString::fromUtf8("Бампери і кузовні панелі"), QString::fromUtf8("Двері і замки"), QString::fromUtf8("Скло"), QString::fromUtf8("Дзеркала")}},
    };
    const QStringList nodes = componentsByNode.keys();
    const auto selectedFromWorkTitles = [orderWorkTitles]() -> QPair<QString, QString> {
        const QString workText = orderWorkTitles.join(QStringLiteral(" ")).toLower();
        const auto matches = [&workText](const QStringList& terms) {
            return std::any_of(terms.cbegin(), terms.cend(), [&workText](const QString& term) { return workText.contains(term); });
        };
        if (matches({QString::fromUtf8("гальм"), QString::fromUtf8("колодк"), QString::fromUtf8("диск")})) return {QString::fromUtf8("Гальмівна система"), QString::fromUtf8("Гальмівні колодки і диски")};
        if (matches({QString::fromUtf8("олив"), QString::fromUtf8("маслян"), QString::fromUtf8("мащен")})) return {QString::fromUtf8("Система мащення"), QString::fromUtf8("Моторна олива")};
        if (matches({QString::fromUtf8("охолод"), QString::fromUtf8("антифриз"), QString::fromUtf8("радіатор"), QString::fromUtf8("термостат")})) return {QString::fromUtf8("Система охолодження"), QString::fromUtf8("Радіатор")};
        if (matches({QString::fromUtf8("підвіс"), QString::fromUtf8("амортиз"), QString::fromUtf8("сайлент"), QString::fromUtf8("важіл"), QString::fromUtf8("шаров")})) return {QString::fromUtf8("Підвіска"), QString::fromUtf8("Важелі та сайлентблоки")};
        if (matches({QString::fromUtf8("руль"), QString::fromUtf8("кермов")})) return {QString::fromUtf8("Рульове керування"), QString::fromUtf8("Рульові тяги і наконечники")};
        if (matches({QString::fromUtf8("зчеплен"), QString::fromUtf8("кпп"), QString::fromUtf8("трансміс"), QString::fromUtf8("шрус"), QString::fromUtf8("піввіс")})) return {QString::fromUtf8("Трансмісія"), QString::fromUtf8("Зчеплення")};
        if (matches({QString::fromUtf8("генератор"), QString::fromUtf8("стартер"), QString::fromUtf8("акумулятор"), QString::fromUtf8("електр")})) return {QString::fromUtf8("Електрообладнання"), QString::fromUtf8("Генератор")};
        if (matches({QString::fromUtf8("кондиц"), QString::fromUtf8("клімат"), QString::fromUtf8("компресор")})) return {QString::fromUtf8("Кліматична система"), QString::fromUtf8("Компресор кондиціонера")};
        if (matches({QString::fromUtf8("вихлоп"), QString::fromUtf8("каталіз"), QString::fromUtf8("глушник"), QString::fromUtf8("лямбд")})) return {QString::fromUtf8("Вихлопна система"), QString::fromUtf8("Глушник")};
        if (matches({QString::fromUtf8("палив"), QString::fromUtf8("форсунк")})) return {QString::fromUtf8("Паливна система"), QString::fromUtf8("Паливний насос")};
        return {QString::fromUtf8("Двигун"), QString::fromUtf8("Навісне обладнання двигуна")};
    };

    auto* form = new QGridLayout();
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);
    auto* node = new QComboBox(dialog);
    node->setEditable(false);
    node->addItems(nodes);
    auto* component = new QComboBox(dialog);
    component->setEditable(false);
    const auto refreshComponents = [node, component, componentsByNode](const QString& preferred = QString()) {
        const QSignalBlocker blocker(component);
        component->clear();
        component->addItems(componentsByNode.value(node->currentText()));
        const int index = component->findText(preferred, Qt::MatchFixedString);
        component->setCurrentIndex(index >= 0 ? index : 0);
    };
    const QPair<QString, QString> detectedSelection = selectedFromWorkTitles();
    node->setCurrentText(detectedSelection.first);
    refreshComponents(detectedSelection.second);
    connect(node, qOverload<int>(&QComboBox::currentIndexChanged), dialog, [refreshComponents](int) { refreshComponents(); });

    auto* fault = new QPlainTextEdit(dialog);
    fault->setPlaceholderText(QString::fromUtf8("Необов'язково. Наприклад: шум при гальмуванні, витік оливи, помилка датчика..."));
    fault->setFixedHeight(AdaptiveUi::px(fault, 78));
    form->addWidget(new QLabel(QString::fromUtf8("Вузол:"), dialog), 0, 0);
    form->addWidget(node, 0, 1);
    form->addWidget(new QLabel(QString::fromUtf8("Агрегат / підвузол:"), dialog), 1, 0);
    form->addWidget(component, 1, 1);
    form->addWidget(new QLabel(QString::fromUtf8("Несправність (за потреби):"), dialog), 2, 0, Qt::AlignTop);
    form->addWidget(fault, 2, 1);
    root->addLayout(form);

    auto* requestButton = new QPushButton(QString::fromUtf8("Отримати пропозиції ШІ"), dialog);
    requestButton->setObjectName("repairAiRequest");
    requestButton->setStyleSheet(glossyActionButtonStyle());
    requestButton->ensurePolished();
    const int compactRequestHeight = qMax(requestButton->fontMetrics().height()+12,
        requestButton->sizeHint().height()-qRound(dialog->logicalDpiY()*5.0/25.4));
    requestButton->setStyleSheet(glossyActionButtonStyle()+QStringLiteral("QPushButton { min-height:0; padding:4px 22px; } QPushButton:pressed {padding:4px 22px;}"));
    requestButton->ensurePolished();
    requestButton->setFixedHeight(qMax(compactRequestHeight, requestButton->fontMetrics().height()+12));
    requestButton->setCursor(Qt::PointingHandCursor);
    root->addWidget(requestButton, 0, Qt::AlignLeft);

    auto* status = new QLabel(QString::fromUtf8("Оберіть вузол і агрегат. Опис несправності можна додати за потреби."), dialog);
    status->setWordWrap(true);
    root->addWidget(status);

    auto* table = new QTableWidget(0, 7, dialog);
    table->setHorizontalHeaderLabels({QString::fromUtf8("Рішення"), QString::fromUtf8("Робота"), QString::fromUtf8("Запчастина"),
                                      QString::fromUtf8("К-сть"), QString::fromUtf8("OEM / бренд"), QString::fromUtf8("Джерело"),
                                      QString::fromUtf8("Пояснення")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0, AdaptiveUi::px(table, 92));
    table->setColumnWidth(1, AdaptiveUi::px(table, 180));
    table->setColumnWidth(2, AdaptiveUi::px(table, 220));
    table->setColumnWidth(3, AdaptiveUi::px(table, 70));
    table->setColumnWidth(4, AdaptiveUi::px(table, 150));
    table->setColumnWidth(5, AdaptiveUi::px(table, 145));
    styleDarkFlatTable(table);
    table->setEditTriggers(QAbstractItemView::DoubleClicked |
                           QAbstractItemView::SelectedClicked |
                           QAbstractItemView::EditKeyPressed);
    table->setStyleSheet(table->styleSheet() + QStringLiteral(
        "QTableWidget::indicator { width: 24px; height: 24px; border: 2px solid #102a43; "
        "border-radius: 5px; background: #ffffff; }"
        "QTableWidget::indicator:checked { background: #0d9c5f; border-color: #073b28; }"
        "QTableWidget::item:focus { outline: 2px solid #58a6ff; }"));
    root->addWidget(table, 1);

    auto* learnButton = new QPushButton(QString::fromUtf8("Запам'ятати рішення майстра"), dialog);
    learnButton->setStyleSheet(glossyActionButtonStyle(false, true));
    learnButton->setEnabled(false);
    auto* closeButton = new QPushButton(QString::fromUtf8("Закрити"), dialog);
    closeButton->setStyleSheet(glossyActionButtonStyle(false, true));
    auto* buttons = new QHBoxLayout();
    buttons->addWidget(learnButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    root->addLayout(buttons);

    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
    connect(requestButton, &QPushButton::clicked, dialog, [orderUid, orderWorkTitles, node, component, fault, table, status, learnButton, requestButton]() {
        const QString nodeText = node->currentText().trimmed();
        const QString componentText = component->currentText().trimmed();
        const QString faultText = fault->toPlainText().trimmed();
        if (nodeText.isEmpty() || componentText.isEmpty() || componentText == QString::fromUtf8("Оберіть або введіть агрегат")) {
            status->setText(QString::fromUtf8("Оберіть вузол і агрегат."));
            return;
        }
        const QString effectiveFault = faultText.isEmpty()
            ? QString::fromUtf8("Підбір запчастини для вузла «%1», агрегату «%2». %3")
                  .arg(nodeText, componentText, orderWorkTitles.join(QStringLiteral("; ")))
            : faultText;
        requestButton->setEnabled(false);
        status->setText(QString::fromUtf8("ШІ аналізує вузол, агрегат, дані автомобіля та історію ремонтів СТО..."));
        // Do not dispatch input while this operation owns live widget pointers.
        QJsonObject finding;
        finding.insert(QStringLiteral("node_group"), nodeText);
        finding.insert(QStringLiteral("component"), componentText);
        finding.insert(QStringLiteral("fault"), effectiveFault);
        QJsonArray existingWorkTitles;
        for (const QString& workTitle : orderWorkTitles) existingWorkTitles.append(workTitle);
        finding.insert(QStringLiteral("existing_work_titles"), existingWorkTitles);
        finding.insert(QStringLiteral("existing_work_context"), orderWorkTitles.join(QStringLiteral("; ")));
        QString error;
        const QJsonObject response = CloudSyncClient::requestRepairAdvisorSuggestions(orderUid, QJsonArray{finding}, &error);
        requestButton->setEnabled(true);
        table->setRowCount(0);
        learnButton->setEnabled(false);
        if (response.isEmpty()) {
            status->setText(QString::fromUtf8("Не вдалося отримати пропозиції: %1").arg(error));
            return;
        }
        const QJsonObject advisor = response.value(QStringLiteral("advisor")).toObject();
        status->setText(advisor.value(QStringLiteral("online_status")).toString().isEmpty()
                            ? QString::fromUtf8("Отримано пропозиції. Перевірте та підтвердьте рішення.")
                            : advisor.value(QStringLiteral("online_status")).toString());
        const QJsonArray suggestions = response.value(QStringLiteral("suggestions")).toArray();
        for (const QJsonValue& value : suggestions) {
            const QJsonObject item = value.toObject();
            if (item.isEmpty()) continue;
            const int row = table->rowCount();
            table->insertRow(row);
            auto* decision = new QTableWidgetItem(QString::fromUtf8("Погодити"));
            decision->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            decision->setCheckState(Qt::Checked);
            decision->setData(Qt::UserRole, QJsonDocument(item).toJson(QJsonDocument::Compact));
            table->setItem(row, 0, decision);
            table->setItem(row, 1, new QTableWidgetItem(item.value(QStringLiteral("work_name")).toString()));
            table->setItem(row, 2, new QTableWidgetItem(item.value(QStringLiteral("part_name")).toString()));
            table->setItem(row, 3, new QTableWidgetItem(QString::number(item.value(QStringLiteral("quantity")).toDouble(1.0), 'f', 2)));
            const QString partNumber = item.value(QStringLiteral("part_number")).toString().trimmed();
            const QString manufacturer = item.value(QStringLiteral("manufacturer")).toString().trimmed();
            table->setItem(row, 4, new QTableWidgetItem(partNumber.isEmpty()
                ? manufacturer
                : (manufacturer.isEmpty() ? partNumber : partNumber + QStringLiteral(" / ") + manufacturer)));
            auto* sourceItem = new QTableWidgetItem(item.value(QStringLiteral("source")).toString());
            QStringList sourceUrls;
            for (const QJsonValue& sourceValue : item.value(QStringLiteral("source_urls")).toArray()) {
                const QString sourceUrl = sourceValue.toString().trimmed();
                if (!sourceUrl.isEmpty()) sourceUrls.append(sourceUrl);
            }
            if (!sourceUrls.isEmpty()) sourceItem->setToolTip(sourceUrls.join(QLatin1Char('\n')));
            table->setItem(row, 5, sourceItem);
            table->setItem(row, 6, new QTableWidgetItem(item.value(QStringLiteral("reason")).toString()));
        }
        if (table->rowCount() == 0) {
            status->setText(QString::fromUtf8("ШІ не знайшов безпечних пропозицій. Додайте позицію вручну після уточнення діагностики."));
            return;
        }
        learnButton->setEnabled(true);
    });
    connect(learnButton, &QPushButton::clicked, dialog, [this, orderUid, table, status, learnButton]() {
        struct ApprovedPartDraft {
            QString workName;
            QString name;
            QString partNumber;
            QString manufacturer;
            QString nodeGroup;
            QString component;
            QString reason;
            double quantity = 1.0;
        };

        QJsonArray feedback;
        QList<ApprovedPartDraft> approvedParts;
        for (int row = 0; row < table->rowCount(); ++row) {
            auto* decision = table->item(row, 0);
            if (!decision) continue;
            const QJsonDocument originalDocument = QJsonDocument::fromJson(decision->data(Qt::UserRole).toByteArray());
            const QJsonObject original = originalDocument.object();
            if (original.isEmpty()) continue;

            const QString workName = table->item(row, 1) ? table->item(row, 1)->text().trimmed() : QString();
            const QString partName = table->item(row, 2) ? table->item(row, 2)->text().trimmed() : QString();
            bool quantityOk = false;
            const double editedQuantity = table->item(row, 3)
                ? table->item(row, 3)->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&quantityOk)
                : 1.0;
            const double quantity = quantityOk && editedQuantity > 0.0 ? editedQuantity : 1.0;

            QString partNumber = original.value(QStringLiteral("part_number")).toString().trimmed();
            QString manufacturer = original.value(QStringLiteral("manufacturer")).toString().trimmed();
            const QString oemAndBrand = table->item(row, 4) ? table->item(row, 4)->text().trimmed() : QString();
            if (!oemAndBrand.isEmpty()) {
                const QStringList values = oemAndBrand.split(QRegularExpression(QStringLiteral("\\s*/\\s*")), Qt::SkipEmptyParts);
                if (!values.isEmpty()) partNumber = values.constFirst().trimmed();
                if (values.size() > 1) manufacturer = values.at(1).trimmed();
            }

            QJsonObject item;
            item.insert(QStringLiteral("node_group"), original.value(QStringLiteral("node_group")));
            item.insert(QStringLiteral("component"), original.value(QStringLiteral("component")));
            item.insert(QStringLiteral("fault"), original.value(QStringLiteral("fault")));
            item.insert(QStringLiteral("original_part_name"), original.value(QStringLiteral("part_name")));
            item.insert(QStringLiteral("original_quantity"), original.value(QStringLiteral("quantity")));
            item.insert(QStringLiteral("part_name"), partName);
            item.insert(QStringLiteral("work_name"), workName);
            item.insert(QStringLiteral("quantity"), quantity);
            item.insert(QStringLiteral("part_number"), partNumber);
            item.insert(QStringLiteral("manufacturer"), manufacturer);
            const bool approved = decision->checkState() == Qt::Checked;
            item.insert(QStringLiteral("approved"), approved);
            feedback.append(item);

            if (approved && !partName.isEmpty()) {
                approvedParts.append({workName,
                                      partName,
                                      partNumber,
                                      manufacturer,
                                      original.value(QStringLiteral("node_group")).toString().trimmed(),
                                      original.value(QStringLiteral("component")).toString().trimmed(),
                                      table->item(row, 6) ? table->item(row, 6)->text().trimmed() : QString(),
                                      quantity});
            }
        }
        if (feedback.isEmpty()) {
            status->setText(QString::fromUtf8("Немає рішень для збереження."));
            return;
        }
        learnButton->setEnabled(false);
        status->setText(QString::fromUtf8("Зберігаємо підтвердження майстра і додаємо погоджені запчастини до наряду..."));
        // Do not dispatch input while this operation owns live widget pointers.
        QString error;
        if (!CloudSyncClient::recordRepairAdvisorFeedback(orderUid, feedback, &error)) {
            learnButton->setEnabled(true);
            status->setText(QString::fromUtf8("Не вдалося зберегти рішення: %1").arg(error));
            return;
        }

        QString pullError;
        const bool pulledServerChanges = CloudSyncClient::syncNow(&pullError);
        if (approvedParts.isEmpty()) {
            refreshAfterMutation(true, true, true, false);
            status->setText(pulledServerChanges
                ? QString::fromUtf8("Рішення збережено. Погоджених запчастин немає; затверджені роботи додано до наряду.")
                : QString::fromUtf8("Рішення збережено. Оновлення наряду буде повторено під час наступної синхронізації: %1").arg(pullError));
            return;
        }

        QString routeError;
        if (!ensureOrderReadyForEditing(m_orderNumber, &routeError)) {
            learnButton->setEnabled(true);
            status->setText(QString::fromUtf8("Рішення збережено, але не вдалося додати запчастини до наряду: %1").arg(routeError));
            return;
        }

        const QList<WorkOrderServiceRow> services = WorkPartsReadQueryService::listServicesByUid(orderUid);
        const auto serviceIdForWork = [services](const QString& workName) {
            const QString wanted = workName.trimmed();
            if (wanted.isEmpty()) return 0;
            for (const WorkOrderServiceRow& service : services) {
                if (service.title.trimmed().compare(wanted, Qt::CaseInsensitive) == 0) return service.id;
            }
            return 0;
        };

        MutationExecutor::Request request;
        request.sourceModule = QStringLiteral("ui.tabs.orders.WorkPartsDialogUiBuilder");
        request.entityType = QStringLiteral("repair_ai_approved_parts");
        request.entityId = orderUid;
        request.operation = QStringLiteral("create_approved_repair_ai_parts");
        request.description = QString::fromUtf8("Додавання погоджених ШІ запчастин до наряду");
        request.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
        request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію БД");
        request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити додавання запчастин ШІ");
        const auto insertResult = WorkPartsMutationService::execute(request,
            [this, &approvedParts, serviceIdForWork](auto& db, QString* transactionError) {
                for (const ApprovedPartDraft& draft : approvedParts) {
                    PartRowPayloadInput payloadInput;
                    payloadInput.orderedAt = QDate::currentDate().toString(Qt::ISODate);
                    payloadInput.partNumber = draft.partNumber;
                    payloadInput.name = normalizeSentence(draft.name);
                    payloadInput.manufacturer = draft.manufacturer;
                    payloadInput.qty = draft.quantity;
                    payloadInput.purchasePrice = 0.0;
                    payloadInput.sourceType = QStringLiteral("repair_ai_approved");
                    if (m_btnCustomerParts->isChecked()) payloadInput.store = PartMarkupPolicyService::customerPartsStore();
                    payloadInput.serviceId = serviceIdForWork(draft.workName);
                    payloadInput.note = QString::fromUtf8("Погоджено майстром за пропозицією ШІ. Вузол: %1. Агрегат: %2. %3")
                        .arg(draft.nodeGroup, draft.component, draft.reason);
                    const WorkOrderPartRow payload = PartRowPayloadBuilderService::build(payloadInput);
                    QString saveError;
                    if (!WorkPartsPersistenceRepositoryFacade::insertPart(db, m_orderNumber, payload, nullptr, &saveError)) {
                        if (transactionError) {
                            *transactionError = QString::fromUtf8("Не вдалося додати погоджену ШІ запчастину «%1».\n%2")
                                .arg(draft.name, saveError);
                        }
                        return false;
                    }
                }
                QString statusError;
                if (!promoteOrderStatusToInProgressIfNeeded(db.connectionName(), m_orderNumber, &statusError)) {
                    if (transactionError) *transactionError = statusError;
                    return false;
                }
                return true;
            });
        if (!insertResult.ok) {
            learnButton->setEnabled(true);
            status->setText(QString::fromUtf8("Рішення збережено, але запчастини не додано: %1").arg(insertResult.errorText));
            return;
        }

        autoAssignPartsToServices();
        refreshAfterMutation(true, true, true, false);
        publishWorkPartsBackupEvent(QStringLiteral("order.parts"),
                                    QStringLiteral("repair_ai_approved"),
                                    QString::number(approvedParts.size()),
                                    orderUid,
                                    m_orderNumber,
                                    QStringLiteral("create_approved_repair_ai_parts"),
                                    QString::fromUtf8("Додано погоджені ШІ запчастини"),
                                    {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")});

        QString pushError;
        const bool pushedLocalChanges = CloudSyncClient::syncNow(&pushError);
        if (pulledServerChanges && pushedLocalChanges) {
            status->setText(QString::fromUtf8("Рішення збережено. Погоджені роботи й запчастини додано до наряду та синхронізовано."));
        } else {
            const QString syncError = !pushError.trimmed().isEmpty() ? pushError : pullError;
            status->setText(QString::fromUtf8("Рішення й запчастини збережено локально. Синхронізацію буде повторено: %1")
                                .arg(syncError.isEmpty() ? QString::fromUtf8("сервер тимчасово недоступний") : syncError));
        }
    });

    if (auto* screen = dialog->screen()) {
        const QRect area = screen->availableGeometry();
        const int inset = qRound(dialog->logicalDpiY()*7.0/25.4);
        dialog->setMinimumSize(0,0);
        dialog->resize(qMin(dialog->width(),area.width()),qMin(dialog->height(),area.height()-inset-32));
        dialog->move(area.left()+(area.width()-dialog->width())/2,area.top()+inset+32);
    }
    dialog->exec();
}
