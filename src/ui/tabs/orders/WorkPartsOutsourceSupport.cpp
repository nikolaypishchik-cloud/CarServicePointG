#include "core/util/DateFormats.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

// extracted from WorkPartsDialogSupport.cpp as part of p.4 decomposition
#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"

static QString outsourceCompanyDisplayName(const QString& companyName)
{
    const QString company = companyName.trimmed();
    if (company.isEmpty()) return QString::fromUtf8("Аутсорс");
    return QString::fromUtf8("Аутсорс \"%1\"").arg(company);
}

static QJsonObject outsourceCompanyToJson(const OutsourceCompanyEntry& e)
{
    QJsonObject o;
    o["id"] = e.id;
    o["company"] = e.company;
    o["address"] = e.address;
    o["contact"] = e.contact;
    o["branch"] = e.branch;
    o["note"] = e.note;
    o["markup"] = e.markup;
    return o;
}

static OutsourceCompanyEntry outsourceCompanyFromJson(const QJsonObject& o)
{
    OutsourceCompanyEntry e;
    e.id = o.value("id").toString();
    if (e.id.isEmpty()) e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.company = o.value("company").toString();
    e.address = o.value("address").toString();
    e.contact = o.value("contact").toString();
    e.branch = o.value("branch").toString();
    e.note = o.value("note").toString();
    e.markup = o.value("markup").toDouble(20.0);
    return e;
}

static QList<OutsourceCompanyEntry> loadOutsourceCompanies()
{
    QList<OutsourceCompanyEntry> out;
    const auto outsourceEntries = OutsourceDirectorySettingsService::loadEntries();
    QJsonArray arr;
    for (const auto& entry : outsourceEntries) {
        QJsonObject o;
        o[QStringLiteral("id")] = entry.id;
        o[QStringLiteral("company")] = entry.company;
        o[QStringLiteral("address")] = entry.address;
        o[QStringLiteral("contact")] = entry.contact;
        o[QStringLiteral("branch")] = entry.branch;
        o[QStringLiteral("note")] = entry.note;
        o[QStringLiteral("markup")] = entry.markup;
        arr.push_back(o);
    }
    const auto raw = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    if (!raw.trimmed().isEmpty()) {
        const auto doc = QJsonDocument::fromJson(raw.toUtf8());
        if (doc.isArray()) {
            for (const auto& v : doc.array()) {
                if (v.isObject()) out.push_back(outsourceCompanyFromJson(v.toObject()));
            }
        }
    }
    if (out.isEmpty()) {
        out.push_back({QUuid::createUuid().toString(QUuid::WithoutBraces), QString::fromUtf8("Ремонт генераторів"), QString(), QString(), QString(), QString(), 20.0});
        out.push_back({QUuid::createUuid().toString(QUuid::WithoutBraces), QString::fromUtf8("Ремонт стартерів"), QString(), QString(), QString(), QString(), 20.0});
        out.push_back({QUuid::createUuid().toString(QUuid::WithoutBraces), QString::fromUtf8("Ремонт форсунок"), QString(), QString(), QString(), QString(), 20.0});
    }
    return out;
}

QString resolvedOrderUidForUi(int orderNumber)
{
    return WorkPartsReadQueryService::canonicalOrderUidForInternalNumber(orderNumber).trimmed();
}

bool ensureWorkPartsDialogDbReady(QWidget* parent, bool refreshRouting, QString* errorText)
{
    Q_UNUSED(refreshRouting);
    QString dbError;
    if (!AppRuntimeService::ensureConnectionAlive(&dbError)) {
        if (errorText) *errorText = dbError;
        if (parent) {
            QMessageBox::critical(parent, QString::fromUtf8("Помилка"),
                                  QString::fromUtf8("Не вдалося підготувати БД для роботи з нарядом.\n%1").arg(dbError));
        }
        return false;
    }
    // Avoid heavyweight startup schema/routing refresh on interactive button
    // clicks; it was causing multi-minute stalls in outsource/detail flows.
    return true;
}

QHash<int, QString> loadOutsourceServiceDisplayNames(int orderNumber)
{
    const QString orderUid = resolvedOrderUidForUi(orderNumber);
    return WorkPartsReadQueryService::loadOutsourceServiceDisplayNames(orderUid);
}

