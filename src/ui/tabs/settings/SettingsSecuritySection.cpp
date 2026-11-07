#include "ui/tabs/settings/SettingsSecuritySection.h"
#include "ui/tabs/settings/PasswordManager.h"
#include "ui/tabs/settings/PasswordDialog.h"
#include "ui/tabs/settings/ToggleSwitch.h"

#include "ui/theme/ThemeManager.h"
#include "ui/tabs/settings/SettingsDetailUiSupport.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

bool secIsDarkUi()
{
    return ThemeManager::currentTheme() == QStringLiteral("graphite");
}

QString secPrimary()
{
    return secIsDarkUi() ? QStringLiteral("#f7fbff") : QStringLiteral("#111111");
}

QString secSecondary()
{
    return secIsDarkUi() ? QStringLiteral("#c7d0db") : QStringLiteral("#4b5563");
}

// Creates an iOS-style toggle switch (custom-painted with sliding white knob)
ToggleSwitch* makeToggle(bool initialState, QWidget* parent)
{
    auto* sw = new ToggleSwitch(parent);
    sw->setChecked(initialState);
    return sw;
}

// Creates a row: [label + description] | [spacer] | [toggle]
QWidget* makeToggleRow(const QString& label,
                       const QString& description,
                       bool initialState,
                       QWidget* parent,
                       ToggleSwitch** outToggle = nullptr)
{
    auto* row = new QWidget(parent);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);

    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(2);
    textCol->setContentsMargins(0, 0, 0, 0);

    auto* lbl = new QLabel(label, row);
    lbl->setStyleSheet(QString("font-size:11pt; font-weight:600; color:%1;").arg(secPrimary()));
    textCol->addWidget(lbl);

    if (!description.isEmpty()) {
        auto* desc = new QLabel(description, row);
        desc->setWordWrap(true);
        desc->setStyleSheet(QString("font-size:9pt; color:%1;").arg(secSecondary()));
        textCol->addWidget(desc);
    }

    h->addLayout(textCol, 1);

    auto* toggle = makeToggle(initialState, row);
    h->addWidget(toggle, 0, Qt::AlignVCenter);

    if (outToggle) *outToggle = toggle;
    return row;
}

// Horizontal separator line
QFrame* makeSeparator(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setFixedHeight(1);
    line->setStyleSheet(QString("background: %1; border: none;")
                            .arg(secIsDarkUi() ? QStringLiteral("rgba(255,255,255,0.10)") : QStringLiteral("#e5e7eb")));
    return line;
}

} // namespace


