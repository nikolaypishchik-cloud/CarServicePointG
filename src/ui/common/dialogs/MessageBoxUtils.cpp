#include "ui/common/dialogs/MessageBoxUtils.h"
#include "ui/common/dialogs/MessageBoxSoundSupport.h"
#include "ui/common/dialogs/UkrainianDialogText.h"

#include <QAbstractButton>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

namespace {

bool shouldPlayAlertFor(QMessageBox* box)
{
    if (!box) return false;

    switch (box->icon()) {
    case QMessageBox::Warning:
    case QMessageBox::Critical:
        return true;
    default:
        break;
    }

    const QString title = box->windowTitle().trimmed().toLower();
    if (title.contains(QString::fromUtf8("попередження")) || title.contains(QString::fromUtf8("помилка"))) {
        return true;
    }
    return false;
}

bool shouldPlayCloseFor(QMessageBox* box)
{
    return shouldPlayAlertFor(box);
}

bool shouldPlayConfirmationShowFor(QMessageBox* box)
{
    if (!box) return false;

    const QString title = box->windowTitle().trimmed().toLower();
    if (title.contains(QString::fromUtf8("підтвердження")) || title.contains(QStringLiteral("confirmation"))) {
        return true;
    }

    const QFlags<QMessageBox::StandardButton> buttons = box->standardButtons();
    if (box->icon() == QMessageBox::Question && buttons.testFlag(QMessageBox::Yes) && buttons.testFlag(QMessageBox::No)) {
        return true;
    }

    return false;
}

bool shouldPlayConfirmationYesFor(QMessageBox* box)
{
    return shouldPlayConfirmationShowFor(box);
}


bool shouldPlayConfirmationNoFor(QMessageBox* box)
{
    return shouldPlayConfirmationShowFor(box);
}


bool hasSettingsPageAncestor(const QObject* object)
{
    const QObject* current = object;
    while (current) {
        const QMetaObject* mo = current->metaObject();
        if (mo && QString::fromLatin1(mo->className()) == QStringLiteral("SettingsPage")) {
            return true;
        }
        current = current->parent();
    }
    return false;
}

bool shouldPlaySettingsActionFor(QMessageBox* box)
{
    if (!box) return false;
    if (shouldPlayConfirmationShowFor(box)) return false;
    if (!hasSettingsPageAncestor(box)) return false;
    return !box->buttons().isEmpty();
}

struct PopupMetrics
{
    int textWidth = 340;
    int totalWidth = 430;
    int totalHeight = 150;
    int buttonWidth = 118;
    int iconSize = 22;
};

QString cleaned(const QString& text)
{
    QString s = text;
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    s.replace(QStringLiteral("\r"), QStringLiteral("\n"));
    return s.trimmed();
}

int wrappedHeightFor(const QString& text, const QFont& font, int width)
{
    if (text.trimmed().isEmpty()) return 0;
    const QFontMetrics fm(font);
    return fm.boundingRect(QRect(0, 0, width, 5000),
                           Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                           text).height();
}

int longestLineWidth(const QString& text, const QFont& font)
{
    const QFontMetrics fm(font);
    int best = 0;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) best = qMax(best, fm.horizontalAdvance(line));
    return best;
}

int chooseTextWidth(const QString& title,
                    const QString& mainText,
                    const QString& infoText,
                    const QFont& titleFont,
                    const QFont& mainFont,
                    const QFont& infoFont)
{
    const QFontMetrics titleFm(titleFont);
    const int titleNeed = qBound(260, titleFm.horizontalAdvance(title) + 56, 420);
    const int lineNeed = qBound(260,
                                qMax(longestLineWidth(mainText, mainFont), longestLineWidth(infoText, infoFont)) + 14,
                                420);

    const QList<int> candidates{280, 300, 320, 340, 360, 380, 400, 420};
    for (const int width : candidates) {
        const int mainHeight = wrappedHeightFor(mainText, mainFont, width);
        const int infoHeight = wrappedHeightFor(infoText, infoFont, width);
        const bool compactMain = mainHeight <= 84;
        const bool compactInfo = infoHeight <= 132;
        if (width >= titleNeed && width >= lineNeed && compactMain && compactInfo) return width;
    }
    return qMax(titleNeed, lineNeed);
}

