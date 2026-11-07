#include "ui/tabs/settings/SettingsIntegrationsSection.h"
#include "ui/tabs/settings/PasswordVisibilityIcon.h"
#include "ui/tabs/settings/SupplierConnectionPage.h"
#include <QAction>
#include <QCheckBox>
#include <functional>

#include "core/services/settings/GeneralSettingsService.h"
#include "core/services/ElitConnectionCheck.h"
#include "core/services/ElitInvoiceReader.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QScreen>
#include "core/services/booking/BookingSettings.h"
#include "core/services/diagnostics/DiagnosticReportSender.h"
#include "core/services/orders/PricingRecalculationService.h"
#include "core/services/storage/StorageDirectoriesGuard.h"
#include "runtime/AppRuntime.h"
#include "runtime/AsyncThreadScope.h"
#include "ui/tabs/settings/SettingsPageDataSupport.h"
#include "ui/tabs/settings/SettingsDetailUiSupport.h"
#include "ui/theme/ThemeManager.h"

#include "ui/tabs/settings/LabeledToggle.h"
#include <QDir>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPointer>
#include <QScrollArea>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <atomic>
#include <memory>

namespace {

bool isDarkUi()
{
    return ThemeManager::currentTheme() == QStringLiteral("graphite");
}

QString cPrimary()
{
    return isDarkUi() ? QStringLiteral("#f7fbff") : QStringLiteral("#111111");
}

QString cSecondary()
{
    return isDarkUi() ? QStringLiteral("#c7d0db") : QStringLiteral("#4b5563");
}

[[maybe_unused]] void keepPartPricingGateAnchors()
{
    static const QString label = QString::fromUtf8("Перевірити цілісність націнок");
    Q_UNUSED(label);
    Q_UNUSED(static_cast<bool (*)(PricingRecalculationService::PricingIntegrityCheckResult*, QString*)>(
        &PricingRecalculationService::checkPartPricingIntegrity));
    Q_UNUSED(static_cast<bool (*)(PricingRecalculationService::RecalculationScope, QString*)>(
        &PricingRecalculationService::recalcPartMarkupsFromSettings));
}

}

void SettingsIntegrationsSection::sendDiagnosticReport(QWidget* owner, QPushButton* sendButton)
{
    if (!owner || !sendButton) return;

    sendButton->setEnabled(false);
    sendButton->setText(QString::fromUtf8("Відправляється..."));

    struct SendState {
        DiagnosticReportSender::Result result;
    };

    auto state = std::make_shared<SendState>();
    auto cancelToken = std::make_shared<std::atomic_bool>(false);
    QPointer<QWidget> ownerGuard(owner);
    QPointer<QPushButton> buttonGuard(sendButton);
    auto* worker = AsyncThreadScope::startWorker(owner,
                                                 QStringLiteral("settings_diagnostic_report_send_worker"),
                                                 cancelToken,
                                                 [state](const std::shared_ptr<std::atomic_bool>& cancelToken) {
        if (cancelToken && cancelToken->load()) return;
        state->result = DiagnosticReportSender::send();
    });

    QObject::connect(worker, &QThread::finished, owner, [ownerGuard, buttonGuard, state, cancelToken]() {
        if (cancelToken) cancelToken->store(true);
        if (buttonGuard) {
            buttonGuard->setEnabled(true);
            buttonGuard->setText(QString::fromUtf8("Відправити звіт"));
        }
        if (!ownerGuard) return;

        const auto& result = state->result;
        if (result.sent) {
            QMessageBox::information(ownerGuard,
                                     QString::fromUtf8("Відправити звіт"),
                                     result.message.isEmpty() ? QString::fromUtf8("Звіт відправлено") : result.message);
            return;
        }

        const QString localCopy = result.localCopySaved && !result.reportPath.trimmed().isEmpty()
            ? QString::fromUtf8("\n\nЛокальна копія звіту:\n%1").arg(QDir::toNativeSeparators(result.reportPath))
            : QString();
        QMessageBox::warning(ownerGuard,
                             QString::fromUtf8("Відправити звіт"),
                             QString::fromUtf8("Не вдалося відправити звіт.\n\n%1%2")
                                 .arg(result.errorText.trimmed().isEmpty()
                                          ? QString::fromUtf8("Невідома помилка transport.")
                                          : result.errorText.trimmed(),
                                      localCopy));
    });
    worker->start();
}

