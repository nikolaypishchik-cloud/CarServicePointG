#include "ui/tabs/settings/SettingsFinanceSection.h"

#include "ui/tabs/settings/SettingsPageDataSupport.h"
#include "ui/tabs/settings/SettingsFinanceSectionUiSupport.h"
#include "core/services/settings/FinanceSettingsService.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QUuid>

using namespace SettingsPageDataSupport;

namespace {

static bool saveFinanceMarkups(const QString& storeMarkupsJson,
                               const QString& oilStoreMarkupsJson,
                               double partsDefault,
                               double oilDefault,
                               FinanceSettingsService::MarkupRecalculationMode recalculationMode,
                               bool* changed = nullptr,
                               QString* errorText = nullptr)
{
    FinanceSettingsService::MarkupSaveRequest request;
    request.storeMarkupsJson = storeMarkupsJson;
    request.oilStoreMarkupsJson = oilStoreMarkupsJson;
    request.defaultPartsMarkupPercent = partsDefault;
    request.oilDefault = oilDefault;
    request.recalculationMode = recalculationMode;
    return FinanceSettingsService::saveMarkupSettings(request, changed, errorText);
}

using namespace SettingsFinanceSectionUiSupport;

class OutsourceSettingsSectionWidget final : public QWidget
{
public:
    explicit OutsourceSettingsSectionWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        m_entries = loadOutsourceEntries();
        buildUi();
        refreshList();
        if (!m_entries.isEmpty()) {
            m_list->setCurrentRow(0);
        }
        m_currentIndex = -1;
        m_stack->setCurrentWidget(m_listPage);
    }

private:
    QList<OutsourceEntry> m_entries;
    QListWidget* m_list = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_listPage = nullptr;
    QWidget* m_editPage = nullptr;
    QLabel* m_editTitle = nullptr;
    QLineEdit* m_company = nullptr;
    QLineEdit* m_address = nullptr;
    QLineEdit* m_contact = nullptr;
    QLineEdit* m_branch = nullptr;
    QPlainTextEdit* m_note = nullptr;
    QDoubleSpinBox* m_markup = nullptr;
    int m_currentIndex = -1;

    void buildUi()
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(14, 14, 14, 14);
        root->setSpacing(10);

        auto* title = new QLabel(QString::fromUtf8("Аутсорс"), this);
        title->setStyleSheet(QString("font-size:16pt; font-weight:800; color:%1;").arg(cPrimary()));
        root->addWidget(title);

        m_stack = new QStackedWidget(this);
        root->addWidget(m_stack, 1);