PopupMetrics computeMetrics(const QString& title,
                            const QString& mainText,
                            const QString& infoText,
                            const QFont& titleFont,
                            const QFont& mainFont,
                            const QFont& infoFont,
                            bool hasIcon,
                            int buttonCount)
{
    PopupMetrics m;
    m.buttonWidth = (buttonCount <= 1) ? 118 : 112;
    m.iconSize = hasIcon ? 22 : 0;
    m.textWidth = chooseTextWidth(title, mainText, infoText, titleFont, mainFont, infoFont);

    const int mainHeight = mainText.trimmed().isEmpty() ? 0 : qMax(24, wrappedHeightFor(mainText, mainFont, m.textWidth));
    const int infoHeight = infoText.trimmed().isEmpty() ? 0 : qMax(20, wrappedHeightFor(infoText, infoFont, m.textWidth));
    const int textHeight = mainHeight + ((mainHeight > 0 && infoHeight > 0) ? 6 : 0) + infoHeight;
    const int contentHeight = qMax(m.iconSize, qMax(24, textHeight));

    const int outerX = 14 * 2;
    const int iconGap = hasIcon ? 10 : 0;
    const int buttonRowHeight = 34;
    const int buttonRowWidth = qBound(1, buttonCount, 3) * m.buttonWidth + qMax(0, buttonCount - 1) * 8;

    m.totalWidth = outerX + m.iconSize + iconGap + m.textWidth;
    m.totalWidth = qBound(390, m.totalWidth, (buttonCount <= 1) ? 520 : 580);
    if (buttonCount <= 1) m.totalWidth = qMax(m.totalWidth, 410);
    else m.totalWidth = qMax(m.totalWidth, 40 + buttonRowWidth);

    m.totalHeight = 12 + contentHeight + 10 + buttonRowHeight + 12;
    m.totalHeight = qBound(132, m.totalHeight, 220);
    return m;
}

QString buildStyleSheet(const QString& textColor)
{
    return QStringLiteral(
        "QMessageBox {"
        " background:#202930;"
        " color:%1;"
        " border:1px solid #69747d;"
        " }"
        "QMessageBox QLabel {"
        " background:transparent;"
        " color:%1;"
        " font-size:10.6pt;"
        " font-weight:600;"
        " border:none;"
        " }"
        "QMessageBox QDialogButtonBox {"
        " background:transparent;"
        " border:none;"
        " }"
        "QMessageBox QPushButton {"
        " min-width:112px;"
        " min-height:34px;"
        " padding:0 18px;"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 #5b6672, stop:0.12 #414c58, stop:1 #252d35);"
        " color:%1;"
        " border:1px solid #84919d;"
        " }"
        "QMessageBox QPushButton:hover {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 #5b6672, stop:0.12 #414c58, stop:1 #252d35);"
        " border:1px solid #dce5ee;"
        " }"
        "QMessageBox QPushButton:pressed {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 #414c58, stop:1 #252d35);"
        " border:1px solid #dce5ee;"
        " }"
    ).arg(textColor);
}

QString buildButtonStyleSheet(const QString& textColor)
{
    return QStringLiteral(
        "QPushButton {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 #5b6672, stop:0.12 #414c58, stop:1 #252d35);"
        " color:%1;"
        " border:1px solid #84919d;"
        " border-radius:0px;"
        " padding:0 18px;"
        " }"
        "QPushButton:hover {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 #5b6672, stop:0.12 #414c58, stop:1 #252d35);"
        " border:1px solid #dce5ee;"
        " }"
        "QPushButton:pressed {"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "   stop:0 #414c58, stop:1 #252d35);"
        " border:1px solid #dce5ee;"
        " }"
        "QPushButton:focus { outline:none; }"
    ).arg(textColor);
}