QWidget* SettingsSecuritySection::build(const QString& id, QWidget* owner)
{
    // ── Захист доступу ───────────────────────────────────────────────────────
    if (id == QStringLiteral("sec.access")) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18, 18, 18, 18);
        l->setSpacing(0);

        auto* title = new QLabel(QString::fromUtf8("Захист доступу"), w);
        title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(secPrimary()));
        l->addWidget(title);

        auto* subtitle = new QLabel(QString::fromUtf8("Налаштування автентифікації та обмеження доступу до застосунку."), w);
        subtitle->setWordWrap(true);
        subtitle->setStyleSheet(QString("color:%1; margin-top:4px; margin-bottom:16px;").arg(secSecondary()));
        l->addWidget(subtitle);

        l->addWidget(makeSeparator(w));
        l->addSpacing(14);

        // ── "Пароль при запуску" toggle — wired to PasswordManager ──────────
        ToggleSwitch* pwToggle = nullptr;
        auto* pwRow = makeToggleRow(
            QString::fromUtf8("Пароль при запуску"),
            QString::fromUtf8("Вимагати пароль щоразу при відкритті застосунку."),
            PasswordManager::isPasswordEnabled(), w, &pwToggle);
        l->addWidget(pwRow);

        // Status label shown under the toggle (e.g. "Пароль встановлено")
        auto* pwStatus = new QLabel(w);
        pwStatus->setWordWrap(true);
        pwStatus->setStyleSheet(QString("font-size:9pt; color:%1; margin-left:2px;").arg(secSecondary()));
        pwStatus->setText(PasswordManager::hasPassword()
            ? QString::fromUtf8("✓ Пароль встановлено")
            : QString::fromUtf8("Пароль ще не встановлено"));
        l->addSpacing(6);
        l->addWidget(pwStatus);

        // "Змінити пароль" button — visible when password protection is on
        auto* changePwBtn = new QPushButton(QString::fromUtf8("Змінити пароль"), w);
        changePwBtn->setFixedHeight(38);
        changePwBtn->setCursor(Qt::PointingHandCursor);
        changePwBtn->setStyleSheet(SettingsDetailUiSupport::standardButtonStyle(false));
        changePwBtn->setVisible(PasswordManager::isPasswordEnabled());
        l->addSpacing(10);
        l->addWidget(changePwBtn, 0, Qt::AlignLeft);

        // Toggle logic: enable → open setup dialog; disable → just turn off
        QObject::connect(pwToggle, &QAbstractButton::toggled, w,
                         [w, pwToggle, pwStatus, changePwBtn](bool on) {
            if (on) {
                auto* dlg = new PasswordDialog(w, /*showSetup=*/true);
                if (dlg->exec() == QDialog::Accepted) {
                    PasswordManager::setPasswordEnabled(true);
                    pwStatus->setText(QString::fromUtf8("✓ Пароль встановлено"));
                    changePwBtn->setVisible(true);
                } else {
                    // User cancelled setup — revert toggle silently
                    QSignalBlocker blocker(pwToggle);
                    pwToggle->setChecked(false);
                }
                dlg->deleteLater();
            } else {
                PasswordManager::setPasswordEnabled(false);
                pwStatus->setText(QString::fromUtf8("Пароль ще не встановлено"));
                changePwBtn->setVisible(false);
            }
        });

        // "Змінити пароль" opens setup dialog
        QObject::connect(changePwBtn, &QPushButton::clicked, w,
                         [w, pwStatus]() {
            auto* dlg = new PasswordDialog(w, /*showSetup=*/true);
            if (dlg->exec() == QDialog::Accepted) {
                pwStatus->setText(QString::fromUtf8("✓ Пароль оновлено"));
            }
            dlg->deleteLater();
        });

        l->addSpacing(14);
        l->addWidget(makeSeparator(w));
        l->addSpacing(14);

        // ── "Автоблокування" toggle + timeout selector ─────────────────────
        ToggleSwitch* lockToggle = nullptr;
        auto* lockRow = makeToggleRow(
            QString::fromUtf8("Автоблокування"),
            QString::fromUtf8("Автоматично блокувати екран після бездіяльності."),
            PasswordManager::isAutoLockEnabled(), w, &lockToggle);
        l->addWidget(lockRow);

        // Timeout selector (visible only when toggle is on)
        auto* timeoutRow = new QWidget(w);
        auto* timeoutLayout = new QHBoxLayout(timeoutRow);
        timeoutLayout->setContentsMargins(0, 8, 0, 0);
        timeoutLayout->setSpacing(10);

        auto* timeoutLbl = new QLabel(QString::fromUtf8("Час бездіяльності:"), timeoutRow);
        timeoutLbl->setStyleSheet(QString("font-size:10pt; color:%1;").arg(secSecondary()));
        timeoutLayout->addWidget(timeoutLbl);

        auto* timeoutCombo = new QComboBox(timeoutRow);
        timeoutCombo->setCursor(Qt::PointingHandCursor);
        timeoutCombo->setFixedWidth(160);
        timeoutCombo->addItem(QString::fromUtf8("1 хвилина"),    1);
        timeoutCombo->addItem(QString::fromUtf8("5 хвилин"),     5);
        timeoutCombo->addItem(QString::fromUtf8("10 хвилин"),    10);
        timeoutCombo->addItem(QString::fromUtf8("15 хвилин"),    15);
        timeoutCombo->addItem(QString::fromUtf8("30 хвилин"),    30);
        timeoutCombo->addItem(QString::fromUtf8("1 година"),     60);
        timeoutCombo->addItem(QString::fromUtf8("2 години"),     120);
        timeoutCombo->setStyleSheet(QStringLiteral(
            "QComboBox{background:#ffffff;border:1px solid rgba(22,28,35,0.28);"
            "border-radius:8px;color:#111111;padding:6px 10px;font-size:10pt;"
            "selection-background-color:#b8d6ff;selection-color:#111111;}"
            "QComboBox::drop-down{border:none;width:24px;background:#eef2f7;}"
            "QComboBox QAbstractItemView{background:#ffffff;color:#111111;"
            "selection-background-color:#b8d6ff;selection-color:#111111;border:1px solid rgba(22,28,35,0.28);}"));

        // Restore current value
        const int currentMin = PasswordManager::autoLockMinutes();
        for (int i = 0; i < timeoutCombo->count(); ++i) {
            if (timeoutCombo->itemData(i).toInt() == currentMin) {
                timeoutCombo->setCurrentIndex(i);
                break;
            }
        }

        timeoutLayout->addWidget(timeoutCombo);
        timeoutLayout->addStretch(1);

        timeoutRow->setVisible(PasswordManager::isAutoLockEnabled());
        l->addWidget(timeoutRow);

        QObject::connect(lockToggle, &QAbstractButton::toggled, w,
                         [timeoutRow](bool on) {
            PasswordManager::setAutoLockEnabled(on);
            timeoutRow->setVisible(on);
        });

        QObject::connect(timeoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), w,
                         [timeoutCombo](int idx) {
            const int minutes = timeoutCombo->itemData(idx).toInt();
            PasswordManager::setAutoLockMinutes(minutes);
        });

        l->addSpacing(14);
        l->addWidget(makeSeparator(w));
        l->addSpacing(14);

        l->addWidget(makeToggleRow(
            QString::fromUtf8("Обмежений режим для персоналу"),
            QString::fromUtf8("Ховати фінансові дані від майстрів та операторів."),
            true, w));
        l->addSpacing(14);
        l->addWidget(makeSeparator(w));
        l->addSpacing(14);

        // ── Lock-screen animation selector (Windows screensavers) ────────────
        auto* lockStyleHeader = new QLabel(QString::fromUtf8("Екран блокування"), w);
        lockStyleHeader->setStyleSheet(QString("font-size:11pt; font-weight:600; color:%1;").arg(secPrimary()));
        l->addWidget(lockStyleHeader);

        auto* lockStyleHint = new QLabel(
            QString::fromUtf8("Анімація, що відтворюється на екрані блокування — як скринсейвер у Windows."), w);
        lockStyleHint->setWordWrap(true);
        lockStyleHint->setStyleSheet(QString("font-size:9pt; color:%1; margin-bottom:6px;").arg(secSecondary()));
        l->addWidget(lockStyleHint);

        auto* lockStyleRow = new QWidget(w);
        auto* lockStyleLayout = new QHBoxLayout(lockStyleRow);
        lockStyleLayout->setContentsMargins(0, 4, 0, 0);
        lockStyleLayout->setSpacing(10);

        auto* lockStyleLbl = new QLabel(QString::fromUtf8("Заставка:"), lockStyleRow);
        lockStyleLbl->setStyleSheet(QString("font-size:10pt; color:%1;").arg(secSecondary()));
        lockStyleLayout->addWidget(lockStyleLbl);

        auto* lockStyleCombo = new QComboBox(lockStyleRow);
        lockStyleCombo->setCursor(Qt::PointingHandCursor);
        lockStyleCombo->setFixedWidth(240);
        const auto options = PasswordManager::availableLockScreens();
        for (const auto& opt : options) {
            lockStyleCombo->addItem(opt.displayName, opt.id);
        }
        lockStyleCombo->setStyleSheet(QStringLiteral(
            "QComboBox{background:#ffffff;border:1px solid rgba(22,28,35,0.28);"
            "border-radius:8px;color:#111111;padding:6px 10px;font-size:10pt;"
            "selection-background-color:#b8d6ff;selection-color:#111111;}"
            "QComboBox::drop-down{border:none;width:24px;background:#eef2f7;}"
            "QComboBox QAbstractItemView{background:#ffffff;color:#111111;"
            "selection-background-color:#b8d6ff;selection-color:#111111;border:1px solid rgba(22,28,35,0.28);}"));

        // Restore current value (defaults to "(немає)" at index 0 if unmatched)
        const QString currentStyle = PasswordManager::lockScreenStyle();
        int matchedIdx = 0;
        for (int i = 0; i < lockStyleCombo->count(); ++i) {
            if (lockStyleCombo->itemData(i).toString() == currentStyle) {
                matchedIdx = i;
                break;
            }
        }
        lockStyleCombo->setCurrentIndex(matchedIdx);

        lockStyleLayout->addWidget(lockStyleCombo);
        lockStyleLayout->addStretch(1);
        l->addWidget(lockStyleRow);

        QObject::connect(lockStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), w,
                         [lockStyleCombo](int idx) {
            const QString id = lockStyleCombo->itemData(idx).toString();
            PasswordManager::setLockScreenStyle(id);
        });

        l->addSpacing(14);
        l->addWidget(makeSeparator(w));

        l->addStretch(1);
        return w;
    }

    // ── Безпека даних ────────────────────────────────────────────────────────
    if (id == QStringLiteral("sec.data")) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18, 18, 18, 18);
        l->setSpacing(0);

        auto* title = new QLabel(QString::fromUtf8("Безпека даних"), w);
        title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(secPrimary()));
        l->addWidget(title);

        auto* subtitle = new QLabel(QString::fromUtf8("Захист бази даних та чутливої інформації застосунку."), w);
        subtitle->setWordWrap(true);
        subtitle->setStyleSheet(QString("color:%1; margin-top:4px; margin-bottom:16px;").arg(secSecondary()));
        l->addWidget(subtitle);

        l->addWidget(makeSeparator(w));
        l->addSpacing(14);

        l->addWidget(makeToggleRow(
            QString::fromUtf8("Шифрування бази даних"),
            QString::fromUtf8("Зберігати дані у зашифрованому вигляді (рекомендовано)."),
            true, w));
        l->addSpacing(14);
        l->addWidget(makeSeparator(w));
        l->addSpacing(14);

        l->addWidget(makeToggleRow(
            QString::fromUtf8("Захищені резервні копії"),
            QString::fromUtf8("Шифрувати резервні копії перед збереженням."),
            true, w));
        l->addSpacing(14);
        l->addWidget(makeSeparator(w));
        l->addSpacing(14);

        l->addWidget(makeToggleRow(
            QString::fromUtf8("Журнал подій безпеки"),
            QString::fromUtf8("Вести аудит-журнал змін і доступу до чутливих даних."),
            false, w));
        l->addSpacing(14);
        l->addWidget(makeSeparator(w));
        l->addSpacing(14);

        l->addWidget(makeToggleRow(
            QString::fromUtf8("Захист від копіювання даних"),
            QString::fromUtf8("Забороняти копіювання вмісту через контекстне меню."),
            false, w));
        l->addSpacing(14);
        l->addWidget(makeSeparator(w));

        l->addStretch(1);
        return w;
    }

    // ── Параметри шифрування ─────────────────────────────────────────────────
    if (id == QStringLiteral("sec.encrypt_params")) {
        auto* w = new QWidget(owner);
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(18, 18, 18, 18);
        l->setSpacing(0);

        auto* title = new QLabel(QString::fromUtf8("Параметри шифрування"), w);
        title->setStyleSheet(QString("font-size:14pt; font-weight:800; color:%1;").arg(secPrimary()));
        l->addWidget(title);

        auto* subtitle = new QLabel(QString::fromUtf8("Технічні параметри алгоритмів шифрування даних застосунку."), w);
        subtitle->setWordWrap(true);
        subtitle->setStyleSheet(QString("color:%1; margin-top:4px; margin-bottom:16px;").arg(secSecondary()));
        l->addWidget(subtitle);

        l->addWidget(makeSeparator(w));
        l->addSpacing(14);

        l->addWidget(makeToggleRow(
            QString::fromUtf8("AES-256 шифрування"),
            QString::fromUtf8("Використовувати AES-256-GCM для шифрування сховища."),
            true, w));
        l->addSpacing(14);
        l->addWidget(makeSeparator(w));
        l->addSpacing(14);

        l->addWidget(makeToggleRow(
            QString::fromUtf8("Апаратне прискорення"),
            QString::fromUtf8("Використовувати AES-NI процесора для прискорення шифрування."),
            true, w));
        l->addSpacing(14);
        l->addWidget(makeSeparator(w));
        l->addSpacing(14);

        l->addWidget(makeToggleRow(
            QString::fromUtf8("Перевірка цілісності при старті"),
            QString::fromUtf8("Перевіряти контрольну суму БД під час запуску застосунку."),
            true, w));
        l->addSpacing(14);
        l->addWidget(makeSeparator(w));

        l->addStretch(1);
        return w;
    }

    return nullptr;
}