bool saveOutsourceJob(int orderNumber, int serviceId, const OutsourceCompanyEntry& company, const OutsourceJobDraft& draft, QWidget* parent)
{
    const QString orderUid = resolvedOrderUidForUi(orderNumber);
    QString errorText;
    if (!OutsourceJobService::saveJob(
                                      orderUid,
                                      orderNumber,
                                      serviceId,
                                      company.id,
                                      company.company,
                                      draft.sentDate,
                                      draft.returnDate,
                                      draft.partName,
                                      draft.companyPrice,
                                      draft.markup,
                                      draft.clientPrice,
                                      draft.note,
                                      &errorText)) {
        QMessageBox::critical(parent, QString::fromUtf8("Помилка"), errorText);
        return false;
    }
    return true;
}


struct OutsourceLinkedServices {
    int removeServiceId = 0;
    int installServiceId = 0;
};

static QString normalizedOutsourceServiceTitle(const QString& prefix, const QString& partName)
{
    return normalizeSentence(QString::fromUtf8("%1 %2").arg(prefix, partName));
}

static OutsourceLinkedServices detectOutsourceLinkedServices(int orderNumber, int serviceId, const QString& partName,
                                                             double* removePrice = nullptr, double* installPrice = nullptr)
{
    OutsourceLinkedServices links;
    if (removePrice) *removePrice = 0.0;
    if (installPrice) *installPrice = 0.0;

    const QString removeExact = normalizedOutsourceServiceTitle(QString::fromUtf8("Зняття"), partName);
    const QString installExact = normalizedOutsourceServiceTitle(QString::fromUtf8("Встановлення"), partName);
    const QString partNeedle = normalizeSentence(partName);
    int fallbackRemoveId = 0;
    int fallbackInstallId = 0;
    double fallbackRemovePrice = 0.0;
    double fallbackInstallPrice = 0.0;

    const auto services = WorkPartsReadQueryService::listServicesByInternalNumber(orderNumber);
    if (services.isEmpty()) return links;

    for (const auto& service : services) {
        const int sid = service.id;
        if (sid == serviceId) continue;
        const QString title = normalizeSentence(service.title);
        const double price = service.price;

        if (links.removeServiceId == 0 && title.compare(removeExact, Qt::CaseInsensitive) == 0) {
            links.removeServiceId = sid;
            if (removePrice) *removePrice = price;
            continue;
        }
        if (links.installServiceId == 0 && title.compare(installExact, Qt::CaseInsensitive) == 0) {
            links.installServiceId = sid;
            if (installPrice) *installPrice = price;
            continue;
        }
        if (!partNeedle.isEmpty() && fallbackRemoveId == 0 &&
            title.startsWith(QString::fromUtf8("Зняття "), Qt::CaseInsensitive) &&
            title.contains(partNeedle, Qt::CaseInsensitive)) {
            fallbackRemoveId = sid;
            fallbackRemovePrice = price;
            continue;
        }
        if (!partNeedle.isEmpty() && fallbackInstallId == 0 &&
            title.startsWith(QString::fromUtf8("Встановлення "), Qt::CaseInsensitive) &&
            title.contains(partNeedle, Qt::CaseInsensitive)) {
            fallbackInstallId = sid;
            fallbackInstallPrice = price;
            continue;
        }
    }

    if (links.removeServiceId <= 0 && fallbackRemoveId > 0) {
        links.removeServiceId = fallbackRemoveId;
        if (removePrice) *removePrice = fallbackRemovePrice;
    }
    if (links.installServiceId <= 0 && fallbackInstallId > 0) {
        links.installServiceId = fallbackInstallId;
        if (installPrice) *installPrice = fallbackInstallPrice;
    }
    return links;
}