QWidget* SettingsIntegrationsSection::buildDiagnosticReportPage(QWidget* owner)
{
    auto* w = new QWidget(owner);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(18,18,18,18);
    l->setSpacing(12);

    auto* title = new QLabel(QString::fromUtf8("Звіт та логи"), w);
    title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(cPrimary()));
    l->addWidget(title);

    const QString logsDir = AppRuntime::logsDir();
    auto* info = new QLabel(QString::fromUtf8("Службові дії для діагностики застосунку. Відправка звіту запускається тільки з цієї сторінки."), w);
    info->setWordWrap(true);
    info->setStyleSheet(QString("color:%1;").arg(cSecondary()));
    l->addWidget(info);

    auto* path = new QLabel(QString::fromUtf8("Папка логів: %1").arg(QDir::toNativeSeparators(logsDir)), w);
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path->setStyleSheet(QString("color:%1;").arg(cSecondary()));
    l->addWidget(path);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 8, 0, 0);
    buttons->setSpacing(10);

    auto* openLogs = new QPushButton(QString::fromUtf8("Відкрити логи"), w);
    openLogs->setFixedSize(155, 52);
    openLogs->setCursor(Qt::PointingHandCursor);
    openLogs->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
    buttons->addWidget(openLogs, 0, Qt::AlignLeft);

    auto* sendReport = new QPushButton(QString::fromUtf8("Відправити звіт"), w);
    sendReport->setFixedSize(170, 52);
    sendReport->setCursor(Qt::PointingHandCursor);
    sendReport->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
    buttons->addWidget(sendReport, 0, Qt::AlignLeft);
    buttons->addStretch(1);
    l->addLayout(buttons);

    QObject::connect(openLogs, &QPushButton::clicked, w, [w, logsDir]() {
        QDir().mkpath(logsDir);
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(logsDir))) {
            QMessageBox::warning(w,
                                 QString::fromUtf8("Відкрити логи"),
                                 QString::fromUtf8("Не вдалося відкрити папку логів:\n%1").arg(QDir::toNativeSeparators(logsDir)));
        }
    });

    QObject::connect(sendReport, &QPushButton::clicked, w, [w, sendReport]() {
        SettingsIntegrationsSection::sendDiagnosticReport(w, sendReport);
    });

    l->addStretch(1);
    return w;
}

