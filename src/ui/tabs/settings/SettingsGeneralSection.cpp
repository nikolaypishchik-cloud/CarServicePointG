#include "core/services/settings/UiStateSettingsService.h"
#include "ui/tabs/settings/SettingsGeneralSection.h"
#include "ui/tabs/statistics/StatsPage.h"
#include "ui/tabs/settings/LabeledToggle.h"
#include <functional>

#include "ui/tabs/settings/SettingsPageDataSupport.h"
#include "ui/tabs/settings/SettingsDetailUiSupport.h"
#include "core/services/settings/GeneralSettingsService.h"
#include "core/services/PdfWebsiteFooter.h"
#include "core/services/ElitSyncVoice.h"
#include <QScrollArea>
#include "core/services/settings/UiSettingsService.h"
#include "app/ClickSoundFilter.h"
#include "app/update/UpdateService.h"
#include "ui/theme/ThemeManager.h"

#include <QComboBox>
#include <QGridLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

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

QString pdfInputStyle()
{
    return QStringLiteral(
        "QLineEdit{background:#ffffff;color:#05070a;placeholder-text-color:#1f2937;"
        "border:1px solid #aab4c0;border-radius:6px;padding:6px 8px;"
        "selection-background-color:#2b5d95;selection-color:#ffffff;}"
        "QLineEdit:focus{border:2px solid #7ea4f4;padding:5px 7px;}");
}

}