static bool loadOutsourceJobForEdit(int orderNumber, int serviceId,
                                    OutsourceCompanyEntry& company,
                                    OutsourceJobDraft& draft,
                                    OutsourceLinkedServices* linksOut = nullptr)
{
    const QString orderUid = resolvedOrderUidForUi(orderNumber);
    if (orderUid.isEmpty()) return false;
    const auto snapshot = WorkPartsQueryService::loadOutsourceJob(orderUid, serviceId);
    if (!snapshot.found) return false;

    company = OutsourceCompanyEntry{};
    company.id = snapshot.companyId;
    company.company = snapshot.companyName;
    company.markup = snapshot.markupPercent;
    const auto companies = loadOutsourceCompanies();
    for (const auto& candidate : companies) {
        if ((!company.id.isEmpty() && candidate.id == company.id) ||
            (!company.company.isEmpty() && candidate.company.compare(company.company, Qt::CaseInsensitive) == 0)) {
            company = candidate;
            break;
        }
    }
    if (company.id.isEmpty()) company.id = snapshot.companyId;
    if (company.company.isEmpty()) company.company = snapshot.companyName;
    if (company.company.isEmpty()) company.company = QString::fromUtf8("(без компанії)");

    draft = OutsourceJobDraft{};
    draft.sentDate = DateFormats::parseDate(snapshot.sentDate);
    if (!draft.sentDate.isValid()) draft.sentDate = QDate::currentDate();
    draft.returnDate = DateFormats::parseDate(snapshot.returnDate);
    draft.partName = normalizeSentence(snapshot.partName);
    draft.companyPrice = FormatUtils::roundMoneyValue(snapshot.companyPrice);
    draft.markup = snapshot.markupPercent;
    draft.clientPrice = FormatUtils::roundMoneyValue(snapshot.clientPrice);
    draft.note = snapshot.note.trimmed();

    double removePrice = 0.0;
    double installPrice = 0.0;
    const OutsourceLinkedServices links = detectOutsourceLinkedServices(orderNumber, serviceId, draft.partName, &removePrice, &installPrice);
    draft.removePrice = FormatUtils::roundMoneyValue(removePrice);
    draft.installPrice = FormatUtils::roundMoneyValue(installPrice);
    if (linksOut) *linksOut = links;
    return true;
}

static bool updateOutsourceRelatedService(int orderNumber, int serviceId, const QString& title, double price, QWidget* parent)
{
    if (serviceId <= 0) return true;
    WorkOrderServiceRow service;
    bool found = false;
    const auto services = WorkPartsReadQueryService::listServicesByInternalNumber(orderNumber);
    for (const auto& candidate : services) {
        if (candidate.id == serviceId) {
            service = candidate;
            found = true;
            break;
        }
    }
    if (!found) return true;

    service.title = normalizeSentence(title);
    service.price = FormatUtils::roundMoneyValue(qMax(0.0, price));
    service.amount = FormatUtils::roundMoneyValue(service.qty * service.price);

    QString repoError;
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("ui.tabs.orders.WorkPartsOutsourceSupport");
    request.entityType = QStringLiteral("service");
    request.entityId = QString::number(serviceId);
    request.operation = QStringLiteral("update_outsource_related_service");
    request.description = QString::fromUtf8("Оновлення пов'язаної роботи аутсорсу");
    request.changedTables = {QStringLiteral("work_order_services"), QStringLiteral("service_masters")};
    request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію БД.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити оновлення пов'язаної роботи аутсорсу.");
    const auto execResult = WorkPartsMutationService::execute(request, [&](auto& db, QString* txError) {
        if (!WorkPartsPersistenceRepositoryFacade::updateService(db, orderNumber, serviceId, service, &repoError)) {
            if (txError) *txError = repoError;
            return false;
        }
        return true;
    });
    if (!execResult.ok) {
        QMessageBox::critical(parent, QString::fromUtf8("Помилка"), execResult.errorText);
        return false;
    }
    publishWorkPartsBackupEvent(QStringLiteral("order.outsource"),
                                QStringLiteral("service"),
                                QString::number(serviceId),
                                AppRuntimeService::resolveOrderUid(orderNumber),
                                orderNumber,
                                QStringLiteral("update"),
                                QString::fromUtf8("Оновлено пов'язану роботу аутсорсу"),
                                {QStringLiteral("work_order_services"), QStringLiteral("service_masters")});
    return true;
}