void hideOriginalMessageBoxChildren(QMessageBox* box)
{
    const auto widgets = box->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* widget : widgets) {
        if (!widget) continue;
        if (widget->objectName().startsWith(QStringLiteral("standardPopup"))) continue;
        if (qobject_cast<QDialogButtonBox*>(widget)) continue;
        widget->hide();
        widget->setMaximumSize(0, 0);
    }

    const auto labels = box->findChildren<QLabel*>();
    for (auto* label : labels) {
        if (!label) continue;
        if (label->objectName().startsWith(QStringLiteral("standardPopup"))) continue;
        label->hide();
        label->setMaximumSize(0, 0);
    }
}

void clearLayout(QLayout* layout, QDialogButtonBox* keepButtonBox)
{
    if (!layout) return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QLayout* childLayout = item->layout()) {
            clearLayout(childLayout, keepButtonBox);
            delete childLayout;
        } else if (QWidget* childWidget = item->widget()) {
            if (childWidget != keepButtonBox) {
                childWidget->hide();
                childWidget->setParent(nullptr);
                childWidget->deleteLater();
            }
        }
        delete item;
    }
}

QLabel* makeTextLabel(QWidget* parent,
                      const char* objectName,
                      const QString& text,
                      const QFont& font,
                      int width)
{
    if (text.trimmed().isEmpty()) return nullptr;
    auto* label = new QLabel(parent);
    label->setObjectName(QLatin1String(objectName));
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    label->setFont(font);
    label->setText(text);
    label->setFixedWidth(width);
    label->adjustSize();
    label->setMinimumHeight(label->sizeHint().height());
    label->show();
    return label;
}

void removeOldStandardChildren(QMessageBox* box, QDialogButtonBox* buttonBox)
{
    const auto widgets = box->findChildren<QWidget*>();
    for (auto* widget : widgets) {
        if (!widget) continue;
        if (widget == buttonBox) continue;
        if (!widget->objectName().startsWith(QStringLiteral("standardPopup"))) continue;
        widget->hide();
        widget->setParent(nullptr);
        widget->deleteLater();
    }
}