QWidget* SettingsGeneralSection::build(const QString& id,
                                       QWidget* owner,
                                       const std::function<QWidget*()>& brandingBuilder)
{

    if (id == QStringLiteral("general.work_log")) {
        auto* page=new StatsPage(owner,StatsPage::View::WorkLog);
        page->setProperty("settingsWorkLog",true);
        page->setProperty("settings_detail_inline_title",QString::fromUtf8("Журнал робіт"));
        return page;
    }
    if (id == QStringLiteral("general.updates")) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18,18,18,18);
        l->setSpacing(12);

        auto* title = new QLabel(QString::fromUtf8("Оновлення"), w);
        title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(cPrimary()));
        l->addWidget(title);

        auto* current = new QLabel(QString::fromUtf8("Поточна версія: %1").arg(UpdateService::currentVersion()), w);
        current->setStyleSheet(QString("font-size:11pt; font-weight:700; color:%1;").arg(cPrimary()));
        l->addWidget(current);

        auto* cb = new LabeledToggle(QString::fromUtf8("Перевіряти оновлення при запуску"), w);
        cb->setChecked(GeneralSettingsService::loadUpdatesAuto());
        l->addWidget(cb);

        auto* repoInfo = new QLabel(QString::fromUtf8("Сервер оновлень: GitHub Releases\nRepo: %1/%2")
                                    .arg(UpdateService::githubOwner(), UpdateService::githubRepo()), w);
        repoInfo->setWordWrap(true);
        repoInfo->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        l->addWidget(repoInfo);

        auto* row = new QHBoxLayout();
        auto* btn = new QPushButton(QString::fromUtf8("Перевірити оновлення"), w);
        btn->setFixedWidth(220);
        row->addWidget(btn);
        row->addStretch(1);
        l->addLayout(row);

        auto* info = new QLabel(QString::fromUtf8("Застосунок перевіряє latest release у GitHub, показує нову версію та запускає оновлення після підтвердження."), w);
        info->setWordWrap(true);
        info->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        l->addWidget(info);

        l->addStretch(1);

        QObject::connect(cb, &LabeledToggle::toggled, w, [](bool on) {
            bool changed = false;
            GeneralSettingsService::saveUpdatesAuto(on, &changed);
            if (changed) {
                SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("updates_settings_save"),
                                                                   QStringLiteral("update_settings"),
                                                                   QStringLiteral("settings.updates"),
                                                                   QString::fromUtf8("Змінено автоперевірку оновлень"),
                                                                   {QStringLiteral("settings")});
            }
        });
        QObject::connect(btn, &QPushButton::clicked, w, [owner]() {
            UpdateService::promptAndRunUpdate(owner, false);
        });

        return w;
    }

    if (id == QStringLiteral("reports.pdf") || id == QStringLiteral("rep.pdf") || id == QStringLiteral("general.print") || id == QStringLiteral("general.pdf")) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18, 18, 18, 18);
        l->setSpacing(12);

        auto* btnBack = SettingsDetailUiSupport::makeBackButton(w);
        btnBack->setFixedSize(120, 40);
        btnBack->setMinimumSize(120, 40);
        btnBack->setMaximumSize(120, 40);
        btnBack->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(true));
        auto* backRow = new QHBoxLayout();
        backRow->setContentsMargins(0, 0, 0, 0);
        backRow->setSpacing(0);
        backRow->addWidget(btnBack, 0, Qt::AlignLeft);
        backRow->addStretch(1);
        l->addLayout(backRow);

        auto* title = new QLabel(QString::fromUtf8("PDF / друк"), w);
        title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(cPrimary()));
        l->addWidget(title);

        const auto reportsPdfSettings = GeneralSettingsService::loadReportsPdfSettings();

        auto* cbLogo = new LabeledToggle(QString::fromUtf8("Додавати логотип у PDF"), w);
        cbLogo->setChecked(reportsPdfSettings.includeLogo);
        l->addWidget(cbLogo);

        auto* cbPhone = new LabeledToggle(QString::fromUtf8("Додавати телефон у футер"), w);
        cbPhone->setChecked(reportsPdfSettings.includePhone);
        l->addWidget(cbPhone);

        auto* cbCustomLogoPosition = new LabeledToggle(QString::fromUtf8("Увімкнути ручне позиціонування логотипу на листі PDF"), w);
        cbCustomLogoPosition->setChecked(reportsPdfSettings.useCustomLogoPosition);
        l->addWidget(cbCustomLogoPosition);

        auto* footerRow = new QHBoxLayout();
        footerRow->addWidget(new QLabel(QString::fromUtf8("Текст футера:"), w));
        auto* footer = new QLineEdit(reportsPdfSettings.footerText, w);
        footerRow->addWidget(footer, 1);
        l->addLayout(footerRow);

        auto* websiteRow = new QHBoxLayout();
        auto* websiteLabel = new QLabel(QString::fromUtf8("Сайт у футері:"), w);
        websiteLabel->setStyleSheet(QString("color:%1;").arg(cPrimary()));
        websiteRow->addWidget(websiteLabel);
        auto* website = new QLineEdit(UiStateSettingsService::loadValue("settings.reports.pdf.footer_website", QString()).toString(), w);
        website->setObjectName("pdfFooterWebsite");
        website->setMaxLength(512);
        website->setPlaceholderText("https://carservicepointg.com.ua");
        websiteRow->addWidget(website, 1);
        l->addLayout(websiteRow);
        auto* websiteQr = new LabeledToggle(QString::fromUtf8("Додавати QR-код сайту у PDF"), w);
        websiteQr->setChecked(UiStateSettingsService::loadValue("settings.reports.pdf.website_qr", true).toBool());
        l->addWidget(websiteQr);
        auto* qrPreview = new QLabel(w);
        qrPreview->setObjectName("pdfWebsiteQrPreview");
        qrPreview->setFixedSize(100, 100);
        l->addWidget(qrPreview, 0, Qt::AlignLeft);
        auto saveWebsite = [website, websiteQr, qrPreview]() {
            QString address = website->text().trimmed();
            const QUrl parsed = QUrl::fromUserInput(address.contains(":") ? address : "https://" + address);
            if (!address.isEmpty() && (!parsed.isValid() || parsed.host().isEmpty()
                || (parsed.scheme() != "https" && parsed.scheme() != "http"))) {
                QMessageBox::warning(website, QString::fromUtf8("Сайт СТО"), QString::fromUtf8("Вкажіть коректну адресу сайту http:// або https://."));
                return;
            }
            if (!address.isEmpty()) address = parsed.toString(QUrl::FullyEncoded);
            const bool changed = UiStateSettingsService::loadValue("settings.reports.pdf.footer_website", QString()).toString() != address
                || UiStateSettingsService::loadValue("settings.reports.pdf.website_qr", true).toBool() != websiteQr->isChecked();
            UiStateSettingsService::saveValue("settings.reports.pdf.footer_website", address);
            UiStateSettingsService::saveValue("settings.reports.pdf.website_qr", websiteQr->isChecked());
            if (changed) SettingsPageDataSupport::publishSettingsBackupEvent("reports_pdf_website_save",
                "report_settings", "settings.reports.pdf", QString::fromUtf8("Змінено сайт і QR-код PDF"), {"settings"});
            const auto image = PdfWebsiteFooter::qr(address);
            qrPreview->setPixmap(QPixmap::fromImage(image).scaled(100, 100, Qt::KeepAspectRatio, Qt::FastTransformation));
            qrPreview->setVisible(websiteQr->isChecked() && !image.isNull());
        };
        const auto initialQr = PdfWebsiteFooter::qr(PdfWebsiteFooter::url());
        qrPreview->setPixmap(QPixmap::fromImage(initialQr).scaled(100, 100, Qt::KeepAspectRatio, Qt::FastTransformation));
        qrPreview->setVisible(websiteQr->isChecked() && !initialQr.isNull());
        QObject::connect(website, &QLineEdit::editingFinished, w, saveWebsite);
        QObject::connect(websiteQr, &LabeledToggle::toggled, w, [saveWebsite](bool) { saveWebsite(); });

        auto* hint = new QLabel(QString::fromUtf8("Ці параметри використовуються у друці наряду та звітах."), w);
        hint->setWordWrap(true);
        hint->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        l->addWidget(hint);

        GeneralSettingsService::ensurePdfServiceRequisitesDefaults();
        const auto pdfService = GeneralSettingsService::loadPdfServiceRequisites();

        auto* serviceBox = new QGroupBox(QString::fromUtf8("Шапка / реквізити сервісу для PDF"), w);
        serviceBox->setStyleSheet(QStringLiteral(
            "QGroupBox{color:%1;border:1px solid rgba(255,255,255,0.22);border-radius:8px;"
            "margin-top:14px;padding:14px 10px 10px 10px;font-weight:800;}"
            "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 6px;}"
            "QLabel{color:%1;}").arg(cPrimary()));
        auto* serviceGrid = new QGridLayout(serviceBox);
        serviceGrid->setHorizontalSpacing(12);
        serviceGrid->setVerticalSpacing(10);

        auto addServiceLine = [&](const QString& label, const QString& value, int row, int col) {
            auto* edit = new QLineEdit(value, serviceBox);
            edit->setStyleSheet(pdfInputStyle());
            serviceGrid->addWidget(new QLabel(label, serviceBox), row, col * 2);
            serviceGrid->addWidget(edit, row, col * 2 + 1);
            return edit;
        };

        auto* serviceName = addServiceLine(QString::fromUtf8("Назва сервісу"), pdfService.name, 0, 0);
        auto* servicePhone = addServiceLine(QString::fromUtf8("Телефон"), pdfService.phone, 0, 1);
        auto* serviceLegalAddress = addServiceLine(QString::fromUtf8("Юридична адреса"), pdfService.legalAddress, 1, 0);
        auto* serviceActualAddress = addServiceLine(QString::fromUtf8("Фактична адреса"), pdfService.actualAddress, 1, 1);
        auto* serviceEmail = addServiceLine(QStringLiteral("Email"), pdfService.email, 2, 0);
        auto* serviceWebsite = addServiceLine(QString::fromUtf8("Сайт"), pdfService.website, 2, 1);
        auto* serviceEdrpou = addServiceLine(QString::fromUtf8("Код ЄДРПОУ"), pdfService.edrpou, 3, 0);
        auto* serviceBankAccount = addServiceLine(QString::fromUtf8("Розрахунковий рахунок"), pdfService.bankAccount, 3, 1);

        auto* serviceActions = new QHBoxLayout();
        auto* saveService = new QPushButton(QString::fromUtf8("Зберегти PDF-реквізити"), serviceBox);
        auto* restoreService = new QPushButton(QString::fromUtf8("Відновити значення за замовчуванням"), serviceBox);
        saveService->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(true));
        restoreService->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(true));
        saveService->setMinimumHeight(36);
        restoreService->setMinimumHeight(36);
        serviceActions->addWidget(saveService);
        serviceActions->addWidget(restoreService);
        serviceActions->addStretch(1);
        serviceGrid->addLayout(serviceActions, 4, 0, 1, 4);
        l->addWidget(serviceBox);

        auto* customLogoHint = new QLabel(QString::fromUtf8("Коли прапорець вимкнено, логотип автоматично ставиться над лінією підпису майстра на відстані 10 мм. Коли увімкнено — використовується користувацьке положення на листі."), w);
        customLogoHint->setWordWrap(true);
        customLogoHint->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        l->addWidget(customLogoHint);

        l->addStretch(1);

        auto publishPdfSettingsBackupEvent = [](const QString& description) {
            SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("reports_pdf_settings_save"),
                                                                QStringLiteral("report_settings"),
                                                                QStringLiteral("settings.reports.pdf"),
                                                                description,
                                                                {QStringLiteral("settings")});
        };

        auto savePayload = [cbLogo, cbPhone, cbCustomLogoPosition, footer]() {
            bool changed = false;
            GeneralSettingsService::ReportsPdfSettingsPayload payload;
            payload.includeLogo = cbLogo->isChecked();
            payload.includePhone = cbPhone->isChecked();
            payload.useCustomLogoPosition = cbCustomLogoPosition->isChecked();
            payload.footerText = footer->text().trimmed();
            GeneralSettingsService::saveReportsPdfSettings(payload, &changed);
            return changed;
        };

        auto collectServicePayload = [serviceName, serviceLegalAddress, serviceActualAddress, servicePhone,
                                      serviceEmail, serviceWebsite, serviceEdrpou, serviceBankAccount]() {
            GeneralSettingsService::PdfServiceRequisitesPayload payload;
            payload.name = serviceName->text().trimmed();
            payload.legalAddress = serviceLegalAddress->text().trimmed();
            payload.actualAddress = serviceActualAddress->text().trimmed();
            payload.phone = servicePhone->text().trimmed();
            payload.email = serviceEmail->text().trimmed();
            payload.website = serviceWebsite->text().trimmed();
            payload.edrpou = serviceEdrpou->text().trimmed();
            payload.bankAccount = serviceBankAccount->text().trimmed();
            return payload;
        };

        QObject::connect(cbLogo, &LabeledToggle::toggled, w, [cbLogo, savePayload, publishPdfSettingsBackupEvent](bool) {
            if (savePayload()) publishPdfSettingsBackupEvent(cbLogo->text());
        });
        QObject::connect(cbPhone, &LabeledToggle::toggled, w, [cbPhone, savePayload, publishPdfSettingsBackupEvent](bool) {
            if (savePayload()) publishPdfSettingsBackupEvent(cbPhone->text());
        });
        QObject::connect(cbCustomLogoPosition, &LabeledToggle::toggled, w, [cbCustomLogoPosition, savePayload, publishPdfSettingsBackupEvent](bool) {
            if (savePayload()) publishPdfSettingsBackupEvent(cbCustomLogoPosition->text());
        });
        QObject::connect(footer, &QLineEdit::editingFinished, w, [savePayload, publishPdfSettingsBackupEvent]() {
            if (savePayload()) {
                publishPdfSettingsBackupEvent(QString::fromUtf8("Змінено текст футера PDF / друку"));
            }
        });
        QObject::connect(saveService, &QPushButton::clicked, w, [collectServicePayload, publishPdfSettingsBackupEvent]() {
            bool changed = false;
            GeneralSettingsService::savePdfServiceRequisites(collectServicePayload(), &changed);
            if (changed) {
                publishPdfSettingsBackupEvent(QString::fromUtf8("Збережено реквізити сервісу для PDF"));
            }
        });
        QObject::connect(restoreService, &QPushButton::clicked, w, [serviceName, serviceLegalAddress, serviceActualAddress,
                                                                    servicePhone, serviceEmail, serviceWebsite,
                                                                    serviceEdrpou, serviceBankAccount,
                                                                    publishPdfSettingsBackupEvent]() {
            const auto defaults = GeneralSettingsService::defaultPdfServiceRequisites();
            serviceName->setText(defaults.name);
            serviceLegalAddress->setText(defaults.legalAddress);
            serviceActualAddress->setText(defaults.actualAddress);
            servicePhone->setText(defaults.phone);
            serviceEmail->setText(defaults.email);
            serviceWebsite->setText(defaults.website);
            serviceEdrpou->setText(defaults.edrpou);
            serviceBankAccount->setText(defaults.bankAccount);
            bool changed = false;
            GeneralSettingsService::savePdfServiceRequisites(defaults, &changed);
            if (changed) {
                publishPdfSettingsBackupEvent(QString::fromUtf8("Відновлено стандартні реквізити сервісу для PDF"));
            }
        });

        l->setSizeConstraint(QLayout::SetMinimumSize);
        auto* scroll = new QScrollArea(owner);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(w);
        return scroll;
    }
    if (id == QStringLiteral("general.sounds")) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18,18,18,18);
        l->setSpacing(12);

        auto* title = new QLabel(QString::fromUtf8("Звуки та голос"), w);
        title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(cPrimary()));
        l->addWidget(title);

        auto* cb = new LabeledToggle(QString::fromUtf8("Увімкнути звуки кнопок"), w);
        cb->setChecked(UiSettingsService::loadSoundEnabled());
        l->addWidget(cb);

        auto* elitVoice=new LabeledToggle(QString::fromUtf8("Озвучка Еліт"),w);
        elitVoice->setObjectName("elitVoiceEnabled");
        elitVoice->setChecked(ElitSyncVoice::enabled());
        l->addWidget(elitVoice);
        auto* elitPreview=new QPushButton(QString::fromUtf8("Прослухати озвучку Еліт"),w);
        elitPreview->setObjectName("elitVoicePreview");
        elitPreview->setEnabled(elitVoice->isChecked());
        l->addWidget(elitPreview,0,Qt::AlignLeft);
        QObject::connect(elitPreview,&QPushButton::clicked,w,[]{ElitSyncVoice::play(ElitSyncVoice::Outcome::Success);});
        QObject::connect(elitVoice,&LabeledToggle::toggled,w,[w,elitVoice,elitPreview](bool on){
            if(!UiStateSettingsService::saveValue("settings.sounds.elit_voice",on)) {
                const QSignalBlocker blocker(elitVoice);elitVoice->setChecked(!on);
                QMessageBox::warning(w,QString::fromUtf8("Озвучка Еліт"),QString::fromUtf8("Не вдалося зберегти налаштування."));return;
            }
            elitPreview->setEnabled(on);
            if(!on) ElitSyncVoice::stop();
            SettingsPageDataSupport::publishSettingsBackupEvent("sound_settings_save","ui_settings","settings.general.sounds",
                on?QString::fromUtf8("Увімкнено озвучку Еліт"):QString::fromUtf8("Вимкнено озвучку Еліт"),{QStringLiteral("settings")});
        });

        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel(QString::fromUtf8("Гучність:"), w);
        auto* sld = new QSlider(Qt::Horizontal, w);
        sld->setRange(0, 100);
        sld->setValue(UiSettingsService::loadSoundVolume());
        sld->setMaximumWidth(320);

        auto* val = new QLabel(QString::number(sld->value()) + "%", w);
        val->setMinimumWidth(50);

        row->addWidget(lbl);
        row->addWidget(sld);
        row->addWidget(val);
        row->addStretch(1);
        l->addLayout(row);

        auto* test = new QPushButton(QString::fromUtf8("Тест"), w);
        test->setFixedWidth(120);
        l->addWidget(test, 0, Qt::AlignLeft);

        auto* info = new QLabel(QString::fromUtf8("Примітка: повна система звуків буде перенесена окремим модулем (wav/ogg + мікшер)."), w);
        info->setWordWrap(true);
        info->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        l->addWidget(info);

        l->addStretch(1);

        QObject::connect(cb, &LabeledToggle::toggled, w, [cb](bool on) {
            const bool previous = UiSettingsService::loadSoundEnabled();
            UiSettingsService::saveSoundEnabled(on);
            if (previous != on) {
                SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("sound_settings_save"),
                                                                   QStringLiteral("ui_settings"),
                                                                   QStringLiteral("settings.general.sounds"),
                                                                   on
                                                                       ? QString::fromUtf8("Увімкнено звуки кнопок")
                                                                       : QString::fromUtf8("Вимкнено звуки кнопок"),
                                                                   {QStringLiteral("settings")});
            }
        });
        QObject::connect(sld, &QSlider::sliderPressed, w, [sld]() {
            sld->setProperty("backup_prev_value", UiSettingsService::loadSoundVolume());
        });
        QObject::connect(sld, &QSlider::valueChanged, w, [val](int v) {
            val->setText(QString::number(v) + "%");
            UiSettingsService::saveSoundVolume(v);
        });
        QObject::connect(sld, &QSlider::sliderReleased, w, [sld]() {
            const int previous = sld->property("backup_prev_value").toInt();
            const int current = sld->value();
            if (previous != current) {
                SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("sound_settings_save"),
                                                                   QStringLiteral("ui_settings"),
                                                                   QStringLiteral("settings.general.sounds"),
                                                                   QString::fromUtf8("Змінено гучність звуків: %1% → %2%")
                                                                       .arg(previous)
                                                                       .arg(current),
                                                                   {QStringLiteral("settings")});
            }
        });
        QObject::connect(test, &QPushButton::clicked, w, [cb]() {
            if (!cb->isChecked()) return;
            ClickSoundFilter::playGlobalClick();
        });

        return w;
    }

    if (id == QStringLiteral("general.planning_reminders")) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18, 18, 18, 18);
        l->setSpacing(12);

        auto* title = new QLabel(QString::fromUtf8("Нагадування планування"), w);
        title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(cPrimary()));
        l->addWidget(title);

        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel(QString::fromUtf8("Показувати нагадування за:"), w);
        auto* minutes = new QSpinBox(w);
        minutes->setRange(1, 240);
        minutes->setSingleStep(5);
        minutes->setSuffix(QString::fromUtf8(" хв"));
        minutes->setValue(GeneralSettingsService::loadPlanningReminderLeadMinutes());
        minutes->setMinimumWidth(140);
        row->addWidget(lbl);
        row->addWidget(minutes);
        row->addStretch(1);
        l->addLayout(row);

        auto* hint = new QLabel(QString::fromUtf8("Наприклад: 20 хв або 60 хв. Нагадування зʼявляється поверх усіх вікон застосунку і відтворює звук нагадування."), w);
        hint->setWordWrap(true);
        hint->setStyleSheet(QString("color:%1;").arg(cSecondary()));
        l->addWidget(hint);

        l->addStretch(1);

        QObject::connect(minutes, qOverload<int>(&QSpinBox::valueChanged), w, [minutes](int value) {
            bool changed = false;
            GeneralSettingsService::savePlanningReminderLeadMinutes(value, &changed);
            if (changed) {
                SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("planning_reminder_settings_save"),
                                                                   QStringLiteral("planning_settings"),
                                                                   QStringLiteral("settings.planning.reminder_lead_minutes"),
                                                                   QString::fromUtf8("Змінено час спрацювання нагадування планування: %1 хв").arg(minutes->value()),
                                                                   {QStringLiteral("settings")});
            }
        });

        return w;
    }

    if (id == QStringLiteral("general.animations")) {
        auto* w = new QWidget(owner);
        auto* root = new QVBoxLayout(w);
        root->setContentsMargins(16,16,16,16);
        root->setSpacing(12);

        auto mkActionBtn = [w](const QString& text) {
            auto* b = new QPushButton(text, w);
            const bool isBackButton = (text == QString::fromUtf8("← Назад") || text == QString::fromUtf8("Назад"));
            if (isBackButton || text == QString::fromUtf8("Скасувати") || text == QString::fromUtf8("Зберегти")) {
                b->setFixedSize(120, 40);
            } else {
                b->setFixedSize(216, 52);
            }
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QString::fromUtf8(
                "QPushButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(214,224,236,0.82), stop:0.14 rgba(154,168,184,0.92), stop:0.42 rgba(95,109,127,0.98), stop:0.48 rgba(40,52,68,1.0), stop:0.49 rgba(8,17,30,1.0), stop:1 rgba(4,12,24,1.0));"
                "border:1px solid rgba(255,255,255,0.90);border-top:1px solid rgba(255,255,255,0.98);border-radius:18px;"
                "padding:0 24px;color:#f8fbff;font-size:11pt;font-weight:800;}"
                "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(228,236,246,0.90), stop:0.14 rgba(170,184,198,0.96), stop:0.42 rgba(108,123,141,1.0), stop:0.48 rgba(48,61,78,1.0), stop:0.49 rgba(10,21,36,1.0), stop:1 rgba(6,14,28,1.0));border:1px solid rgba(255,255,255,0.98);border-top:1px solid rgba(255,255,255,1.0);}"
                "QPushButton:pressed{padding-top:1px;background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(40,52,68,1.0), stop:0.40 rgba(18,29,43,1.0), stop:1 rgba(6,14,28,1.0));border:1px solid rgba(255,255,255,0.74);}"
                "QPushButton:disabled{background:rgba(112,122,136,0.46);color:#dbe4ef;border:1px solid rgba(255,255,255,0.28);}"));
            return b;
        };

        auto* top = new QHBoxLayout();
        top->setSpacing(12);
        auto* btnBack = mkActionBtn(QString::fromUtf8("← Назад"));
        auto* title = new QLabel(QString::fromUtf8("Ефекти / Анімації"), w);
        title->setStyleSheet(QString("font-size:16pt; font-weight:800; color:%1;").arg(cPrimary()));
        auto* btnCancel = mkActionBtn(QString::fromUtf8("Скасувати"));
        auto* btnSave = mkActionBtn(QString::fromUtf8("Зберегти"));
        top->addWidget(btnBack);
        top->addWidget(title, 1);
        top->addWidget(btnCancel);
        top->addWidget(btnSave);
        root->addLayout(top);

        auto* stage = new QFrame(w);
        stage->setStyleSheet(QString::fromUtf8(
            "QFrame{background:rgba(255,255,255,26);border:1px solid rgba(255,255,255,0.18);border-radius:18px;}"));
        auto* stageL = new QVBoxLayout(stage);
        stageL->setContentsMargins(14,14,14,14);
        stageL->setSpacing(12);

        auto* chip = new QLabel(QString::fromUtf8("Ефекти / Анімації"), stage);
        chip->setStyleSheet(QString::fromUtf8(
            "background:rgba(17,23,30,0.78);border:1px solid rgba(225,235,246,0.24);border-radius:12px;padding:4px 14px;color:#f8fbff;font-weight:600;"));
        stageL->addWidget(chip, 0, Qt::AlignLeft);

        auto* panel = new QFrame(stage);
        panel->setStyleSheet(QString::fromUtf8(
            "QFrame{background:rgba(255,255,255,0.08);border:1px solid rgba(255,255,255,56);border-radius:22px;}"
            "QLabel{color:%1;}"
            "QComboBox{background:#ffffff;border:1px solid rgba(22,28,35,0.22);border-radius:18px;padding:10px 16px;"
            "color:#111111;font-size:11pt;min-height:32px;}"
            "QSlider::groove:horizontal{height:6px;background:rgba(255,255,255,0.35);border-radius:3px;}"
            "QSlider::sub-page:horizontal{background:#49b8f2;border-radius:3px;}"
            "QSlider::handle:horizontal{background:#384255;border:2px solid #49b8f2;width:16px;height:16px;margin:-6px 0;border-radius:8px;}").arg(cPrimary()));
        auto* panelL = new QVBoxLayout(panel);
        panelL->setContentsMargins(22,20,22,20);
        panelL->setSpacing(12);

        auto* row1 = new QHBoxLayout();
        auto* levelLbl = new QLabel(QString::fromUtf8("Рівень ефектності:"), panel);
        levelLbl->setMinimumWidth(170);
        auto* level = new QComboBox(panel);
        level->addItem(QString::fromUtf8("Мінімальний"), "minimal");
        level->addItem(QString::fromUtf8("Стандартний"), "normal");
        level->addItem(QString::fromUtf8("Ultra"), "ultra");
        {
            const QString cur = UiSettingsService::loadEffectsSettings().effectsLevel;
            const int idx = level->findData(cur);
            if (idx >= 0) level->setCurrentIndex(idx);
        }
        row1->addWidget(levelLbl);
        row1->addWidget(level, 1);
        panelL->addLayout(row1);

        auto* row2 = new QHBoxLayout();
        auto* overlayLbl = new QLabel(QString::fromUtf8("Затемнення фону (overlay):"), panel);
        overlayLbl->setMinimumWidth(230);
        auto* overlay = new QSlider(Qt::Horizontal, panel);
        overlay->setRange(0, 40);
        overlay->setValue(UiSettingsService::loadEffectsSettings().overlayPercent);
        auto* overlayVal = new QLabel(QString::number(overlay->value()) + "%", panel);
        overlayVal->setMinimumWidth(48);
        row2->addWidget(overlayLbl);
        row2->addWidget(overlay, 1);
        row2->addWidget(overlayVal);
        panelL->addLayout(row2);

        auto* hint = new QLabel(QString::fromUtf8("Порада: на світлій темі overlay краще тримати 0–15%."), panel);
        hint->setWordWrap(true);
        hint->setStyleSheet(QString("color:%1; font-size:10.5pt;").arg(cSecondary()));
        panelL->addWidget(hint);

        stageL->addWidget(panel);
        root->addWidget(stage);
        root->addStretch(1);

        auto saveState = [level, overlay]() {
            UiSettingsService::EffectsSettingsPayload payload;
            payload.effectsLevel = level->currentData().toString();
            payload.overlayPercent = overlay->value();
            UiSettingsService::saveEffectsSettings(payload);
        };
        auto loadState = [level, overlay, overlayVal]() {
            const auto effects = UiSettingsService::loadEffectsSettings();
            const int idx = level->findData(effects.effectsLevel);
            if (idx >= 0) level->setCurrentIndex(idx);
            overlay->setValue(effects.overlayPercent);
            overlayVal->setText(QString::number(overlay->value()) + "%");
        };

        QObject::connect(btnBack, &QPushButton::clicked, w, [owner]() {
            if (!owner) return;
            if (auto* list = owner->findChild<QListWidget*>()) {
                list->setCurrentRow(-1);
            }
        });
        QObject::connect(btnCancel, &QPushButton::clicked, w, loadState);
        QObject::connect(btnSave, &QPushButton::clicked, w, [saveState, level, overlay]() {
            const auto previousEffects = UiSettingsService::loadEffectsSettings();
            const QString previousLevel = previousEffects.effectsLevel;
            const int previousOverlay = previousEffects.overlayPercent;
            saveState();
            if (previousLevel != level->currentData().toString() || previousOverlay != overlay->value()) {
                SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("ui_effects_save"),
                                                                   QStringLiteral("ui_settings"),
                                                                   QStringLiteral("settings.ui.effects"),
                                                                   QString::fromUtf8("Збережено налаштування ефектів / анімацій"),
                                                                   {QStringLiteral("settings")});
            }
        });
        QObject::connect(overlay, &QSlider::valueChanged, w, [overlayVal](int v) {
            overlayVal->setText(QString::number(v) + "%");
        });

        return w;
    }

    if (id == QStringLiteral("general.branding")) {
        return brandingBuilder ? brandingBuilder() : nullptr;
    }
    return nullptr;
}