bool clearOutsourceServiceMaster(int orderNumber, int serviceId, QWidget* parent)
{
    if (serviceId <= 0) return true;
    QString repoError;
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("ui.tabs.orders.WorkPartsOutsourceSupport");
    request.entityType = QStringLiteral("service");
    request.entityId = QString::number(serviceId);
    request.operation = QStringLiteral("clear_outsource_service_master");
    request.description = QString::fromUtf8("Очищення майстра у пов'язаній роботі аутсорсу");
    request.changedTables = {QStringLiteral("service_masters")};
    request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію БД.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити очищення майстра у пов'язаній роботі аутсорсу.");
    const auto execResult = WorkPartsMutationService::execute(request, [&](auto& db, QString* txError) {
        if (!WorkPartsPersistenceRepositoryFacade::replaceServiceMasterLink(db, serviceId, QString(), QString(), &repoError)) {
            if (txError) *txError = repoError;
            return false;
        }
        return true;
    });
    if (!execResult.ok) {
        QMessageBox::critical(parent, QString::fromUtf8("Помилка"), execResult.errorText);
        return false;
    }
    publishWorkPartsBackupEvent(QStringLiteral("order.outsource"),
                                QStringLiteral("service"),
                                QString::number(serviceId),
                                AppRuntimeService::resolveOrderUid(orderNumber),
                                orderNumber,
                                QStringLiteral("update"),
                                QString::fromUtf8("Очищено майстра у пов'язаній роботі аутсорсу"),
                                {QStringLiteral("service_masters")});
    return true;
}