void rebuildMessageBoxLayout(QMessageBox* box,
                             const QString& textColor,
                             const PopupMetrics& metrics,
                             const QString& mainText,
                             const QString& infoText,
                             const QPixmap& iconPixmap,
                             QDialogButtonBox* buttonBox)
{
    if (!box || !buttonBox) return;

    hideOriginalMessageBoxChildren(box);
    removeOldStandardChildren(box, buttonBox);

    if (QLayout* oldLayout = box->layout()) {
        clearLayout(oldLayout, buttonBox);
        delete oldLayout;
    }

    auto* root = new QVBoxLayout(box);
    root->setObjectName(QStringLiteral("standardPopupRoot"));
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    auto* body = new QWidget(box);
    body->setObjectName(QStringLiteral("standardPopupBody"));
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(iconPixmap.isNull() ? 0 : 12);

    if (!iconPixmap.isNull()) {
        auto* icon = new QLabel(body);
        icon->setObjectName(QStringLiteral("standardPopupIcon"));
        icon->setFixedSize(metrics.iconSize, metrics.iconSize);
        icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        icon->setPixmap(iconPixmap.scaled(metrics.iconSize, metrics.iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        bodyLayout->addWidget(icon, 0, Qt::AlignTop);
    }

    auto* textWrap = new QWidget(body);
    textWrap->setObjectName(QStringLiteral("standardPopupTextWrap"));
    textWrap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    textWrap->setFixedWidth(metrics.textWidth);
    auto* textLayout = new QVBoxLayout(textWrap);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(infoText.isEmpty() ? 0 : 4);

    QFont mainFont = box->font();
    mainFont.setBold(true);
    mainFont.setPointSizeF(10.8);

    QFont infoFont = box->font();
    infoFont.setBold(true);
    infoFont.setPointSizeF(10.2);

    if (QLabel* mainLabel = makeTextLabel(textWrap, "standardPopupMainText", mainText, mainFont, metrics.textWidth)) {
        mainLabel->setStyleSheet(QStringLiteral("background:transparent;color:%1;border:none;").arg(textColor));
        textLayout->addWidget(mainLabel, 0, Qt::AlignLeft | Qt::AlignTop);
    }

    if (QLabel* infoLabel = makeTextLabel(textWrap, "standardPopupInfoText", infoText, infoFont, metrics.textWidth)) {
        infoLabel->setStyleSheet(QStringLiteral("background:transparent;color:%1;border:none;").arg(textColor));
        textLayout->addWidget(infoLabel, 0, Qt::AlignLeft | Qt::AlignTop);
    }

    bodyLayout->addWidget(textWrap, 1, Qt::AlignLeft | Qt::AlignTop);
    root->addWidget(body, 0, Qt::AlignLeft | Qt::AlignTop);

    buttonBox->setParent(box);
    buttonBox->show();
    buttonBox->setCenterButtons(false);
    buttonBox->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    buttonBox->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    if (QLayout* buttonsLayout = buttonBox->layout()) {
        buttonsLayout->setContentsMargins(0, 0, 0, 0);
        buttonsLayout->setSpacing(10);
        buttonsLayout->setAlignment((buttonBox->buttons().size() <= 1) ? Qt::AlignHCenter : Qt::AlignRight);
    }

    const QList<QPushButton*> buttons = buttonBox->findChildren<QPushButton*>();
    const QString buttonStyle = buildButtonStyleSheet(textColor);
    for (auto* btn : buttons) {
        if (!btn) continue;
        btn->setCursor(Qt::PointingHandCursor);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        btn->setFixedSize((buttons.size() <= 1) ? 118 : 112, 34);
        btn->setFlat(false);
        btn->setAutoDefault(false);
        btn->setDefault(false);
        btn->setAttribute(Qt::WA_StyledBackground, true);
        btn->setStyleSheet(buttonStyle);
    }

    root->addWidget(buttonBox, 0, (buttons.size() <= 1) ? Qt::AlignHCenter : Qt::AlignRight);
    root->activate();
}

void polishNow(QMessageBox* box)
{
    if (!box) return;

    auto* buttonBox = box->findChild<QDialogButtonBox*>();
    if (!buttonBox) return;

    const QString textColor = QStringLiteral("#ffffff");

    box->setTextFormat(Qt::PlainText);
    box->setSizeGripEnabled(false);
    box->setAttribute(Qt::WA_StyledBackground, true);
    box->setStyleSheet(buildStyleSheet(textColor));

    if (auto* textLabel = box->findChild<QLabel*>(QStringLiteral("qt_msgbox_label"))) {
        textLabel->setWordWrap(true);
        textLabel->setTextFormat(Qt::PlainText);
        textLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        textLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        QFont textFont = textLabel->font();
        textFont.setBold(true);
        textFont.setPointSizeF(10.8);
        textLabel->setFont(textFont);
        textLabel->adjustSize();
    }

    if (auto* infoLabel = box->findChild<QLabel*>(QStringLiteral("qt_msgbox_informativelabel"))) {
        infoLabel->setWordWrap(true);
        infoLabel->setTextFormat(Qt::PlainText);
        infoLabel->setTextInteractionFlags(Qt::NoTextInteraction);
        infoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        QFont infoFont = infoLabel->font();
        infoFont.setBold(true);
        infoFont.setPointSizeF(10.2);
        infoLabel->setFont(infoFont);
        infoLabel->adjustSize();
    }

    if (auto* iconLabel = box->findChild<QLabel*>(QStringLiteral("qt_msgboxex_icon_label"))) {
        iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    }

    if (QLayout* buttonsLayout = buttonBox->layout()) {
        buttonsLayout->setContentsMargins(0, 6, 0, 0);
        buttonsLayout->setSpacing(10);
        buttonsLayout->setAlignment((buttonBox->buttons().size() <= 1) ? Qt::AlignHCenter : Qt::AlignRight);
    }

    const QList<QPushButton*> buttons = buttonBox->findChildren<QPushButton*>();
    const QString buttonStyle = buildButtonStyleSheet(textColor);
    for (auto* btn : buttons) {
        if (!btn) continue;
        btn->setCursor(Qt::PointingHandCursor);
        btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        btn->setMinimumHeight(34);
        btn->setMinimumWidth((buttons.size() <= 1) ? 118 : 112);
        btn->setMaximumWidth(220);
        btn->setAutoDefault(false);
        btn->setDefault(false);
        btn->setAttribute(Qt::WA_StyledBackground, true);
        btn->setStyleSheet(buttonStyle);
    }
}

} // namespace

void applyStandardMessageBoxStyle(QMessageBox* box)
{
    if (!box) return;
    box->setWindowTitle(UkrainianDialogText::title(box->windowTitle()));
    box->setText(UkrainianDialogText::translate(box->text()));
    box->setInformativeText(UkrainianDialogText::translate(box->informativeText()));
    box->setDetailedText(UkrainianDialogText::translate(box->detailedText()));
    const QList<QPair<QMessageBox::StandardButton, QString>> captions = {
        {QMessageBox::Ok, QString::fromUtf8("Гаразд")},
        {QMessageBox::Cancel, QString::fromUtf8("Скасувати")},
        {QMessageBox::Yes, QString::fromUtf8("Так")},
        {QMessageBox::No, QString::fromUtf8("Ні")},
        {QMessageBox::YesToAll, QString::fromUtf8("Так для всіх")},
        {QMessageBox::NoToAll, QString::fromUtf8("Ні для всіх")},
        {QMessageBox::Save, QString::fromUtf8("Зберегти")},
        {QMessageBox::SaveAll, QString::fromUtf8("Зберегти все")},
        {QMessageBox::Open, QString::fromUtf8("Відкрити")},
        {QMessageBox::Close, QString::fromUtf8("Закрити")},
        {QMessageBox::Discard, QString::fromUtf8("Відхилити зміни")},
        {QMessageBox::Apply, QString::fromUtf8("Застосувати")},
        {QMessageBox::Reset, QString::fromUtf8("Скинути")},
        {QMessageBox::RestoreDefaults, QString::fromUtf8("Відновити типові")},
        {QMessageBox::Help, QString::fromUtf8("Довідка")},
        {QMessageBox::Retry, QString::fromUtf8("Повторити")},
        {QMessageBox::Abort, QString::fromUtf8("Перервати")},
        {QMessageBox::Ignore, QString::fromUtf8("Пропустити")}
    };
    for (const auto& caption : captions) {
        if (auto* button = box->button(caption.first)) {
            // Preserve descriptive custom Ukrainian actions, such as "Закрити наряд".
            if (!button->text().contains(QRegularExpression(QString::fromUtf8("[А-Яа-яІіЇїЄєҐґ]"))))
                button->setText(caption.second);
        }
    }
    if (box->property("standardPopupStyledOnce").toBool()) return;
    box->setProperty("standardPopupStyledOnce", true);
    polishNow(box);
}

void playStandardMessageBoxAlertSound(QMessageBox* box)
{
    if (!box) return;
    if (!shouldPlayAlertFor(box)) return;
    if (box->property("standardPopupAlertSoundPlayed").toBool()) return;

    static qint64 s_lastPlayMs = 0;
    const qint64 ts = MessageBoxSoundSupport::nowMs();
    if (ts - s_lastPlayMs < 120) {
        box->setProperty("standardPopupAlertSoundPlayed", true);
        return;
    }
    s_lastPlayMs = ts;

    box->setProperty("standardPopupAlertSoundPlayed", true);

    if (!MessageBoxSoundSupport::soundsEnabled()) return;

    const int volumePct = MessageBoxSoundSupport::soundsVolumePct();
    MessageBoxSoundSupport::playAlert(volumePct);
}

void playStandardMessageBoxConfirmationShowSound(QMessageBox* box)
{
    if (!box) return;
    if (!shouldPlayConfirmationShowFor(box)) return;
    if (box->property("standardPopupConfirmationShowSoundPlayed").toBool()) return;

    static qint64 s_lastPlayMs = 0;
    const qint64 ts = MessageBoxSoundSupport::nowMs();
    if (ts - s_lastPlayMs < 120) {
        box->setProperty("standardPopupConfirmationShowSoundPlayed", true);
        return;
    }
    s_lastPlayMs = ts;

    box->setProperty("standardPopupConfirmationShowSoundPlayed", true);

    if (!MessageBoxSoundSupport::soundsEnabled()) return;

    const int volumePct = MessageBoxSoundSupport::soundsVolumePct();
    MessageBoxSoundSupport::playConfirmationShow(volumePct);
}

void installStandardMessageBoxConfirmationYesSoundHook(QMessageBox* box)
{
    if (!box) return;
    if (!shouldPlayConfirmationYesFor(box)) return;
    if (box->property("standardPopupConfirmationYesHookInstalled").toBool()) return;

    box->setProperty("standardPopupConfirmationYesHookInstalled", true);

    auto* yesButton = box->button(QMessageBox::Yes);
    if (!yesButton) return;

    QObject::connect(yesButton, &QAbstractButton::clicked, box, [guard = QPointer<QMessageBox>(box)]() {
        if (!guard) return;
        if (!shouldPlayConfirmationYesFor(guard)) return;
        if (!MessageBoxSoundSupport::soundsEnabled()) return;

        const int volumePct = MessageBoxSoundSupport::soundsVolumePct();
        MessageBoxSoundSupport::playConfirmationYes(volumePct);
    });
}


void installStandardMessageBoxConfirmationNoSoundHook(QMessageBox* box)
{
    if (!box) return;
    if (!shouldPlayConfirmationNoFor(box)) return;
    if (box->property("standardPopupConfirmationNoHookInstalled").toBool()) return;

    box->setProperty("standardPopupConfirmationNoHookInstalled", true);

    auto* noButton = box->button(QMessageBox::No);
    if (!noButton) return;

    QObject::connect(noButton, &QAbstractButton::clicked, box, [guard = QPointer<QMessageBox>(box)]() {
        if (!guard) return;
        if (!shouldPlayConfirmationNoFor(guard)) return;
        if (!MessageBoxSoundSupport::soundsEnabled()) return;

        const int volumePct = MessageBoxSoundSupport::soundsVolumePct();
        MessageBoxSoundSupport::playConfirmationNo(volumePct);
    });
}

void installStandardMessageBoxSettingsActionSoundHook(QMessageBox* box)
{
    if (!box) return;
    if (!shouldPlaySettingsActionFor(box)) return;
    if (box->property("standardPopupSettingsActionHookInstalled").toBool()) return;

    box->setProperty("standardPopupSettingsActionHookInstalled", true);

    MessageBoxSoundSupport::primeSettingsAction();

    const auto buttons = box->buttons();
    for (auto* button : buttons) {
        if (!button) continue;
        auto playActionSound = [guard = QPointer<QMessageBox>(box)]() {
            if (!guard) return;
            if (!shouldPlaySettingsActionFor(guard)) return;
            if (guard->property("standardPopupSettingsActionSoundPlayed").toBool()) return;
            if (!MessageBoxSoundSupport::soundsEnabled()) return;

            guard->setProperty("standardPopupSettingsActionSoundPlayed", true);
            guard->setProperty("standardPopupCloseSoundPlayed", true);

            const int volumePct = MessageBoxSoundSupport::soundsVolumePct();
            MessageBoxSoundSupport::playSettingsAction(volumePct);
        };

        if (auto* pushButton = qobject_cast<QPushButton*>(button)) {
            QObject::connect(pushButton, &QPushButton::pressed, box, playActionSound);
        }
        QObject::connect(button, &QAbstractButton::clicked, box, playActionSound);
    }
}

void playStandardMessageBoxCloseSound(QMessageBox* box)
{
    if (!box) return;
    if (!shouldPlayCloseFor(box)) return;
    if (box->property("standardPopupCloseSoundPlayed").toBool()) return;

    static qint64 s_lastPlayMs = 0;
    const qint64 ts = MessageBoxSoundSupport::nowMs();
    if (ts - s_lastPlayMs < 80) {
        box->setProperty("standardPopupCloseSoundPlayed", true);
        return;
    }
    s_lastPlayMs = ts;

    box->setProperty("standardPopupCloseSoundPlayed", true);

    if (!MessageBoxSoundSupport::soundsEnabled()) return;

    const int volumePct = MessageBoxSoundSupport::soundsVolumePct();
    MessageBoxSoundSupport::playClose(volumePct);
}