        buildListPage();
        buildEditPage();
        m_stack->setCurrentWidget(m_listPage);
    }

    void buildListPage()
    {
        m_listPage = new QWidget(this);
        auto* l = new QVBoxLayout(m_listPage);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(12);

        auto* panel = new QFrame(m_listPage);
        panel->setObjectName(QStringLiteral("outsource_company_list_panel"));
        const QString panelGradient = isDarkUi()
            ? QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 rgba(132,146,162,0.96),stop:0.18 rgba(96,110,126,0.98),stop:0.52 rgba(73,86,101,0.98),stop:1 rgba(45,56,69,0.99))")
            : QStringLiteral("qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #ffffff,stop:0.18 #eef3f8,stop:0.55 #dfe7ef,stop:1 #cdd7e2)");
        panel->setStyleSheet(QString("QFrame#outsource_company_list_panel{background:%1;border:1px solid %2;border-radius:14px;}"
                                     "QFrame#outsource_company_list_panel QListWidget{background:transparent;border:none;color:%3;font-size:11pt;}"
                                     "QFrame#outsource_company_list_panel QListWidget::item{padding:14px 18px;border-radius:10px;background:transparent;}"
                                     "QFrame#outsource_company_list_panel QListWidget::item:selected{background:%4;}")
                                 .arg(panelGradient).arg(cPanelBorder()).arg(cPrimary()).arg(cSelBg()));
        auto* panelShadow = new QGraphicsDropShadowEffect(panel);
        panelShadow->setBlurRadius(18);
        panelShadow->setOffset(0, 2);
        panelShadow->setColor(QColor(0, 0, 0, isDarkUi() ? 95 : 45));
        panel->setGraphicsEffect(panelShadow);
        auto* pl = new QVBoxLayout(panel);
        pl->setContentsMargins(14, 14, 14, 14);
        pl->setSpacing(10);

        m_list = new QListWidget(panel);
        m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        pl->addWidget(m_list, 1);
        l->addWidget(panel, 1);

        auto* btnRow = new QHBoxLayout();
        auto mk = [this](const QString& t, bool primary = false) {
            auto* b = new QPushButton(t, this);
            b->setProperty("stay_in_detail_menu", true);
            b->setMinimumWidth(168);
            styleRaisedButton(b, primary);
            if (t == QString::fromUtf8("Зберегти")) {
                b->setFixedSize(120, 40);
            }
            return b;
        };
        auto* btnAdd = mk(QString::fromUtf8("Додати"));
        auto* btnDelete = mk(QString::fromUtf8("Видалити"));
        auto* btnSave = mk(QString::fromUtf8("Зберегти"), true);
        btnRow->addWidget(btnAdd);
        btnRow->addWidget(btnDelete);
        btnRow->addWidget(btnSave);
        btnRow->addStretch(1);
        l->addLayout(btnRow);

        connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
            if (!it) return;
            openEditor(m_list->row(it));
        });
        connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
            if (!it) return;
            openEditor(m_list->row(it));
        });
        connect(btnAdd, &QPushButton::clicked, this, [this]() {
            OutsourceEntry e;
            e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            e.company = QString::fromUtf8("Нова компанія");
            e.markup = 20.0;
            m_entries.push_back(e);
            refreshList();
            openEditor(m_entries.size() - 1);
        });
        connect(btnDelete, &QPushButton::clicked, this, [this]() {
            const int row = m_list->currentRow();
            if (row < 0 || row >= m_entries.size()) return;
            m_entries.removeAt(row);
            saveOutsourceEntries(m_entries);
            refreshList();
            if (!m_entries.isEmpty()) m_list->setCurrentRow(qMin(row, m_entries.size() - 1));
        });
        connect(btnSave, &QPushButton::clicked, this, [this]() {
            saveOutsourceEntries(m_entries);
            QMessageBox::information(this, QString::fromUtf8("Аутсорс"), QString::fromUtf8("Налаштування аутсорсу збережено."));
        });

        m_stack->addWidget(m_listPage);
    }

    void buildEditPage()
    {
        m_editPage = new QWidget(this);
        auto* l = new QVBoxLayout(m_editPage);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(12);

        auto* top = new QHBoxLayout();
        top->setContentsMargins(0, 0, 0, 0);
        top->setSpacing(12);
        auto configureTopButton = [](QPushButton* button, const QSize& size, bool primary) {
            styleGlossyPillButton(button, primary);
            button->setCursor(Qt::PointingHandCursor);
            button->setFixedSize(size);
            button->setMinimumSize(size);
            button->setMaximumSize(size);
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        };

        auto* btnBack = new QPushButton(QString::fromUtf8("← Назад"), m_editPage);
        btnBack->setProperty("stay_in_detail_menu", true);
        configureTopButton(btnBack, QSize(120, 40), false);
        m_editTitle = new QLabel(QString(), m_editPage);
        m_editTitle->setStyleSheet(QString("font-size:16pt; font-weight:800; color:%1;").arg(cPrimary()));
        auto* btnCancel = new QPushButton(QString::fromUtf8("Скасувати"), m_editPage);
        btnCancel->setProperty("stay_in_detail_menu", true);
        configureTopButton(btnCancel, QSize(120, 40), false);
        auto* btnApply = new QPushButton(QString::fromUtf8("Зберегти"), m_editPage);
        btnApply->setProperty("stay_in_detail_menu", true);
        configureTopButton(btnApply, QSize(120, 40), true);
        top->addWidget(btnBack, 0, Qt::AlignVCenter);
        top->addWidget(m_editTitle, 1, Qt::AlignVCenter);
        top->addWidget(btnCancel, 0, Qt::AlignVCenter);
        top->addWidget(btnApply, 0, Qt::AlignVCenter);
        l->addLayout(top);

        auto* scroll = new QScrollArea(m_editPage);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* panel = new QFrame(m_editPage);
        panel->setStyleSheet(QString::fromUtf8(
            "QFrame{background:%1;border:1px solid %2;border-radius:20px;}"
            "QLabel{background:transparent;border:none;color:%3;}"
            "QLineEdit,QPlainTextEdit,QDoubleSpinBox{background:#ffffff;border:1px solid rgba(18,24,32,0.18);border-radius:16px;padding:7px 14px;color:#111111;selection-background-color:rgba(66,124,214,0.22);selection-color:#111111;min-height:34px;}"
            "QLineEdit:focus,QPlainTextEdit:focus,QDoubleSpinBox:focus{border:1px solid rgba(83,145,237,0.72);background:#ffffff;color:#111111;}"
            "QDoubleSpinBox::up-button,QDoubleSpinBox::down-button{width:0px;border:none;background:transparent;}"
            "QPlainTextEdit{padding-top:8px;}"
        ).arg(isDarkUi() ? "#526071" : "#f7f7f7").arg(cPanelBorder()).arg(cPrimary()));
        auto* pl = new QVBoxLayout(panel);
        pl->setContentsMargins(16, 16, 16, 16);
        pl->setSpacing(10);

        auto* tabChip = new QLabel(QString::fromUtf8("Компанія"), panel);
        tabChip->setStyleSheet(QString::fromUtf8("background:transparent;border:none;padding:0 2px 2px 2px;color:%1;font-size:12pt;font-weight:800;").arg(cPrimary()));
        pl->addWidget(tabChip, 0, Qt::AlignLeft);

        auto* form = new QFormLayout();
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignTop);
        form->setHorizontalSpacing(26);
        form->setVerticalSpacing(10);

        m_company = new QLineEdit(panel);
        m_address = new QLineEdit(panel);
        m_contact = new QLineEdit(panel);
        m_branch = new QLineEdit(panel);
        m_note = new QPlainTextEdit(panel);
        m_note->setFixedHeight(88);
        m_markup = new QDoubleSpinBox(panel);
        m_markup->setRange(0.0, 300.0);
        m_markup->setDecimals(1);
        m_markup->setSuffix(QString::fromUtf8(" %"));
        m_markup->setSingleStep(1.0);
        m_markup->setMinimumWidth(180);

        m_company->setPlaceholderText(QString::fromUtf8("Компанія"));
        m_address->setPlaceholderText(QString::fromUtf8("Адреса / місто"));
        m_contact->setPlaceholderText(QString::fromUtf8("Ім'я та телефон"));
        m_branch->setPlaceholderText(QString::fromUtf8("Нова Пошта (№/адреса)"));
        m_note->setPlaceholderText(QString::fromUtf8("Примітка (опціонально)"));

        auto* lblCompany = new QLabel(QString::fromUtf8("Компанія"), panel);
        auto* lblAddress = new QLabel(QString::fromUtf8("Де знаходиться (адреса)"), panel);
        auto* lblContact = new QLabel(QString::fromUtf8("Відповідальний (ім'я, телефон)"), panel);
        auto* lblBranch = new QLabel(QString::fromUtf8("Відділення НП"), panel);
        auto* lblNote = new QLabel(QString::fromUtf8("Примітка"), panel);
        auto* lblMarkup = new QLabel(QString::fromUtf8("Доданий % до ремонту"), panel);
        for (auto* lbl : {lblCompany, lblAddress, lblContact, lblBranch, lblNote, lblMarkup}) styleFormLabel(lbl);
        form->addRow(lblCompany, m_company);
        form->addRow(lblAddress, m_address);
        form->addRow(lblContact, m_contact);
        form->addRow(lblBranch, m_branch);
        form->addRow(lblNote, m_note);
        form->addRow(lblMarkup, m_markup);
        pl->addLayout(form);
        scroll->setWidget(panel);
        l->addWidget(scroll, 1);

        connect(btnBack, &QPushButton::clicked, this, [this]() { m_stack->setCurrentWidget(m_listPage); });
        connect(btnCancel, &QPushButton::clicked, this, [this]() { m_stack->setCurrentWidget(m_listPage); });
        connect(btnApply, &QPushButton::clicked, this, [this]() {
            applyEditor();
            saveOutsourceEntries(m_entries);
            refreshList();
            m_stack->setCurrentWidget(m_listPage);
        });

        m_stack->addWidget(m_editPage);
    }

    void refreshList()
    {
        const QSignalBlocker blocker(m_list);
        m_list->clear();
        for (const auto& e : m_entries) {
            auto* it = new QListWidgetItem(e.company.trimmed().isEmpty() ? QString::fromUtf8("Без назви") : e.company, m_list);
            it->setData(Qt::UserRole, e.id);
            m_list->addItem(it);
        }
    }

    void openEditor(int row)
    {
        if (row < 0 || row >= m_entries.size()) return;
        m_currentIndex = row;
        const auto& e = m_entries[row];
        m_editTitle->setText(e.company.trimmed().isEmpty() ? QString::fromUtf8("Нова компанія") : e.company);
        m_company->setText(e.company);
        m_address->setText(e.address);
        m_contact->setText(e.contact);
        m_branch->setText(e.branch);
        m_note->setPlainText(e.note);
        m_markup->setValue(e.markup);
        m_stack->setCurrentWidget(m_editPage);
    }

    void applyEditor()
    {
        if (m_currentIndex < 0 || m_currentIndex >= m_entries.size()) return;
        auto& e = m_entries[m_currentIndex];
        e.company = titleCaseWords(m_company->text());
        e.address = m_address->text().trimmed();
        e.contact = m_contact->text().trimmed();
        e.branch = m_branch->text().trimmed();
        e.note = m_note->toPlainText().trimmed();
        e.markup = m_markup->value();
    }
};