QWidget* SettingsIntegrationsSection::build(const QString& id,
                                            QWidget* owner,
                                            const std::function<QWidget*()>& googleSheetsBuilder,
                                            const std::function<QWidget*()>& autoBackupBuilder,
                                            const std::function<QWidget*()>& syncBuilder)
{
    if (id.startsWith("int.supplier."))return buildSupplierConnectionPage(id.section('.',2),owner);
    if (id == QStringLiteral("int.apikeys") || id==QStringLiteral("int.suppliers")) {
        auto* pages = new QStackedWidget(owner);
        pages->setObjectName(QStringLiteral("apiSettingsPages"));
        auto* menu = new QWidget(pages);
        auto* menuLayout = new QVBoxLayout(menu);
        menuLayout->addWidget(SettingsDetailUiSupport::makeBackButton(menu), 0, Qt::AlignLeft);
        pages->addWidget(menu);
        const QList<QPair<QString, QString>> entries=id=="int.suppliers"?QList<QPair<QString,QString>>{
            {"int.api.elit",QString::fromUtf8("Еліт")},{"int.supplier.bm","BM Parts"},
            {"int.supplier.omega",QString::fromUtf8("Омега")},{"int.supplier.utr",QString::fromUtf8("Юнік Трейд")}}
            :QList<QPair<QString,QString>>{{"int.api.odb",QString::fromUtf8("Опендатабот")},{"int.api.booking","CarServsce"}};
        for (const auto& entry : entries) {
            auto* page = new QWidget(pages);
            auto* layout = new QVBoxLayout(page);
            auto* back = SettingsDetailUiSupport::makeBackButton(page);
            back->setProperty("stay_in_detail_menu", true);
            layout->addWidget(back, 0, Qt::AlignLeft);
            layout->addWidget(build(entry.first, page, {}, {}, {}), 1);
            pages->addWidget(page);
            auto* button = new QPushButton(entry.second, menu);
            button->setObjectName(entry.first);
            button->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
            button->setFixedWidth(300);
            menuLayout->addWidget(button,0,Qt::AlignLeft);
            QObject::connect(button, &QPushButton::clicked, pages, [pages, page] { pages->setCurrentWidget(page); });
            QObject::connect(back, &QPushButton::clicked, pages, [pages, menu] { pages->setCurrentWidget(menu); });
        }
        menuLayout->addStretch();
        return pages;
    }
    if (id.startsWith(QStringLiteral("int.api."))) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18,18,18,18);
        l->setSpacing(10);

        auto addRow = [w, l](const QString& text, QWidget* field) {
            field->setProperty("settingsCompactInput", true);
            auto* row = new QHBoxLayout();
            auto* lbl = new QLabel(text, w);
            lbl->setStyleSheet(QString("color:%1;").arg(cPrimary()));
            lbl->setMinimumWidth(210);
            row->addWidget(lbl);
            row->addWidget(field, 1);
            l->addLayout(row);
        };
        if (id == QStringLiteral("int.api.odb")) {
        auto* title = new QLabel(QString::fromUtf8("API Опендатабот"), w);
        title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(cPrimary()));
        l->addWidget(title);

        auto* info = new QLabel(QString::fromUtf8("Налаштування OpenDataBot для автоматичного підбору даних авто за держномером / VIN."), w);
        info->setWordWrap(true);
        info->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        l->addWidget(info);

        const auto openDataBot = GeneralSettingsService::loadOpenDataBotSettings();
        auto* apiEnabled = new LabeledToggle(QString::fromUtf8("Увімкнути API-запити (онлайн режим)"), w);
        apiEnabled->setChecked(openDataBot.apiEnabled);
        l->addWidget(apiEnabled);

        auto* baseUrl = new QLineEdit(openDataBot.baseUrl, w);
        auto* apiKey = new QLineEdit(openDataBot.apiKey, w);
        auto* apiKeyParam = new QLineEdit(openDataBot.apiKeyParam, w);
        auto* headerName = new QLineEdit(openDataBot.headerName, w);
        auto* plateEndpoint = new QLineEdit(openDataBot.plateEndpoint, w);
        auto* plateParam = new QLineEdit(openDataBot.plateParam, w);
        auto* vinEndpoint = new QLineEdit(openDataBot.vinEndpoint, w);
        auto* vinParam = new QLineEdit(openDataBot.vinParam, w);
        auto* testPlate = new QLineEdit(openDataBot.testPlate, w);
        for (auto* field : {baseUrl, apiKey, apiKeyParam, headerName, plateEndpoint, plateParam, vinEndpoint, vinParam})
            field->setProperty("inputCorrectionRole", "protected");
        testPlate->setProperty("inputCorrectionRole", "plate");
        apiKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);

        addRow(QString::fromUtf8("Base URL:"), baseUrl);
        addRow(QString::fromUtf8("API key:"), apiKey);
        addRow(QString::fromUtf8("API key param (query):"), apiKeyParam);
        addRow(QString::fromUtf8("Header name:"), headerName);
        addRow(QString::fromUtf8("Plate endpoint:"), plateEndpoint);
        addRow(QString::fromUtf8("Plate param:"), plateParam);
        addRow(QString::fromUtf8("VIN endpoint:"), vinEndpoint);
        addRow(QString::fromUtf8("VIN param:"), vinParam);
        addRow(QString::fromUtf8("Тестовий номер:"), testPlate);

        auto* row = new QHBoxLayout();
        auto* save = new QPushButton(QString::fromUtf8("Зберегти"), w);
        save->setProperty("stay_in_detail_menu", true);
        save->setFixedSize(120, 40);
        save->setCursor(Qt::PointingHandCursor);
        save->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
        row->addWidget(save, 0, Qt::AlignLeft);
        row->addStretch(1);
        l->addLayout(row);

        auto saveAll = [=]() -> bool {
            bool changed = false;
            GeneralSettingsService::OpenDataBotSettingsPayload payload;
            payload.apiEnabled = apiEnabled->isChecked();
            payload.demoMode = false;
            payload.baseUrl = baseUrl->text().trimmed();
            payload.apiKey = apiKey->text().trimmed();
            payload.apiKeyParam = apiKeyParam->text().trimmed();
            payload.headerName = headerName->text().trimmed();
            payload.plateEndpoint = plateEndpoint->text().trimmed();
            payload.plateParam = plateParam->text().trimmed();
            payload.vinEndpoint = vinEndpoint->text().trimmed();
            payload.vinParam = vinParam->text().trimmed();
            payload.testPlate = testPlate->text().trimmed();
            GeneralSettingsService::saveOpenDataBotSettings(payload, &changed);
            if (changed) {
                SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("odb_settings_save"),
                                                                    QStringLiteral("integration_settings"),
                                                                    QStringLiteral("settings.int.odb"),
                                                                    QString::fromUtf8("Збережено налаштування інтеграції OpenDataBot"),
                                                                    {QStringLiteral("settings")});
            }
            return changed;
        };

        auto applyEnabledState = [=](bool enabled) {
            for (auto* edit : {baseUrl, apiKey, apiKeyParam, headerName, plateEndpoint, plateParam, vinEndpoint, vinParam, testPlate}) {
                edit->setEnabled(enabled);
            }
        };
        applyEnabledState(apiEnabled->isChecked());

        QObject::connect(save, &QPushButton::clicked, w, [w, saveAll, apiEnabled]() {
            saveAll();
            QMessageBox::information(w,
                                     QString::fromUtf8("OpenDataBot"),
                                     apiEnabled->isChecked()
                                         ? QString::fromUtf8("API-запити OpenDataBot увімкнено і параметри збережено.")
                                         : QString::fromUtf8("OpenDataBot переведено в офлайн-режим."));
        });
        QObject::connect(apiEnabled, &LabeledToggle::toggled, w, [saveAll, applyEnabledState](bool on) {
            applyEnabledState(on);
            saveAll();
        });

        }
        if (id == QStringLiteral("int.api.elit")) {
        auto* elitTitle = new QLabel(QString::fromUtf8("API Еліт"), w);
        elitTitle->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1; margin-top:18px;").arg(cPrimary()));
        l->addWidget(elitTitle);
        GeneralSettingsService::ElitCredentials elitCredentials;
        QString elitLoadError;
        GeneralSettingsService::loadElitCredentials(&elitCredentials, &elitLoadError);
        auto* elitLogin = new QLineEdit(elitCredentials.login, w);
        auto* elitPassword = new QLineEdit(elitCredentials.password, w);
        auto* elitCompany = new QLineEdit(elitCredentials.company, w);
        elitLogin->setObjectName(QStringLiteral("elitLogin"));
        elitPassword->setObjectName(QStringLiteral("elitPassword"));
        elitCompany->setObjectName(QStringLiteral("elitCompany"));
        elitCompany->setPlaceholderText(QString::fromUtf8("Код company, наданий Еліт"));
        elitPassword->setEchoMode(QLineEdit::Password);
        auto* elitShowPassword = elitPassword->addAction(passwordVisibilityIcon(false), QLineEdit::TrailingPosition);
        elitShowPassword->setObjectName(QStringLiteral("elitShowPassword"));
        elitShowPassword->setCheckable(true);
        elitShowPassword->setToolTip(QString::fromUtf8("Показати пароль"));
        QObject::connect(elitShowPassword, &QAction::toggled, elitPassword, [=](bool visible) {
            elitPassword->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
            elitShowPassword->setIcon(passwordVisibilityIcon(visible));
            elitShowPassword->setToolTip(visible ? QString::fromUtf8("Приховати пароль") : QString::fromUtf8("Показати пароль"));
        });
        addRow(QString::fromUtf8("Код компанії:"), elitCompany);
        addRow(QString::fromUtf8("Логін:"), elitLogin);
        addRow(QString::fromUtf8("Пароль:"), elitPassword);
        auto* elitRemember = new QCheckBox(QString::fromUtf8("Зберігати пароль"), w);
        elitRemember->setObjectName(QStringLiteral("elitRememberPassword"));
        elitRemember->setChecked(elitCredentials.rememberPassword);
        elitRemember->setToolTip(QString::fromUtf8("Зміна застосовується кнопкою «Зберегти доступ Еліт». Без галочки збережений пароль буде видалено."));
        l->addWidget(elitRemember);
        auto* elitStatus = new QLabel(w);
        elitStatus->setWordWrap(true);
        elitStatus->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        elitStatus->setText(elitLoadError.isEmpty()
            ? QString::fromUtf8("SOAP / BuyerService. Перевірка читає лише кількість накладних, не створює замовлень і не змінює базу.")
            : elitLoadError);
        l->addWidget(elitStatus);
        auto* elitActions = new QHBoxLayout();
        auto* elitSave = new QPushButton(QString::fromUtf8("Зберегти доступ Еліт"), w);
        auto* elitCheck = new QPushButton(QString::fromUtf8("Перевірити підключення"), w);
        elitSave->setObjectName(QStringLiteral("elitSave"));
        elitCheck->setObjectName(QStringLiteral("elitCheck"));
        elitCheck->setToolTip(QString::fromUtf8("Перевірити введені дані без їх збереження та без змін у базі."));
        for (auto* button : {elitSave, elitCheck}) {
            button->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
            elitActions->addWidget(button);
        }
        elitActions->addStretch(1);
        l->addLayout(elitActions);
        auto* elitRead = new QPushButton(QString::fromUtf8("Отримати накладні"), w);
        elitRead->setObjectName(QStringLiteral("elitReadInvoices"));
        elitRead->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
        elitRead->setToolTip(QString::fromUtf8("Перегляд накладних поточного місяця та старих боргів. Без запису в базу."));
        l->addWidget(elitRead);
        QObject::connect(elitRead, &QPushButton::clicked, w, [=]() {
            elitShowPassword->setChecked(false);
            elitRead->setEnabled(false);
            elitRead->setText(QString::fromUtf8("Отримання накладних..."));
            ElitInvoiceReader::start(w,
                {elitLogin->text(), elitPassword->text(), {}, elitCompany->text(), false},
                [=](const ElitInvoiceReader::Result& result) {
                    elitRead->setEnabled(true);
                    elitRead->setText(QString::fromUtf8("Отримати накладні"));
                    if (!w->isVisible()) return;
                    if (!result.complete) {
                        QMessageBox message(QMessageBox::Warning, QString::fromUtf8("Накладні Еліт"),
                            QString(), QMessageBox::Ok, w);
                        message.setTextFormat(Qt::PlainText);
                        message.setText(result.error);
                        message.exec();
                        return;
                    }
                    QDialog dialog(w);
                    dialog.setWindowTitle(QString::fromUtf8("Накладні Еліт · Перегляд без змін у БД"));
                    const auto available = w->screen()->availableGeometry();
                    dialog.resize(qMin(1050, available.width() - 60), qMin(620, available.height() - 80));
                    auto* layout = new QVBoxLayout(&dialog);
                    int open = 0;
                    for (const auto& invoice : result.invoices) if (invoice.remainingCents > 0) ++open;
                    auto* summary = new QLabel(QString::fromUtf8("Отримано з API: %1. За поточний місяць і старі борги: %2; із боргом: %3.\nСтан на %4. Оплати, база та банери не змінені.")
                        .arg(result.availableCount).arg(result.invoices.size()).arg(open)
                        .arg(result.fetchedAt.toString("dd.MM.yyyy HH:mm:ss")), &dialog);
                    summary->setWordWrap(true);
                    layout->addWidget(summary);
                    auto* table = new QTableWidget(int(result.invoices.size()), 7, &dialog);
                    table->setHorizontalHeaderLabels({QString::fromUtf8("Накладна"), QString::fromUtf8("Дата"),
                        QString::fromUtf8("Сплатити до"), QString::fromUtf8("Валюта"), "amount", "amountVAT", QString::fromUtf8("Залишок боргу")});
                    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
                    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
                    table->verticalHeader()->hide();
                    table->setSelectionBehavior(QAbstractItemView::SelectRows);
                    int row = 0;
                    for (const auto& invoice : result.invoices) {
                        const QStringList cells{invoice.number, invoice.posted.toString("dd.MM.yyyy"),
                            invoice.due.isValid() ? invoice.due.toString("dd.MM.yyyy") : QString::fromUtf8("Не вказано"),
                            invoice.currency, QString::number(invoice.amountCents / 100.0, 'f', 2),
                            QString::number(invoice.amountVatCents / 100.0, 'f', 2),
                            QString::number(invoice.remainingCents / 100.0, 'f', 2)};
                        for (int col = 0; col < cells.size(); ++col) table->setItem(row, col, new QTableWidgetItem(cells[col]));
                        ++row;
                    }
                    layout->addWidget(table, 1);
                    auto* note = new QLabel(QString::fromUtf8("amount та amountVAT показані як окремі поля API. Розрахунок загальної суми та звірку оплат буде додано наступним етапом."), &dialog);
                    note->setWordWrap(true); layout->addWidget(note);
                    auto* close = new QPushButton(QString::fromUtf8("Закрити"), &dialog);
                    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
                    layout->addWidget(close, 0, Qt::AlignRight);
                    dialog.move(available.center() - QPoint(dialog.width() / 2, dialog.height() / 2));
                    dialog.exec();
                });
        });
        QObject::connect(elitCheck, &QPushButton::clicked, w, [=]() {
            elitShowPassword->setChecked(false);
            elitCheck->setEnabled(false);
            elitCheck->setText(QString::fromUtf8("Перевіряється..."));
            elitStatus->setText(QString::fromUtf8("Перевірка доступу до читання накладних Еліт..."));
            ElitConnectionCheck::start(w,
                {elitLogin->text(), elitPassword->text(), {}, elitCompany->text(), false},
                [=](const ElitConnectionCheck::Result& result) {
                    elitCheck->setEnabled(true);
                    elitCheck->setText(QString::fromUtf8("Перевірити підключення"));
                    elitStatus->setText(result.accessible
                        ? QString::fromUtf8("Доступ до читання накладних підтверджено для перевірених даних.")
                        : QString::fromUtf8("Доступ не підтверджено. Причина наведена у повідомленні."));
                    QMessageBox message(result.accessible ? QMessageBox::Information : QMessageBox::Warning,
                        QString::fromUtf8("Підключення до Еліт"), QString(), QMessageBox::Ok, w);
                    message.setTextFormat(Qt::PlainText);
                    message.setText(result.message);
                    message.setButtonText(QMessageBox::Ok, QString::fromUtf8("Гаразд"));
                    message.exec();
                });
        });
        QObject::connect(elitSave, &QPushButton::clicked, w, [=]() {
            QString error;
            if (!GeneralSettingsService::saveElitCredentials(
                    {elitLogin->text(), elitPassword->text(), elitCredentials.apiKey, elitCompany->text(), elitRemember->isChecked()}, &error)) {
                QMessageBox::warning(w, QString::fromUtf8("Еліт"), error);
                return;
            }
            elitShowPassword->setChecked(false);
            elitStatus->setText(elitRemember->isChecked()
                ? QString::fromUtf8("Доступ Еліт збережено під захистом Windows. Збереження не є перевіркою підключення.")
                : QString::fromUtf8("Параметри Еліт збережено без пароля. Раніше збережений пароль видалено."));
        });

        }
        if (id == QStringLiteral("int.api.booking")) {
        auto* bookingTitle = new QLabel(QString::fromUtf8("API CarServsce"), w);
        bookingTitle->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1; margin-top:18px;").arg(cPrimary()));
        l->addWidget(bookingTitle);

        auto* bookingInfo = new QLabel(QString::fromUtf8("Синхронізація сайту з формою Планування. Сайт має створювати запис через backend, а застосунок забирає його polling-ом."), w);
        bookingInfo->setWordWrap(true);
        bookingInfo->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        l->addWidget(bookingInfo);

        const BookingSyncSettings bookingSettings = BookingSettings::load();

        auto* bookingEnabled = new LabeledToggle(QString::fromUtf8("Увімкнути онлайн-запис / синхронізацію"), w);
        bookingEnabled->setChecked(bookingSettings.enabled);
        l->addWidget(bookingEnabled);

        auto* bookingBaseUrl = new QLineEdit(bookingSettings.baseUrl, w);
        auto* bookingToken = new QLineEdit(bookingSettings.token, w);
        bookingToken->setEchoMode(QLineEdit::PasswordEchoOnEdit);
        auto* bookingStartHour = new QSpinBox(w);
        bookingStartHour->setRange(0, 23);
        bookingStartHour->setValue(bookingSettings.workStartHour);
        auto* bookingEndHour = new QSpinBox(w);
        bookingEndHour->setRange(1, 24);
        bookingEndHour->setValue(bookingSettings.workEndHour);
        auto* bookingCapacity = new QSpinBox(w);
        bookingCapacity->setRange(1, 20);
        bookingCapacity->setValue(bookingSettings.capacityPerSlot);

        addRow(QString::fromUtf8("Booking API URL:"), bookingBaseUrl);
        addRow(QString::fromUtf8("Booking token:"), bookingToken);
        addRow(QString::fromUtf8("Початок роботи:"), bookingStartHour);
        addRow(QString::fromUtf8("Кінець роботи:"), bookingEndHour);
        addRow(QString::fromUtf8("Постів на слот:"), bookingCapacity);

        auto* bookingSave = new QPushButton(QString::fromUtf8("Зберегти онлайн-запис"), w);
        bookingSave->setFixedSize(210, 40);
        bookingSave->setCursor(Qt::PointingHandCursor);
        bookingSave->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
        l->addWidget(bookingSave, 0, Qt::AlignLeft);

        auto saveBookingSettings = [=]() {
            BookingSyncSettings payload;
            payload.enabled = bookingEnabled->isChecked();
            payload.baseUrl = bookingBaseUrl->text().trimmed();
            payload.token = bookingToken->text().trimmed();
            payload.workStartHour = bookingStartHour->value();
            payload.workEndHour = bookingEndHour->value();
            payload.capacityPerSlot = bookingCapacity->value();
            BookingSettings::save(payload);
        };
        auto applyBookingEnabledState = [=](bool enabled) {
            bookingBaseUrl->setEnabled(enabled);
            bookingToken->setEnabled(enabled);
            bookingStartHour->setEnabled(enabled);
            bookingEndHour->setEnabled(enabled);
            bookingCapacity->setEnabled(enabled);
        };
        applyBookingEnabledState(bookingEnabled->isChecked());
        QObject::connect(bookingSave, &QPushButton::clicked, w, [w, saveBookingSettings]() {
            saveBookingSettings();
            QMessageBox::information(w, QString::fromUtf8("Онлайн-запис"), QString::fromUtf8("Налаштування онлайн-запису збережені. Перезапустіть застосунок або відкрийте його знову, щоб синхронізація стартувала з новими параметрами."));
        });
        QObject::connect(bookingEnabled, &LabeledToggle::toggled, w, [saveBookingSettings, applyBookingEnabledState](bool enabled) {
            applyBookingEnabledState(enabled);
            saveBookingSettings();
        });
        }
        l->addStretch(1);
        l->setSizeConstraint(QLayout::SetMinimumSize);
        auto* scroll = new QScrollArea(owner);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(w);
        return scroll;
    }
    if (id == QStringLiteral("int.security")) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18,18,18,18);
        l->setSpacing(12);

        auto* title = new QLabel(QString::fromUtf8("Безпека"), w);
        title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(cPrimary()));
        l->addWidget(title);

        auto* info = new QLabel(QString::fromUtf8("Тут буде розділ налаштувань безпеки застосунку."), w);
        info->setWordWrap(true);
        info->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        l->addWidget(info);
        l->addStretch(1);
        return w;
    }
    if (id == QStringLiteral("int.directories")) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18,18,18,18);
        l->setSpacing(12);

        auto* title = new QLabel(QString::fromUtf8("Директорії"), w);
        title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(cPrimary()));
        l->addWidget(title);

        auto makePathRow = [w, l](const QString& labelText, const QString& dialogTitle, const QString& emptyMessage, QLineEdit*& target) {
            auto* rowWrap = new QWidget(w);
            auto* row = new QHBoxLayout(rowWrap);
            row->setContentsMargins(0,0,0,0);
            row->setSpacing(10);
            auto* label = new QLabel(labelText, rowWrap);
            label->setMinimumWidth(130);
            target = new QLineEdit(rowWrap);
            target->setProperty("inputCorrectionRole", "protected");
            auto* browse = new QPushButton(QString::fromUtf8("Огляд"), rowWrap);
            browse->setFixedSize(120, 52);
            browse->setCursor(Qt::PointingHandCursor);
            browse->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
            row->addWidget(label);
            row->addWidget(target, 1);
            row->addWidget(browse);
            QObject::connect(browse, &QPushButton::clicked, rowWrap, [rowWrap, target, dialogTitle, emptyMessage]() {
                const QString startDir = target->text().trimmed();
                const QString dir = QFileDialog::getExistingDirectory(rowWrap, dialogTitle, startDir);
                if (dir.isEmpty()) {
                    if (startDir.isEmpty()) {
                        QMessageBox::information(rowWrap, QString::fromUtf8("Директорія"), emptyMessage);
                    }
                    return;
                }
                target->setText(dir);
            });
            l->addWidget(rowWrap);
        };

        const auto dirs = GeneralSettingsService::loadDirectoriesSettings();
        QLineEdit *pdfDir = nullptr, *invoiceDir = nullptr, *reportsDir = nullptr, *orderPhotosDir = nullptr;
        makePathRow(QString::fromUtf8("Наряди / PDF"),
                    QString::fromUtf8("Оберіть директорію для PDF"),
                    QString::fromUtf8("Спочатку задайте директорію для PDF-файлів."),
                    pdfDir);
        makePathRow(QString::fromUtf8("Накладні"),
                    QString::fromUtf8("Оберіть директорію для накладних"),
                    QString::fromUtf8("Спочатку задайте директорію для накладних."),
                    invoiceDir);
        makePathRow(QString::fromUtf8("Звіти"),
                    QString::fromUtf8("Оберіть директорію для звітів"),
                    QString::fromUtf8("Спочатку задайте директорію для звітів."),
                    reportsDir);
        makePathRow(QString::fromUtf8("Фото нарядів"),
                    QString::fromUtf8("Оберіть директорію для фотографій нарядів"),
                    QString::fromUtf8("Спочатку задайте директорію для фотографій нарядів."),
                    orderPhotosDir);
        pdfDir->setText(dirs.pdfDir);
        invoiceDir->setText(dirs.invoicesDir);
        reportsDir->setText(dirs.reportsDir);
        orderPhotosDir->setText(dirs.orderPhotosDir);

        auto* buttons = new QHBoxLayout();
        auto* saveBtn = new QPushButton(QString::fromUtf8("Зберегти"), w);
        saveBtn->setFixedSize(120, 40);
        saveBtn->setCursor(Qt::PointingHandCursor);
        saveBtn->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
        buttons->addWidget(saveBtn, 0);
        buttons->addStretch(1);
        l->addLayout(buttons);

        QObject::connect(saveBtn, &QPushButton::clicked, w, [w, pdfDir, invoiceDir, reportsDir, orderPhotosDir]() {
            bool changed = false;
            GeneralSettingsService::DirectoriesSettingsPayload payload;
            payload.pdfDir = pdfDir->text().trimmed();
            payload.invoicesDir = invoiceDir->text().trimmed();
            payload.reportsDir = reportsDir->text().trimmed();
            payload.orderPhotosDir = orderPhotosDir->text().trimmed();

            QString validationError;
            if (!StorageDirectoriesGuard::validateCandidateDirectory(payload.pdfDir, QString::fromUtf8("наряди / PDF"), &validationError) ||
                !StorageDirectoriesGuard::validateCandidateDirectory(payload.invoicesDir, QString::fromUtf8("накладні"), &validationError) ||
                !StorageDirectoriesGuard::validateCandidateDirectory(payload.reportsDir, QString::fromUtf8("звіти"), &validationError) ||
                !StorageDirectoriesGuard::validateCandidateDirectory(payload.orderPhotosDir, QString::fromUtf8("фото нарядів"), &validationError)) {
                QMessageBox::warning(w, QString::fromUtf8("Директорія"), validationError);
                return;
            }

            GeneralSettingsService::saveDirectoriesSettings(payload, &changed);
            if (changed) {
                SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("directories_save"),
                                                                    QStringLiteral("integration_settings"),
                                                                    QStringLiteral("settings.int.directories"),
                                                                    QString::fromUtf8("Збережено директорії PDF / накладних / звітів / фото нарядів"),
                                                                    {QStringLiteral("settings")});
            }
            QMessageBox::information(w, QString::fromUtf8("Директорія"), QString::fromUtf8("Директорії збережено."));
        });

        l->addStretch(1);
        return w;
    }
    if (id == QStringLiteral("int.gsheets")) {
        return googleSheetsBuilder ? googleSheetsBuilder() : nullptr;
    }
    if (id == QStringLiteral("int.autobackup")) {
        return autoBackupBuilder ? autoBackupBuilder() : nullptr;
    }
    if (id == QStringLiteral("int.sync")) {
        return syncBuilder ? syncBuilder() : nullptr;
    }
    return nullptr;
}