static bool runOutsourceDetailDialog(QWidget* parent, int orderNumber, const OutsourceCompanyEntry& company, OutsourceJobDraft& draft)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QString::fromUtf8("Аутсорс — відправити запчастину на ремонт"));
    dlg.resize(980, 520);
    dlg.setStyleSheet(QString::fromUtf8(
        "QDialog { background-color: rgba(88,96,112,242); }"
        "QLabel { color: #f5f7fa; }"
        "QCheckBox { color: #f5f7fa; font-weight: 600; }"
        "QLineEdit, QPlainTextEdit, QDateEdit, QDoubleSpinBox {"
        " background: rgba(255,255,255,0.97); color: #101214; border: 1px solid rgba(205,215,225,0.72);"
        " border-radius: 12px; padding: 8px 12px; selection-background-color: rgba(105,145,210,0.55); }"
        "QDateEdit::drop-down, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 28px; }"
        "QPushButton { min-width: 138px; min-height: 40px; border-radius: 14px;"
        " color: #f5f7fa; font-weight: 700; padding: 8px 18px;"
        " border: 1px solid rgba(220,230,242,0.22);"
        " background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(150,160,170,0.32), stop:1 rgba(85,92,102,0.18)); }"
        "QPushButton:hover { border-color: rgba(240,245,250,0.38); background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 rgba(168,178,188,0.38),stop:1 rgba(96,104,114,0.22)); }"
        "QPushButton:pressed { padding-top: 10px; padding-left: 20px; }"));

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(22, 22, 22, 22);
    root->setSpacing(18);

    auto* summary = new QLabel(QString::fromUtf8("Наряд: %1 | Компанія: <b>%2</b>").arg(orderNumber).arg(company.company), &dlg);
    summary->setTextFormat(Qt::RichText);
    summary->setStyleSheet(QString::fromUtf8("font-size:16pt;font-weight:700;color:#f5f7fa;"));
    root->addWidget(summary);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignTop);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(14);

    auto* sentDate = new QDateEdit(draft.sentDate.isValid() ? draft.sentDate : QDate::currentDate(), &dlg);
    sentDate->setCalendarPopup(true);
    sentDate->setDisplayFormat("dd.MM.yy");
    sentDate->setMinimumHeight(42);

    // The legacy return_date field stores the plan, not a received/returned event.
    auto* returnDateEnabled = new QCheckBox(QString::fromUtf8("Запланована дата повернення"), &dlg);
    returnDateEnabled->setChecked(draft.returnDate.isValid());

    auto* returnDate = new QDateEdit(draft.returnDate.isValid() ? draft.returnDate : QDate::currentDate(), &dlg);
    returnDate->setCalendarPopup(true);
    returnDate->setDisplayFormat("dd.MM.yy");
    returnDate->setMinimumHeight(42);
    returnDate->setToolTip(QString::fromUtf8("Дату повернення можна відредагувати заздалегідь, навіть якщо запчастина ще не повернулась."));

    auto* partName = new QLineEdit(draft.partName, &dlg);
    partName->setPlaceholderText(QString::fromUtf8("Напр.: генератор / стартер / форсунка ..."));
    partName->setMinimumHeight(42);
    installSentenceCaseAutoCap(partName);

    auto* removePrice = new ClearOnFocusSpinBox(&dlg);
    removePrice->setRange(0.0, 10000000.0);
    removePrice->setDecimals(2);
    removePrice->setSuffix(QString::fromUtf8(" грн"));
    removePrice->setValue(draft.removePrice);
    removePrice->setMinimumHeight(42);

    auto* companyPrice = new ClearOnFocusSpinBox(&dlg);
    companyPrice->setRange(0.0, 10000000.0);
    companyPrice->setDecimals(2);
    companyPrice->setSuffix(QString::fromUtf8(" грн"));
    companyPrice->setValue(draft.companyPrice);
    companyPrice->setMinimumHeight(42);

    auto* markup = new QDoubleSpinBox(&dlg);
    markup->setRange(0.0, 1000.0);
    markup->setDecimals(1);
    markup->setSuffix(QString::fromUtf8(" %"));
    markup->setValue(draft.markup > 0.0 ? draft.markup : company.markup);
    markup->setMinimumHeight(42);

    auto* clientPrice = new QLabel(QString::fromUtf8("0 грн"), &dlg);
    clientPrice->setStyleSheet(QString::fromUtf8("font-size:15pt;font-weight:700;color:#ffffff;"));

    auto* installPrice = new ClearOnFocusSpinBox(&dlg);
    installPrice->setRange(0.0, 10000000.0);
    installPrice->setDecimals(2);
    installPrice->setSuffix(QString::fromUtf8(" грн"));
    installPrice->setValue(draft.installPrice);
    installPrice->setMinimumHeight(42);

    auto recalc = [companyPrice, markup, clientPrice, &draft]() {
        const double price = FormatUtils::roundMoneyValue(companyPrice->value() * (1.0 + markup->value() / 100.0));
        draft.clientPrice = price;
        clientPrice->setText(QString::fromUtf8("%1 грн").arg(FormatUtils::money(price, 0)));
    };

    auto* note = new QPlainTextEdit(draft.note, &dlg);
    note->setPlaceholderText(QString::fromUtf8("Примітка (необов’язково)"));
    note->setFixedHeight(48);

    form->addRow(QString::fromUtf8("Дата відправки:"), sentDate);
    form->addRow(QString(), returnDateEnabled);
    form->addRow(QString::fromUtf8("План повернення:"), returnDate);
    form->addRow(QString::fromUtf8("Запчастина:"), partName);
    form->addRow(QString::fromUtf8("Зняття запчастини:"), removePrice);
    form->addRow(QString::fromUtf8("Встановлення запчастини:"), installPrice);
    form->addRow(QString::fromUtf8("Ціна ремонту (компанія):"), companyPrice);
    form->addRow(QString::fromUtf8("Націнка до ремонту:"), markup);
    form->addRow(QString::fromUtf8("Ціна клієнту:"), clientPrice);
    form->addRow(QString::fromUtf8("Примітка:"), note);
    root->addLayout(form);

    returnDate->setEnabled(returnDateEnabled->isChecked());
    QObject::connect(returnDateEnabled, &QCheckBox::toggled, &dlg, [&draft, returnDate](bool checked) {
        returnDate->setEnabled(checked);
        if (checked) {
            draft.returnDate = returnDate->date();
        } else {
            draft.returnDate = QDate();
        }
    });
    QObject::connect(returnDate, &QDateEdit::dateChanged, &dlg, [&draft, returnDateEnabled](const QDate& d) {
        draft.returnDate = returnDateEnabled->isChecked() ? d : QDate();
    });
    QObject::connect(companyPrice, qOverload<double>(&QDoubleSpinBox::valueChanged), &dlg, [recalc](double){ recalc(); });
    QObject::connect(markup, qOverload<double>(&QDoubleSpinBox::valueChanged), &dlg, [recalc](double){ recalc(); });
    recalc();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    buttons->setCenterButtons(false);
    const int shrinkWidthPx = qMax(1, qRound((8.0 * dlg.logicalDpiX()) / 25.4));
    const int shrinkHeightPx = qMax(1, qRound((4.0 * dlg.logicalDpiY()) / 25.4));
    auto tuneDialogActionButton = [&](QPushButton* btn, bool primary) {
        if (!btn) return;
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(glossyActionButtonStyle(primary));
        btn->setText(primary ? QString::fromUtf8("Зберегти") : QString::fromUtf8("Скасувати"));
        btn->ensurePolished();
        const QSize hinted = btn->sizeHint();
        const int targetW = qMax(132, hinted.width() - shrinkWidthPx);
        const int targetH = qMax(34, hinted.height() - shrinkHeightPx);
        btn->setFixedSize(targetW, targetH);
    };
    tuneDialogActionButton(buttons->button(QDialogButtonBox::Save), true);
    tuneDialogActionButton(buttons->button(QDialogButtonBox::Cancel), false);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
        const QString part = normalizeSentence(partName->text());
        if (part.isEmpty()) {
            QMessageBox::warning(&dlg, QString::fromUtf8("Аутсорс"), QString::fromUtf8("Вкажіть запчастину."));
            return;
        }
        draft.sentDate = sentDate->date();
        draft.returnDate = returnDateEnabled->isChecked() ? returnDate->date() : QDate();
        draft.partName = part;
        draft.removePrice = FormatUtils::roundMoneyValue(removePrice->value());
        draft.companyPrice = FormatUtils::roundMoneyValue(companyPrice->value());
        draft.markup = markup->value();
        draft.installPrice = FormatUtils::roundMoneyValue(installPrice->value());
        draft.note = note->toPlainText().trimmed();
        draft.clientPrice = FormatUtils::roundMoneyValue(draft.companyPrice * (1.0 + draft.markup / 100.0));
        if (!draft.returnDate.isValid()) draft.returnDate = QDate();
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(buttons, 0, Qt::AlignRight);

    return dlg.exec() == QDialog::Accepted;
}