QWidget* buildFinanceDefaultsWidget(QWidget* parent)
{

        auto* w = new QWidget(parent);
        auto* outer = new QVBoxLayout(w);
        outer->setContentsMargins(0,0,0,0);
        outer->setSpacing(0);

        auto* fixedTopWrap = new QWidget(w);
        auto* fixedTopLayout = new QVBoxLayout(fixedTopWrap);
        fixedTopLayout->setContentsMargins(18,18,18,12);
        fixedTopLayout->setSpacing(0);
        outer->addWidget(fixedTopWrap, 0);

        auto* scroll = new QScrollArea(w);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        outer->addWidget(scroll, 1);

        auto* body = new QWidget(scroll);
        auto* l = new QVBoxLayout(body);
        l->setContentsMargins(18,0,18,18);
        l->setSpacing(12);
        scroll->setWidget(body);

        auto* topBar = new QHBoxLayout();
        topBar->setContentsMargins(0,0,0,0);
        topBar->setSpacing(12);
        auto* btnBack = makeSectionBackButton(body);
        btnBack->setFixedSize(120, 40);
        auto* title = new QLabel(QString::fromUtf8("Націнка запчастин"), body);
        title->setStyleSheet(QString("font-size:16pt; font-weight:800; color:%1;").arg(cPrimary()));
        title->setAlignment(Qt::AlignCenter);
        auto* btnReset = new QPushButton(QString::fromUtf8("Скасувати"), body);
        auto* btnSave = new QPushButton(QString::fromUtf8("Зберегти"), body);
        btnReset->setFixedSize(120, 40);
        btnSave->setFixedSize(120, 40);
        topBar->addWidget(btnBack, 0, Qt::AlignVCenter);
        topBar->addStretch(1);
        topBar->addWidget(title, 0, Qt::AlignCenter);
        topBar->addStretch(1);
        topBar->addWidget(btnReset, 0, Qt::AlignVCenter);
        topBar->addWidget(btnSave, 0, Qt::AlignVCenter);
        fixedTopLayout->addLayout(topBar);

        const QString groupBg = isDarkUi() ? QStringLiteral("#4a5665") : panelBg();
        const QString fieldBg = isDarkUi() ? QStringLiteral("#eff3f7") : QStringLiteral("#ffffff");
        const QString fieldText = QStringLiteral("#101820");
        const QString buttonBg = isDarkUi() ? QStringLiteral("#566170") : QStringLiteral("#d7dee7");
        const QString buttonHover = isDarkUi() ? QStringLiteral("#627081") : QStringLiteral("#e2e8ef");
        const QString buttonGlossTop = isDarkUi() ? QStringLiteral("rgba(255,255,255,0.34)") : QStringLiteral("rgba(255,255,255,0.76)");
        const QString buttonBorder = QStringLiteral("rgba(255,255,255,0.62)");
        const QString buttonBorderHover = QStringLiteral("rgba(255,255,255,0.84)");
        const QString buttonShadow = isDarkUi() ? QStringLiteral("rgba(0,0,0,0.34)") : QStringLiteral("rgba(41,52,66,0.18)");

        auto makeGroup = [body, groupBg, fieldBg, fieldText](const QString& caption) {
            auto* gb = new QGroupBox(caption, body);
            gb->setStyleSheet(QString::fromUtf8(R"QSS(
                QGroupBox {
                    font-weight: 700;
                    color: %4;
                    border: 1px solid %1;
                    border-radius: 16px;
                    margin-top: 14px;
                    padding-top: 18px;
                    background: %2;
                }
                QGroupBox::title {
                    subcontrol-origin: margin;
                    left: 18px;
                    padding: 4px 14px;
                    background: %2;
                    border: 1px solid %1;
                    border-radius: 12px;
                    color: %4;
                }
                QLabel { color: %4; }
                QLineEdit, QDoubleSpinBox, QComboBox {
                    min-height: 33px;
                    background: %3;
                    color: %2;
                    border: 1px solid %1;
                    border-radius: 12px;
                    padding: 4px 14px;
                    font-size: 11pt;
                }
                QComboBox::drop-down {
                    width: 32px;
                    border: none;
                    background: transparent;
                }
                QComboBox QAbstractItemView {
                    background: %3;
                    color: %2;
                    border: 1px solid %1;
                    selection-background-color: rgba(94,129,172,0.22);
                    selection-color: %2;
                }
                QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
                    width: 0px;
                    border: none;
                    background: transparent;
                }
            )QSS")
                .arg(cPanelBorder())
                .arg(groupBg)
                .arg(fieldBg)
                .arg(fieldText));
            auto* gl = new QVBoxLayout(gb);
            gl->setContentsMargins(18, 20, 18, 18);
            gl->setSpacing(12);
            return gb;
        };

        auto* partsGroup = makeGroup(QString::fromUtf8("Запчастини"));
        auto* oilGroup = makeGroup(QString::fromUtf8("Моторне мастило"));

        auto* partsLayout = qobject_cast<QVBoxLayout*>(partsGroup->layout());
        auto* oilLayout = qobject_cast<QVBoxLayout*>(oilGroup->layout());

        auto* partsDefaultWrap = new QWidget(partsGroup);
        auto* partsDefaultRow = new QHBoxLayout(partsDefaultWrap);
        partsDefaultRow->setContentsMargins(0, 0, 0, 4);
        partsDefaultRow->setSpacing(14);
        auto* partsDefaultLabel = new QLabel(QString::fromUtf8("Глобальна націнка запчастин, %"), partsDefaultWrap);
        partsDefaultLabel->setStyleSheet("font-weight:800;");
        auto* partsDefaultPct = new QDoubleSpinBox(partsDefaultWrap);
        partsDefaultPct->setRange(0.0, 300.0);
        partsDefaultPct->setDecimals(1);
        partsDefaultPct->setSuffix(" %");
        partsDefaultPct->setButtonSymbols(QAbstractSpinBox::NoButtons);
        partsDefaultPct->setMinimumHeight(44);
        partsDefaultPct->setFixedWidth(190);
        partsDefaultRow->addWidget(partsDefaultLabel, 1);
        partsDefaultRow->addWidget(partsDefaultPct, 0);
        partsLayout->addWidget(partsDefaultWrap);

        auto makePartsHeader = [body]() {
            auto* row = new QWidget(body);
            auto* hl = new QHBoxLayout(row);
            hl->setContentsMargins(0,0,0,0);
            hl->setSpacing(14);
            auto* left = new QLabel(QString::fromUtf8("Назва магазину"), row);
            auto* right = new QLabel(QString::fromUtf8("Доданий %"), row);
            left->setMinimumWidth(280);
            left->setStyleSheet("font-weight:700;");
            right->setFixedWidth(190);
            right->setAlignment(Qt::AlignCenter);
            right->setStyleSheet("font-weight:700;");
            hl->addWidget(left, 1);
            hl->addWidget(right);
            return row;
        };

        auto makeOilHeader = [body]() {
            auto* row = new QWidget(body);
            auto* hl = new QHBoxLayout(row);
            hl->setContentsMargins(0,0,0,0);
            hl->setSpacing(14);
            auto* storeLbl = new QLabel(QString::fromUtf8("Магазин"), row);
            auto* brandLbl = new QLabel(QString::fromUtf8("Бренд"), row);
            auto* oilLbl = new QLabel(QString::fromUtf8("Мастило"), row);
            auto* pctLbl = new QLabel(QString::fromUtf8("Доданий %"), row);
            storeLbl->setMinimumWidth(260);
            brandLbl->setFixedWidth(180);
            oilLbl->setFixedWidth(170);
            pctLbl->setFixedWidth(150);
            for (auto* lbl : {storeLbl, brandLbl, oilLbl, pctLbl}) lbl->setStyleSheet("font-weight:700;");
            brandLbl->setAlignment(Qt::AlignCenter);
            oilLbl->setAlignment(Qt::AlignCenter);
            pctLbl->setAlignment(Qt::AlignCenter);
            hl->addWidget(storeLbl, 1);
            hl->addWidget(brandLbl, 0);
            hl->addWidget(oilLbl, 0);
            hl->addWidget(pctLbl, 0);
            return row;
        };

        partsLayout->addWidget(makePartsHeader());
        oilLayout->addWidget(makeOilHeader());

        auto* partsRowsWrap = new QWidget(partsGroup);
        auto* partsRows = new QVBoxLayout(partsRowsWrap);
        partsRows->setContentsMargins(0,0,0,0);
        partsRows->setSpacing(12);
        partsLayout->addWidget(partsRowsWrap);

        auto* oilRowsWrap = new QWidget(oilGroup);
        auto* oilRows = new QVBoxLayout(oilRowsWrap);
        oilRows->setContentsMargins(0,0,0,0);
        oilRows->setSpacing(12);
        oilLayout->addWidget(oilRowsWrap);
        auto applyFinanceButtonStyle = [](QPushButton* b, bool primary = false) {
            Q_UNUSED(primary);
            b->setCursor(Qt::PointingHandCursor);
            b->setFixedHeight(52);
            b->setStyleSheet(QString::fromUtf8(
                "QPushButton {"
                " color:#f8fbff;"
                " font-weight:800;"
                " font-size:11pt;"
                " min-height:50px;"
                " padding:0 24px;"
                " border:1px solid rgba(255,255,255,0.72);"
                " border-radius:18px;"
                " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                "  stop:0 #536170, stop:0.12 #7b8998, stop:0.46 #313b48, stop:0.52 #111825, stop:1 #091018);"
                "}"
                "QPushButton:hover {"
                " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                "  stop:0 #5f6d7c, stop:0.14 #8b98a5, stop:0.48 #394452, stop:0.54 #151d2b, stop:1 #0b1220);"
                " border-color: rgba(255,255,255,0.82);"
                "}"
                "QPushButton:pressed {"
                " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                "  stop:0 #101722, stop:0.45 #1a2533, stop:1 #445262);"
                " padding-top:1px;"
                " padding-bottom:0px;"
                "}"
                "QPushButton:disabled {"
                " color: rgba(245,248,252,0.52);"
                " border-color: rgba(255,255,255,0.18);"
                " background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
                "  stop:0 rgba(90,100,112,0.50), stop:1 rgba(35,41,49,0.44));"
                "}"
            ));
            auto* shadow = new QGraphicsDropShadowEffect(b);
            shadow->setBlurRadius(18.0);
            shadow->setOffset(0, 4.0);
            shadow->setColor(QColor(0, 0, 0, 120));
            b->setGraphicsEffect(shadow);
        };

        auto makeButtonsRow = [body, applyFinanceButtonStyle](const QString& addText) {
            auto* row = new QWidget(body);
            auto* hl = new QHBoxLayout(row);
            hl->setContentsMargins(0,0,0,0);
            hl->setSpacing(10);
            auto styleBtn = [applyFinanceButtonStyle](QPushButton* b) {
                applyFinanceButtonStyle(b, false);
            };
            auto* add = new QPushButton(addText, row);
            auto* del = new QPushButton(QString::fromUtf8("Видалити"), row);
            add->setFixedSize(240, 52);
            del->setFixedSize(190, 52);
            styleBtn(add);
            styleBtn(del);
            hl->addWidget(add, 0);
            hl->addWidget(del, 0);
            hl->addStretch(1);
            row->setProperty("addBtn", QVariant::fromValue<QObject*>(add));
            row->setProperty("delBtn", QVariant::fromValue<QObject*>(del));
            return row;
        };

        auto* partsBtns = makeButtonsRow(QString::fromUtf8("Додати магазин"));
        auto* oilBtns = makeButtonsRow(QString::fromUtf8("Додати магазин"));
        partsLayout->addWidget(partsBtns);
        oilLayout->addWidget(oilBtns);

        applyFinanceButtonStyle(btnReset, false);
        applyFinanceButtonStyle(btnSave, true);

        l->addWidget(partsGroup);
        l->addWidget(oilGroup);
        l->addStretch(1);

        struct MarkupRowWidgets {
            QWidget* root = nullptr;
            QLineEdit* store = nullptr;
            QLineEdit* oilBrand = nullptr;
            QComboBox* oilGrade = nullptr;
            QDoubleSpinBox* pct = nullptr;
            bool oilMode = false;
        };

        struct SimpleMarkupRowData {
            QString store;
            double pct = 0.0;
        };

        auto* partRowsList = new QList<MarkupRowWidgets>();
        auto* oilRowsList = new QList<MarkupRowWidgets>();
        const QStringList oilGradeChoices = FinanceSettingsService::loadOilGradeChoices();

        auto createRow = [body, oilGradeChoices](QVBoxLayout* host, QList<MarkupRowWidgets>* list,
                                                  const QString& storeName, double pctValue,
                                                  bool oilMode, const QString& oilGrade = QString(),
                                                  const QString& oilBrand = QString()) {
            MarkupRowWidgets rw;
            rw.oilMode = oilMode;
            rw.root = new QWidget(body);
            auto* hl = new QHBoxLayout(rw.root);
            hl->setContentsMargins(0,0,0,0);
            hl->setSpacing(14);
            rw.store = new QLineEdit(rw.root);
            rw.store->setProperty("inputCorrectionRole", "protected");
            rw.store->setPlaceholderText(QString::fromUtf8("Напр.: Еліт"));
            rw.store->setText(titleCaseWords(storeName));
            rw.store->setMinimumHeight(44);
            QObject::connect(rw.store, &QLineEdit::editingFinished, rw.store, [edit = rw.store]() {
                const QString norm = titleCaseWords(edit->text());
                if (edit->text() != norm) edit->setText(norm);
            });
            if (oilMode) {
                rw.store->setMinimumWidth(260);
                rw.store->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

                rw.oilBrand = new QLineEdit(rw.root);
                rw.oilBrand->setProperty("inputCorrectionRole", "protected");
                rw.oilBrand->setPlaceholderText(QString::fromUtf8("Напр.: Castrol"));
                rw.oilBrand->setText(titleCaseWords(oilBrand));
                rw.oilBrand->setMinimumHeight(44);
                rw.oilBrand->setFixedWidth(180);
                QObject::connect(rw.oilBrand, &QLineEdit::editingFinished, rw.oilBrand, [edit = rw.oilBrand]() {
                    const QString norm = titleCaseWords(edit->text());
                    if (edit->text() != norm) edit->setText(norm);
                });

                rw.oilGrade = new QComboBox(rw.root);
                rw.oilGrade->setMinimumHeight(44);
                rw.oilGrade->setFixedWidth(170);
                rw.oilGrade->addItem(QString::fromUtf8("Оберіть мастило"), QString());
                for (const QString& grade : oilGradeChoices) rw.oilGrade->addItem(grade, grade);
                const QString normalizedGrade = normalizeOilGrade(oilGrade);
                const int idx = rw.oilGrade->findData(normalizedGrade);
                rw.oilGrade->setCurrentIndex(idx >= 0 ? idx : 0);
                hl->addWidget(rw.store, 1);
                hl->addWidget(rw.oilBrand, 0);
                hl->addWidget(rw.oilGrade, 0);
            } else {
                hl->addWidget(rw.store, 1);
            }
            rw.pct = new QDoubleSpinBox(rw.root);
            rw.pct->setRange(0.0, 300.0);
            rw.pct->setDecimals(1);
            rw.pct->setSuffix(" %");
            rw.pct->setValue(pctValue);
            rw.pct->setButtonSymbols(QAbstractSpinBox::NoButtons);
            rw.pct->setMinimumHeight(44);
            rw.pct->setFixedWidth(oilMode ? 150 : 190);
            hl->addWidget(rw.pct, 0);
            host->addWidget(rw.root);
            list->append(rw);
        };

        auto clearRows = [](QVBoxLayout* host, QList<MarkupRowWidgets>* list) {
            while (!list->isEmpty()) {
                MarkupRowWidgets rw = list->takeLast();
                host->removeWidget(rw.root);
                delete rw.root;
            }
        };

        auto collectPartsJson = [](const QList<MarkupRowWidgets>& list) {
            QJsonObject obj;
            for (const auto& row : list) {
                const QString key = titleCaseWords(row.store->text());
                if (key.isEmpty()) continue;
                obj.insert(key, row.pct->value());
            }
            return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        };

        auto collectOilJson = [](const QList<MarkupRowWidgets>& list) {
            QJsonArray arr;
            for (const auto& row : list) {
                const QString store = titleCaseWords(row.store->text());
                const QString brand = row.oilBrand ? titleCaseWords(row.oilBrand->text()) : QString();
                const QString oil = row.oilGrade ? normalizeOilGrade(row.oilGrade->currentData().toString()) : QString();
                const double pct = row.pct->value();
                if (store.isEmpty() && brand.isEmpty() && oil.isEmpty() && pct <= 0.0) continue;
                QJsonObject o;
                o.insert(QStringLiteral("store"), store);
                o.insert(QStringLiteral("brand"), brand);
                o.insert(QStringLiteral("oil"), oil);
                o.insert(QStringLiteral("pct"), pct);
                arr.append(o);
            }
            return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        };

        auto loadPartRowsFromSetting = [&](QVBoxLayout* host, QList<MarkupRowWidgets>* list, const QList<SimpleMarkupRowData>& fallbackRows) {
            clearRows(host, list);
            const QString raw = FinanceSettingsService::loadMarkupSnapshot().storeMarkupsJson;
            const auto doc = QJsonDocument::fromJson(raw.toUtf8());
            QList<SimpleMarkupRowData> rows;
            if (doc.isObject()) {
                const auto obj = doc.object();
                for (auto it = obj.begin(); it != obj.end(); ++it) rows.append(SimpleMarkupRowData{it.key(), it.value().toDouble()});
            }
            if (rows.isEmpty()) rows = fallbackRows;
            if (rows.isEmpty()) rows.append(SimpleMarkupRowData{QString(), 0.0});
            for (const auto& pair : rows) createRow(host, list, pair.store, pair.pct, false);
        };

        auto loadOilRowsFromSetting = [&](QVBoxLayout* host, QList<MarkupRowWidgets>* list, const QList<FinanceSettingsService::OilMarkupRule>& fallbackRows) {
            clearRows(host, list);
            QList<FinanceSettingsService::OilMarkupRule> rows = FinanceSettingsService::loadOilMarkupRules(FinanceSettingsService::loadMarkupSnapshot().oilStoreMarkupsJson);
            if (rows.isEmpty()) rows = fallbackRows;
            if (rows.isEmpty()) rows.append(FinanceSettingsService::OilMarkupRule{QString(), QString(), QString(), 0.0});
            for (const auto& row : rows) createRow(host, list, row.store, row.pct, true, row.oilGrade, row.brand);
        };

        auto deleteLastFilledRow = [](QVBoxLayout* host, QList<MarkupRowWidgets>* list) {
            if (list->isEmpty()) return;
            int idx = list->size() - 1;
            while (idx > 0) {
                const auto& row = list->at(idx);
                const bool hasStore = !row.store->text().trimmed().isEmpty();
                const bool hasBrand = row.oilBrand && !row.oilBrand->text().trimmed().isEmpty();
                const bool hasOil = row.oilGrade && !normalizeOilGrade(row.oilGrade->currentData().toString()).isEmpty();
                if (hasStore || hasBrand || hasOil || row.pct->value() != 0.0) break;
                --idx;
            }
            auto rw = list->takeAt(idx);
            host->removeWidget(rw.root);
            delete rw.root;
        };

        auto reloadUi = [&]() {
            const auto snapshot = FinanceSettingsService::loadMarkupSnapshot();
            partsDefaultPct->setValue(qMax(0.0, snapshot.defaultPartsMarkupPercent));
            loadPartRowsFromSetting(partsRows, partRowsList, {
                {QString(), 0.0}, {QString(), 0.0}, {QString(), 0.0}
            });
            QList<FinanceSettingsService::OilMarkupRule> oilFallback;
            const double oldOilDefault = snapshot.oilDefault;
            oilFallback.append(FinanceSettingsService::OilMarkupRule{QString(), QString(), QString(), oldOilDefault > 0.0 ? oldOilDefault : 0.0});
            loadOilRowsFromSetting(oilRows, oilRowsList, oilFallback);
        };

        reloadUi();

        auto* partsAddBtn = qobject_cast<QPushButton*>(partsBtns->property("addBtn").value<QObject*>());
        auto* partsDelBtn = qobject_cast<QPushButton*>(partsBtns->property("delBtn").value<QObject*>());
        auto* oilAddBtn = qobject_cast<QPushButton*>(oilBtns->property("addBtn").value<QObject*>());
        auto* oilDelBtn = qobject_cast<QPushButton*>(oilBtns->property("delBtn").value<QObject*>());

        QObject::connect(partsAddBtn, &QPushButton::clicked, w, [=]() {
            createRow(partsRows, partRowsList, QString(), 0.0, false);
        });
        QObject::connect(oilAddBtn, &QPushButton::clicked, w, [=]() {
            createRow(oilRows, oilRowsList, QString(), 0.0, true);
        });
        QObject::connect(partsDelBtn, &QPushButton::clicked, w, [=]() {
            deleteLastFilledRow(partsRows, partRowsList);
            if (partRowsList->isEmpty()) createRow(partsRows, partRowsList, QString(), 0.0, false);
        });
        QObject::connect(oilDelBtn, &QPushButton::clicked, w, [=]() {
            deleteLastFilledRow(oilRows, oilRowsList);
            if (oilRowsList->isEmpty()) createRow(oilRows, oilRowsList, QString(), 0.0, true);
        });

        QObject::connect(btnReset, &QPushButton::clicked, w, [reloadUi]() {
            reloadUi();
        });

        QObject::connect(btnSave, &QPushButton::clicked, w, [=]() {
            const QString newStoreMarkups = collectPartsJson(*partRowsList);
            const QString newOilStoreMarkups = collectOilJson(*oilRowsList);

            double oilDefault = 0.0;
            for (const auto& row : *oilRowsList) {
                if (row.pct->value() > 0.0) {
                    oilDefault = row.pct->value();
                    if (!row.store->text().trimmed().isEmpty()) break;
                }
            }

            bool changed = false;
            QString saveErrorText;
            const double partsDefault = partsDefaultPct->value();

            FinanceSettingsService::MarkupRecalculationMode recalcMode = FinanceSettingsService::MarkupRecalculationMode::NoRecalculate;
            QMessageBox recalcBox(w);
            recalcBox.setIcon(QMessageBox::Question);
            recalcBox.setWindowTitle(QString::fromUtf8("Перерахунок націнки"));
            recalcBox.setText(QString::fromUtf8("Як застосувати нові правила націнки?"));
            recalcBox.setInformativeText(QString::fromUtf8("Рекомендовано перерахувати відкриті наряди. Перерахунок усіх нарядів змінює історичні фінансові суми в активній базі. Для повністю безпечного збереження без зміни існуючих сум оберіть “Не перераховувати зараз”."));
            auto* btnOpen = recalcBox.addButton(QString::fromUtf8("Перерахувати відкриті"), QMessageBox::AcceptRole);
            auto* btnNewOnly = recalcBox.addButton(QString::fromUtf8("Тільки для нових"), QMessageBox::NoRole);
            auto* btnNoRecalc = recalcBox.addButton(QString::fromUtf8("Не перераховувати зараз"), QMessageBox::RejectRole);
            auto* btnAll = recalcBox.addButton(QString::fromUtf8("Перерахувати всі"), QMessageBox::DestructiveRole);
            if (auto* defaultRecalcButton = qobject_cast<QPushButton*>(btnOpen)) recalcBox.setDefaultButton(defaultRecalcButton);
            recalcBox.exec();
            if (recalcBox.clickedButton() == btnOpen) {
                recalcMode = FinanceSettingsService::MarkupRecalculationMode::OpenOrdersOnly;
            } else if (recalcBox.clickedButton() == btnNewOnly) {
                recalcMode = FinanceSettingsService::MarkupRecalculationMode::NewRowsOnly;
            } else if (recalcBox.clickedButton() == btnAll) {
                recalcMode = FinanceSettingsService::MarkupRecalculationMode::AllOrders;
            } else if (recalcBox.clickedButton() == btnNoRecalc || recalcBox.clickedButton() == nullptr) {
                recalcMode = FinanceSettingsService::MarkupRecalculationMode::NoRecalculate;
            }

            if (!saveFinanceMarkups(newStoreMarkups, newOilStoreMarkups, partsDefault, oilDefault, recalcMode, &changed, &saveErrorText)) {
                QMessageBox::warning(w, QString::fromUtf8("Помилка"),
                                     saveErrorText.isEmpty()
                                         ? QString::fromUtf8("Не вдалося зберегти та перерахувати націнки.")
                                         : saveErrorText);
                return;
            }

            if (changed) {
                SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("finance_markups_save"),
                                                                    QStringLiteral("finance_settings"),
                                                                    QStringLiteral("settings.fin.markups"),
                                                                    QString::fromUtf8("Збережено націнки для запчастин і моторного мастила"),
                                                                    {QStringLiteral("settings"), QStringLiteral("parts")});
            }
            QString doneMessage = QString::fromUtf8("Націнки для запчастин збережено без перерахунку існуючих нарядів.");
            if (recalcMode == FinanceSettingsService::MarkupRecalculationMode::OpenOrdersOnly) {
                doneMessage = QString::fromUtf8("Націнки збережено і перераховано для відкритих нарядів.");
            } else if (recalcMode == FinanceSettingsService::MarkupRecalculationMode::AllOrders) {
                doneMessage = QString::fromUtf8("Націнки збережено і перераховано для всіх активних нарядів.");
            } else if (recalcMode == FinanceSettingsService::MarkupRecalculationMode::NewRowsOnly) {
                doneMessage = QString::fromUtf8("Націнки збережено і застосовуватимуться тільки для нових запчастин.");
            }
            QMessageBox::information(w, QString::fromUtf8("Збережено"), doneMessage);
        });

        return w;
}


QWidget* buildOutsourceDefaultsWidget(QWidget* parent)
{
    return new OutsourceSettingsSectionWidget(parent);
}


} // namespace

QWidget* SettingsFinanceSection::build(const QString& id, QWidget* owner)
{
    if (id == QStringLiteral("fin.defaults")) {
        return buildFinanceDefaultsWidget(owner);
    }
    if (id == QStringLiteral("out.defaults")) {
        return buildOutsourceDefaultsWidget(owner);
    }
    return nullptr;
}