bool chooseOutsourceCompanyAndFill(QWidget* parent, int orderNumber, OutsourceCompanyEntry& selectedCompany, OutsourceJobDraft& draft)
{
    const auto companies = loadOutsourceCompanies();
    if (companies.isEmpty()) {
        QMessageBox::warning(parent, QString::fromUtf8("Аутсорс"), QString::fromUtf8("Немає компаній аутсорсу в налаштуваннях."));
        return false;
    }

    QDialog dlg(parent);
    dlg.setWindowTitle(QString::fromUtf8("Аутсорс — вибір компанії"));
    dlg.resize(920, 520);
    dlg.setStyleSheet(QString::fromUtf8(
        "QDialog {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "  stop:0 rgba(182,192,204,0.78),"
        "  stop:0.12 rgba(170,181,194,0.74),"
        "  stop:0.50 rgba(150,162,176,0.78),"
        "  stop:0.54 rgba(136,148,162,0.80),"
        "  stop:1 rgba(124,136,150,0.82));"
        " }"
        "QLabel { color:#ffffff; }"
        "QHeaderView::section {"
        " color:#1c2430; font-weight:800;"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(245,247,251,0.98), stop:1 rgba(225,232,241,0.98));"
        " border:1px solid rgba(125,136,152,0.38);"
        " padding:10px 12px; }"
        "QTableWidget {"
        " background:rgba(251,252,254,0.98); color:#121722;"
        " gridline-color:rgba(92,102,118,0.20);"
        " border:1px solid rgba(255,255,255,0.28);"
        " border-radius:12px;"
        " selection-background-color:rgba(171,191,223,0.62);"
        " selection-color:#0f1720; }"
        "QTableWidget::item { padding:10px 12px; }"));

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(14);

    auto* title = new QLabel(QString::fromUtf8("Оберіть компанію для ремонту запчастини:"), &dlg);
    title->setStyleSheet(QString::fromUtf8("font-size:15pt;font-weight:800;color:#ffffff;"));
    root->addWidget(title);

    auto* table = new QTableWidget(companies.size(), 2, &dlg);
    table->setHorizontalHeaderLabels({QString::fromUtf8("Компанія"), QString::fromUtf8("Націнка (%)")});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (int i = 0; i < companies.size(); ++i) {
        auto* c0 = new QTableWidgetItem(companies[i].company);
        c0->setData(Qt::UserRole, i);
        auto* c1 = new QTableWidgetItem(QString::number(companies[i].markup, 'f', 0));
        table->setItem(i, 0, c0);
        table->setItem(i, 1, c1);
    }
    root->addWidget(table, 1);

    auto finishForRow = [&](int row) {
        if (row < 0 || row >= companies.size()) return;
        OutsourceCompanyEntry company = companies[row];
        OutsourceJobDraft localDraft;
        localDraft.markup = company.markup;
        if (runOutsourceDetailDialog(&dlg, orderNumber, company, localDraft)) {
            selectedCompany = company;
            draft = localDraft;
            dlg.accept();
        }
    };

    QObject::connect(table, &QTableWidget::cellDoubleClicked, &dlg, [&](int row, int){ finishForRow(row); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    if (auto* okBtn = buttons->button(QDialogButtonBox::Ok)) {
        okBtn->setText(QString::fromUtf8("OK"));
        okBtn->setCursor(Qt::PointingHandCursor);
        okBtn->setStyleSheet(glossyActionButtonStyle(true));
        okBtn->setFixedSize(154, 40);
    }
    if (auto* cancelBtn = buttons->button(QDialogButtonBox::Cancel)) {
        cancelBtn->setText(QString::fromUtf8("Скасувати"));
        cancelBtn->setCursor(Qt::PointingHandCursor);
        cancelBtn->setStyleSheet(glossyActionButtonStyle(false));
        cancelBtn->setFixedSize(172, 40);
    }
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() { finishForRow(table->currentRow()); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(buttons, 0, Qt::AlignRight);

    return dlg.exec() == QDialog::Accepted;
}


bool WorkPartsDialog::runEditOutsourceJobFlow(QWidget* parent, int orderNumber, int serviceId)
{
    if (orderNumber <= 0 || serviceId <= 0) return false;

    OutsourceCompanyEntry company;
    OutsourceJobDraft draft;
    OutsourceLinkedServices links;
    if (!loadOutsourceJobForEdit(orderNumber, serviceId, company, draft, &links)) {
        QMessageBox::warning(parent, QString::fromUtf8("Аутсорс"), QString::fromUtf8("Не вдалося знайти запис аутсорсу для цієї запчастини."));
        return false;
    }

    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2200);
    if (!runOutsourceDetailDialog(parent, orderNumber, company, draft)) return false;

    if (!updateOutsourceRelatedService(orderNumber, links.removeServiceId,
                                       QString::fromUtf8("Зняття %1").arg(draft.partName),
                                       draft.removePrice, parent)) return false;
    if (!updateOutsourceRelatedService(orderNumber, links.installServiceId,
                                       QString::fromUtf8("Встановлення %1").arg(draft.partName),
                                       draft.installPrice, parent)) return false;
    if (!updateOutsourceRelatedService(orderNumber, serviceId,
                                       draft.partName,
                                       draft.clientPrice, parent)) return false;
    if (!clearOutsourceServiceMaster(orderNumber, serviceId, parent)) return false;
    if (!saveOutsourceJob(orderNumber, serviceId, company, draft, parent)) return false;
    AppRuntimeService::refreshOrderRegistry(orderNumber);
    publishWorkPartsBackupEvent(QStringLiteral("order.outsource"),
                                QStringLiteral("outsource_job"),
                                QString::number(serviceId),
                                resolvedOrderUidForUi(orderNumber),
                                orderNumber,
                                QStringLiteral("update"),
                                QString::fromUtf8("Оновлено аутсорс-ремонт"),
                                {QStringLiteral("outsource_jobs"), QStringLiteral("work_order_services"), QStringLiteral("service_masters")});
    return true;
}
