// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

// extracted from WorkPartsDialog.cpp as part of p.4 decomposition
#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"
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

void WorkPartsDialog::autofillDraftPartsForService(int row)
{
    if (m_syncingServices || m_loadingParts || !isEnabled() || !m_servicesTable || !m_partsTable) return;
    auto* titleItem = m_servicesTable->item(row, ServiceColTitle);
    if (!titleItem || titleItem->data(RoleId).toInt() > 0 || titleItem->data(RoleIsHint).toBool()) return;
    const QString title = titleItem->text().simplified().toCaseFolded();
    if (titleItem->data(HistoryPartsDraftRoles::CheckedTitle).toString() == title) return;
    QList<WorkOrderPartRow> suggestions;
    QString error;
    if (!title.isEmpty() && !HistoryPartsAutofillService::suggest(m_orderNumber, title, &suggestions, &error)) {
        qWarning().noquote() << "History parts preview:" << error;
        return;
    }
    const QSignalBlocker servicesBlocker(m_servicesTable);
    const QSignalBlocker partsBlocker(m_partsTable);
    const QScopedValueRollback<bool> loadingGuard(m_loadingParts, true);
    QString workKey = titleItem->data(HistoryPartsDraftRoles::WorkKey).toString();
    if (workKey.isEmpty()) workKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
    titleItem->setData(HistoryPartsDraftRoles::WorkKey, workKey);
    titleItem->setData(HistoryPartsDraftRoles::CheckedTitle, title);

    const auto cells = [this](int r) {
        QStringList result;
        for (int c = PartColOrderedAt; c <= PartColAttachment; ++c) {
            auto* item = m_partsTable->item(r, c);
            result.append(item ? item->text().simplified().toCaseFolded() : QString());
        }
        auto* delivered = m_partsTable->item(r, PartColDelivered);
        result.append(delivered ? delivered->data(RoleDeliveryDate).toString() : QString());
        return result;
    };
    // Changing a draft work replaces only untouched suggestions; manual edits survive.
    for (int r = m_partsTable->rowCount() - 1; r >= 0; --r) {
        auto* name = m_partsTable->item(r, PartColName);
        if (!name || name->data(HistoryPartsDraftRoles::SourceWorkKey).toString() != workKey) continue;
        if (name->data(RoleId).toInt() <= 0 && name->data(HistoryPartsDraftRoles::OriginalCells).toStringList() == cells(r)) {
            m_partsTable->removeRow(r);
            QSet<int> shifted;
            for (int dirty : m_dirtyPartRows) {
                if (dirty != r) shifted.insert(dirty > r ? dirty - 1 : dirty);
            }
            m_dirtyPartRows = shifted;
        } else {
            name->setData(HistoryPartsDraftRoles::SourceWorkKey, QVariant());
        }
    }

    // Compare with the live table, including manual rows that are not in the DB yet.
    QList<WorkOrderPartRow> existing;
    int insertAt = 0;
    for (int r = 0; r < m_partsTable->rowCount(); ++r) {
        if (!rowHasMeaningfulPartData(m_partsTable, r)) continue;
        insertAt = r + 1;
        WorkOrderPartRow part;
        if (auto* item = m_partsTable->item(r, PartColName)) part.name = item->text();
        if (auto* item = m_partsTable->item(r, PartColOem)) part.partNumber = item->text();
        existing.append(part);
    }
    for (const auto& part : suggestions) {
        bool duplicate = false;
        for (const auto& current : existing) {
            if (HistoryPartsAutofillService::samePart(part, current)) { duplicate = true; break; }
        }
        if (duplicate) continue;
        // Insert above the trailing input/filler rows without moving the work editor's focus.
        insertEmptyPartRow(insertAt);
        clearRowHintState(m_partsTable, insertAt);
        auto* name = m_partsTable->item(insertAt, PartColName);
        name->setText(part.name);
        name->setData(HistoryPartsDraftRoles::SourceWorkKey, workKey);
        name->setData(HistoryPartsDraftRoles::SourceNote, part.note);
        m_partsTable->item(insertAt, PartColOem)->setText(part.partNumber);
        m_partsTable->item(insertAt, PartColQty)->setText(FormatUtils::smartQty(part.qty, 3));
        m_partsTable->item(insertAt, PartColPrice)->setText(QStringLiteral("0"));
        m_partsTable->item(insertAt, PartColAmount)->setText(QStringLiteral("0"));
        setRowEditingVisual(m_partsTable, insertAt);
        syncPartDeliveredCellState(m_partsTable, insertAt);
        refreshPartAttachmentCell(insertAt);
        synchronizePairedRowHeights(insertAt);
        name->setData(HistoryPartsDraftRoles::OriginalCells, cells(insertAt));
        m_dirtyPartRows.insert(insertAt++);
        existing.append(part);
    }
    renumberPartItems();
    m_userTouchedPendingContent = true;
    notifyDraftDirtyChanged();
}

void WorkPartsDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 8);
    root->setSpacing(6);

    m_lblTitle = new QLabel(this);
    m_lblTitle->setStyleSheet(QStringLiteral("color:#111111;"));
    QFont tf = interfaceHeaderFont(4);
    tf.setPointSizeF(qMax(6.0, QFontInfo(tf).pointSizeF() + 1.5));
    tf.setBold(true);
    m_lblTitle->setFont(tf);

    m_lblSub = new QLabel(this);
    m_lblSub->setStyleSheet(QStringLiteral("color:#111111;"));
    m_lblSub->setWordWrap(true);
    m_lblSub->setTextFormat(Qt::RichText);
    m_lblSub->setContentsMargins(0, 0, 0, 0);
    QFont subFont = interfaceHeaderFont(0);
    subFont.setPointSizeF(10.0);
    subFont.setBold(false);
    m_lblSub->setFont(subFont);

    m_btnRepairAi = new QPushButton(QString::fromUtf8("ШІ-підбір"), this);
    m_btnRepairAi->setCursor(Qt::PointingHandCursor);
    m_btnRepairAi->setToolTip(QString::fromUtf8("Підібрати назви робіт і запчастин за результатом діагностики. Кожну пропозицію підтверджує майстер."));

    auto* headerWrap = new QHBoxLayout();
    headerWrap->setContentsMargins(0, 0, 0, 0);
    headerWrap->setSpacing(12);
    auto* headerText = new QFrame(this);
    headerText->setObjectName("orderSummaryFrame");
    headerText->setStyleSheet("QFrame#orderSummaryFrame { border: 1px solid black; background: transparent; }");
    auto* headerTextWrap = new QVBoxLayout(headerText);
    headerTextWrap->setContentsMargins(4, 2, 4, 2);
    headerTextWrap->setSpacing(2);
    auto* titleRow = new QHBoxLayout;
    titleRow->addWidget(m_lblTitle);
    m_settlement = new QLabel(headerText);
    m_settlement->setObjectName("orderSettlement");
    m_settlement->setTextFormat(Qt::PlainText);
    m_settlement->setStyleSheet("QLabel#orderSettlement {color:#ffffff;background:transparent;border:none;}");
    auto settlementFont = interfaceHeaderFont(0);
    settlementFont.setBold(true);
    settlementFont.setUnderline(true);
    m_settlement->setFont(settlementFont);
    m_settlement->setAlignment(Qt::AlignCenter);
    titleRow->addWidget(m_settlement, 1);
    headerTextWrap->addLayout(titleRow);
    headerTextWrap->addWidget(m_lblSub);
    headerWrap->addWidget(headerText, 1);

    auto* dialogButtonsWrap = new QHBoxLayout();
    dialogButtonsWrap->setContentsMargins(0, 0, 0, 0);
    dialogButtonsWrap->setSpacing(8);
    m_btnDialogOk = new QPushButton(QStringLiteral("OK"), this);
    m_btnDialogCancel = new QPushButton(QString::fromUtf8("Скасувати"), this);
    for (auto* btn : {m_btnDialogOk, m_btnDialogCancel}) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setMinimumHeight(AdaptiveUi::px(btn, 34));
        btn->setMinimumWidth(AdaptiveUi::px(btn, 118));
        btn->setStyleSheet(glossyActionButtonStyle(false, true)+QStringLiteral("QPushButton { font-family:'Times New Roman'; font-weight:normal; }"));
    }
    m_btnDialogOk->setMinimumWidth(AdaptiveUi::px(m_btnDialogOk, 110));
    m_btnDialogCancel->setMinimumWidth(AdaptiveUi::px(m_btnDialogCancel, 138));
    dialogButtonsWrap->addWidget(m_btnDialogOk);
    dialogButtonsWrap->addWidget(m_btnDialogCancel);
    headerWrap->addLayout(dialogButtonsWrap, 0);
    root->addLayout(headerWrap);

    auto* controlsHost=new QWidget(this);
    controlsHost->setObjectName("workOrderToolbar");
    auto* controlsRow = new QHBoxLayout(controlsHost);
    controlsRow->setContentsMargins(0, 0, 0, 0);
    controlsRow->setSpacing(0);

    auto* masterLbl = new QLabel(QString::fromUtf8("Майстер:"), this);
    masterLbl->setStyleSheet(QString::fromUtf8("color:%1;").arg(mainDialogTextColor()));
    QFont lf = interfaceHeaderFont(0);
    lf.setBold(true);
    masterLbl->setFont(lf);
    masterLbl->setFixedWidth(masterLbl->fontMetrics().horizontalAdvance(masterLbl->text())+2);

    // Static master controls geometry matched to the reference screenshot.
    // The save/import buttons deliberately use the same reference size as
    // "Закрити наряд".  The master combobox is shortened so the save button
    // ends on the parts-table split boundary with a compact 2-3 mm gap.
    constexpr int kMasterComboFixedWidthPx = 368;
    constexpr int kMasterComboFixedHeightPx = 44;
    constexpr int kTopActionReferenceWidthPx = 150;
    constexpr int kTopActionReferenceHeightPx = 42;
    constexpr int kSaveMasterFixedWidthPx = kTopActionReferenceWidthPx;
    constexpr int kSaveMasterFixedHeightPx = kTopActionReferenceHeightPx;
    constexpr int kRepairAiFixedWidthPx = kTopActionReferenceWidthPx - 30;
    constexpr int kRepairAiFixedHeightPx = kTopActionReferenceHeightPx;
    constexpr int kImportInvoiceFixedWidthPx = kTopActionReferenceWidthPx;
    constexpr int kImportInvoiceFixedHeightPx = kTopActionReferenceHeightPx;

    m_masterCombo = createWorkPartsArrowCombo(this);
    m_masterCombo->setFixedSize(AdaptiveUi::px(m_masterCombo, kMasterComboFixedWidthPx),
                                AdaptiveUi::px(m_masterCombo, kMasterComboFixedHeightPx));
    m_masterCombo->setStyleSheet(whiteMasterComboStyle());
    m_btnSaveMaster = new QPushButton(QString::fromUtf8("Зберегти майстра"), this);
    m_btnCustomerParts = new QPushButton(QString::fromUtf8("З/Ч клієнта"), this);
    m_btnCustomerParts->setObjectName("customerPartsMode");
    m_btnCustomerParts->setCheckable(true);
    m_btnCustomerParts->setToolTip(QString::fromUtf8("Нові запчастини надані клієнтом: без ціни та списання зі складу."));
    connect(m_btnCustomerParts, &QPushButton::toggled, this, [this](bool enabled) {
        if (!m_partsTable) return;
        const QSignalBlocker blocker(m_partsTable);
        for (int row = 0; row < m_partsTable->rowCount(); ++row) {
            auto* name = m_partsTable->item(row, PartColName);
            if (name && !name->text().trimmed().isEmpty() && !rowHasHintMarker(m_partsTable, row))
                name->setData(Qt::UserRole + 420, true);
        }
        m_partsTable->setProperty("csCustomerPartsMode", enabled);
    });
    m_btnPartPercent = new QPushButton(QString::fromUtf8("%"), this);
    m_btnOutsource = new QPushButton(QString::fromUtf8("Аутсорс"), this);
    m_btnPdf = new QPushButton(QString::fromUtf8("PDF"), this);
    m_btnCloseOrder = new QPushButton(QString::fromUtf8("Закрити наряд"), this);
    m_btnImportExcel = new QPushButton(QString::fromUtf8("Імпорт накладної"), this);
    const QString topActionButtonCss = glossyActionButtonStyle(false, true) + QStringLiteral(
        "QPushButton { font-family:'Times New Roman'; font-weight:normal; padding:4px 6px 5px 6px; } QPushButton:pressed { padding:6px 4px 3px 8px; }");
    for (auto* btn : {m_btnSaveMaster, m_btnPartPercent, m_btnOutsource, m_btnPdf, m_btnCloseOrder, m_btnImportExcel, m_btnRepairAi, m_btnCustomerParts}) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setMinimumHeight(AdaptiveUi::px(btn, 30));
        btn->setStyleSheet(topActionButtonCss);
    }
    m_btnSaveMaster->setFixedSize(AdaptiveUi::px(m_btnSaveMaster, kSaveMasterFixedWidthPx),
                                  AdaptiveUi::px(m_btnSaveMaster, kSaveMasterFixedHeightPx));
    m_btnRepairAi->setFixedSize(AdaptiveUi::px(m_btnRepairAi, kRepairAiFixedWidthPx),
                                AdaptiveUi::px(m_btnRepairAi, kRepairAiFixedHeightPx));
    m_btnCustomerParts->setFixedSize(AdaptiveUi::px(this, 140), AdaptiveUi::px(this, kTopActionReferenceHeightPx));
    m_btnCustomerParts->setStyleSheet(topActionButtonCss + QStringLiteral(
        "QPushButton:checked { color:white; border:1px solid #f5aaaa;"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #eb6666,stop:0.45 #c51d2b,stop:1 #790610); }"));
    m_btnPartPercent->setMinimumWidth(AdaptiveUi::px(m_btnPartPercent, 84));
    m_btnOutsource->setMinimumWidth(AdaptiveUi::px(m_btnOutsource, 102));
    m_btnPdf->setMinimumWidth(AdaptiveUi::px(m_btnPdf, 84));
    m_btnCloseOrder->setFixedSize(AdaptiveUi::px(m_btnCloseOrder, kTopActionReferenceWidthPx),
                                  AdaptiveUi::px(m_btnCloseOrder, kTopActionReferenceHeightPx));
    m_btnImportExcel->setFixedSize(AdaptiveUi::px(m_btnImportExcel, kImportInvoiceFixedWidthPx),
                                   AdaptiveUi::px(m_btnImportExcel, kImportInvoiceFixedHeightPx));

    auto* masterControls = controlsRow;
    const int buttonGap = qMax(1, qRound(logicalDpiX() * 0.5 / 25.4));
    // Size by the actual styled text, not reference widths that force wrapping.
    for(auto* button:{m_btnSaveMaster,m_btnRepairAi,m_btnCustomerParts,m_btnPartPercent,
                     m_btnOutsource,m_btnPdf,m_btnCloseOrder,m_btnImportExcel}) {
        button->ensurePolished();
        const int readableWidth=button->fontMetrics().horizontalAdvance(button->text())+16;
        button->setMinimumWidth(readableWidth);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    }
    masterControls->setContentsMargins(0, 0, 0, 0);
    masterControls->setSpacing(buttonGap);
    m_masterCombo->setObjectName("orderPrimaryMaster");
    const int masterReduction=qRound(logicalDpiX()*15.0/25.4);
    m_masterCombo->setMinimumWidth(AdaptiveUi::px(this,180));
    m_masterCombo->setMaximumWidth(AdaptiveUi::px(this,kMasterComboFixedWidthPx)-masterReduction);
    m_masterCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    masterControls->addWidget(masterLbl, 0, Qt::AlignVCenter);
    masterControls->addWidget(m_masterCombo, 0, Qt::AlignVCenter);
    masterControls->addWidget(m_btnSaveMaster, 1, Qt::AlignVCenter);
    auto* postLabel=new QLabel(QString::fromUtf8("Пост №"),this);
    postLabel->setFont(lf);postLabel->setStyleSheet(masterLbl->styleSheet());
    postLabel->setFixedWidth(postLabel->fontMetrics().horizontalAdvance(postLabel->text())+2);
    masterControls->addWidget(postLabel,0,Qt::AlignVCenter);
    auto* postCombo=createWorkPartsArrowCombo(this);
    postCombo->setObjectName("orderWorkshopPost");
    postCombo->setStyleSheet(whiteMasterComboStyle()+QStringLiteral(
        "QComboBox { padding:4px 20px 4px 5px; } QComboBox:focus { padding:3px 19px 3px 4px; } QComboBox::drop-down { width:16px; }"));
    postCombo->addItems({QString::fromUtf8("—"),"1","2","3"});
    postCombo->setItemData(1,QString::fromUtf8("Пост 1 — авто"),Qt::ToolTipRole);
    postCombo->setItemData(2,QString::fromUtf8("Пост 2 — авто"),Qt::ToolTipRole);
    postCombo->setItemData(3,QString::fromUtf8("Пост 3 — двигуни та КПП"),Qt::ToolTipRole);
    postCombo->setFixedSize(qRound(logicalDpiX()*15.0/25.4),AdaptiveUi::px(this,kMasterComboFixedHeightPx));
    postCombo->addItem("?");
    auto updatePost=[this,postCombo]{const int post=BaySnapshot::currentPosts().value(currentOrderUid(),-1);postCombo->setCurrentIndex(post<0?4:post);};
    updatePost();
    auto* bayTimer=new QTimer(this);
    auto refreshPost=[this,updatePost]{if(isVisible()) BaySnapshot::refresh(this,updatePost);};
    connect(bayTimer,&QTimer::timeout,this,refreshPost);bayTimer->start(30000);QTimer::singleShot(0,this,refreshPost);
    postCombo->setToolTip(QString::fromUtf8("Фактичний пост: ? — стан невідомий; — — поза постом. Зміна потребує підтвердження сервера та збереження."));
    connect(postCombo,qOverload<int>(&QComboBox::activated),this,[this,updatePost](int post){
        updatePost();
        BayOccupancyPage::open(this,currentOrderUid(),post==4?-1:post);
        updatePost();
        refreshAfterMutation(true,false,false,false);
    });
    masterControls->addWidget(postCombo,0,Qt::AlignVCenter);
    masterControls->addWidget(m_btnRepairAi, 1, Qt::AlignVCenter);
    masterControls->addWidget(m_btnCustomerParts, 1, Qt::AlignVCenter);

    auto* actionButtons = controlsRow;
    actionButtons->setContentsMargins(0, 0, 0, 0);
    actionButtons->setSpacing(buttonGap);
    actionButtons->addWidget(m_btnPartPercent, 1, Qt::AlignVCenter);
    actionButtons->addWidget(m_btnOutsource, 1, Qt::AlignVCenter);
    actionButtons->addWidget(m_btnPdf, 1, Qt::AlignVCenter);
    actionButtons->addWidget(m_btnCloseOrder, 1, Qt::AlignVCenter);
    actionButtons->addWidget(m_btnImportExcel, 1, Qt::AlignVCenter);

    dialogButtonsWrap->setSpacing(buttonGap);
    // The two upper actions track the final layout widths of the matching toolbar actions.
    class MatchButtonSize final : public QObject {
    public:
        explicit MatchButtonSize(QPushButton* target) : QObject(target), target(target) {}
        bool eventFilter(QObject* watched, QEvent* event) override {
            if (event->type() == QEvent::Resize)
                target->setFixedSize(static_cast<QWidget*>(watched)->size());
            return false;
        }
        QPushButton* target;
    };
    m_btnCloseOrder->installEventFilter(new MatchButtonSize(m_btnDialogOk));
    m_btnImportExcel->installEventFilter(new MatchButtonSize(m_btnDialogCancel));
    root->addWidget(controlsHost);

    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->setChildrenCollapsible(false);
    root->addWidget(m_split, 1);

    auto makePaneTitle = [](const QString& text, QWidget* parent) {
        auto* lbl = new QLabel(text, parent);
        QFont f = interfaceHeaderFont(0);
        f.setBold(true);
        lbl->setFont(f);
        return lbl;
    };

    auto* left = new QFrame(this);
    left->setObjectName("workPane");
    auto* leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0,0,0,0);
    leftLay->setSpacing(6);
    auto* serviceHeader = new QHBoxLayout();
    serviceHeader->setContentsMargins(18,0,18,0);
    serviceHeader->setSpacing(10);
    serviceHeader->addWidget(makePaneTitle(QString::fromUtf8("Виконання робіт"), left));
    serviceHeader->addStretch(1);
    m_btnAddService = new QPushButton(QString::fromUtf8("Додати роботу"), left);
    m_btnEditService = new QPushButton(QString::fromUtf8("Редагувати"), left);
    m_btnAddService->setStyleSheet(glossyActionButtonStyle());
    m_btnEditService->setStyleSheet(glossyActionButtonStyle());
    m_btnAddService->hide();
    m_btnEditService->hide();
    leftLay->addLayout(serviceHeader);

    m_servicesTable = new QTableWidget(0, 6, left);
    m_servicesTable->setObjectName(QStringLiteral("orderServicesTable"));
    m_servicesTable->setProperty("cs_order_number", m_orderNumber);
    m_servicesTable->setProperty("csWorkPartsTableRevision", QVariant::fromValue<qulonglong>(m_tableRevision));
    m_servicesTable->setHorizontalHeaderLabels({QString::fromUtf8("№"), QString::fromUtf8("Робота"), QString::fromUtf8("Ціна"), QString::fromUtf8("Кіл-ть"), QString::fromUtf8("Майстер"), QString::fromUtf8("📎")});
    if (auto* workHeader = m_servicesTable->horizontalHeaderItem(ServiceColTitle)) {
        workHeader->setToolTip(QString::fromUtf8("Колонка підтримує автонормалізацію української технічної мови та переклад назв робіт з російської на українську після завершення редагування."));
    }
    m_servicesTable->verticalHeader()->setVisible(false);
    m_servicesTable->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_servicesTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_servicesTable->verticalHeader()->setDefaultSectionSize(AdaptiveUi::px(m_servicesTable, kDefaultTableRowHeight));
    m_servicesTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_servicesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_servicesTable->setContextMenuPolicy(Qt::CustomContextMenu);
    if (m_servicesTable->viewport()) m_servicesTable->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_servicesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_servicesTable->setTabKeyNavigation(false);
    m_servicesTable->horizontalHeader()->setSectionsMovable(true);
    m_servicesTable->horizontalHeader()->setStretchLastSection(false);
    m_servicesTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_servicesTable->horizontalHeader()->setMinimumSectionSize(12);
    m_servicesTable->horizontalHeader()->setMinimumHeight(AdaptiveUi::px(m_servicesTable, 52));
    m_servicesTable->horizontalHeader()->setFixedHeight(AdaptiveUi::px(m_servicesTable, 52));
    for (int i = 0; i < m_servicesTable->columnCount(); ++i) m_servicesTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Interactive);
    m_servicesTable->setColumnWidth(ServiceColIndex, 49);
    m_servicesTable->setColumnWidth(ServiceColTitle, 339);
    m_servicesTable->setColumnWidth(ServiceColPrice, 86);
    m_servicesTable->setColumnWidth(ServiceColQty, 62);
    m_servicesTable->setColumnWidth(ServiceColMaster, 150);
    m_servicesTable->setColumnWidth(ServiceColAttachment, 54);
    {
        const QFont headerFont = interfaceHeaderFont();
        m_servicesTable->horizontalHeader()->setFont(headerFont);
        QFont compactHeaderFont = headerFont;
        compactHeaderFont.setPointSizeF(qMax(6.0, QFontInfo(headerFont).pointSizeF() - 2.0));
        for (int col = 0; col < m_servicesTable->columnCount(); ++col) {
            if (auto* headerItem = m_servicesTable->horizontalHeaderItem(col)) {
                headerItem->setFont(col == ServiceColQty ? compactHeaderFont : headerFont);
                headerItem->setTextAlignment(Qt::AlignCenter);
            }
        }
    }
    styleDarkFlatTable(m_servicesTable);
    m_servicesTable->setItemDelegate(createWorkPartsItemDelegate(m_servicesTable));
    m_servicesTable->installEventFilter(createWorkPartsKeyboardNavigator(m_servicesTable));
    leftLay->addWidget(m_servicesTable, 1);

    auto* right = new QFrame(this);
    right->setObjectName("partsPane");
    auto* rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0,0,0,0);
    rightLay->setSpacing(6);
    auto* partHeader = new QHBoxLayout();
    partHeader->setContentsMargins(18,0,18,0);
    partHeader->setSpacing(10);
    partHeader->addWidget(makePaneTitle(QString::fromUtf8("Запчастини"), right));
    partHeader->addStretch(1);
    m_btnAddPart = new QPushButton(QString::fromUtf8("Додати запчастину"), right);
    m_btnEditPart = new QPushButton(QString::fromUtf8("Редагувати"), right);
    m_btnAddPart->setStyleSheet(glossyActionButtonStyle());
    m_btnEditPart->setStyleSheet(glossyActionButtonStyle());
    m_btnAddPart->hide();
    m_btnEditPart->hide();
    rightLay->addLayout(partHeader);

    m_partsTable = new QTableWidget(0, 12, right);
    m_partsTable->setObjectName(QStringLiteral("orderPartsTable"));
    m_partsTable->setHorizontalHeaderLabels({
        QString::fromUtf8("№"),
        QString::fromUtf8("✓"),
        QString::fromUtf8("Дата\nзамовл."),
        QString::fromUtf8("OEM\nномер"),
        QString::fromUtf8("Запчастина"),
        QString::fromUtf8("Кіл-\nть"),
        QString::fromUtf8("Ціна"),
        QString::fromUtf8("Вартість"),
        QString::fromUtf8("Магазин"),
        QString::fromUtf8("Бренд"),
        QString::fromUtf8("№-Н"),
        QString::fromUtf8("📎")});
    m_partsTable->verticalHeader()->setVisible(false);
    m_partsTable->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_partsTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_partsTable->verticalHeader()->setDefaultSectionSize(AdaptiveUi::px(m_partsTable, kDefaultTableRowHeight));
    m_partsTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_partsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_partsTable->setProperty("cs_order_number", m_orderNumber);
    m_partsTable->setProperty("csWorkPartsTableRevision", QVariant::fromValue<qulonglong>(m_tableRevision));
    m_partsTable->setProperty("csWorkPartsTableRevision", QVariant::fromValue<qulonglong>(m_tableRevision));
    m_partsTable->setContextMenuPolicy(Qt::CustomContextMenu);
    if (m_partsTable->viewport()) m_partsTable->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_partsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_partsTable->setTabKeyNavigation(false);
    m_partsTable->horizontalHeader()->setSectionsMovable(true);
    m_partsTable->horizontalHeader()->setStretchLastSection(false);
    m_partsTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_partsTable->horizontalHeader()->setMinimumSectionSize(12);
    m_partsTable->horizontalHeader()->setMinimumHeight(AdaptiveUi::px(m_partsTable, 52));
    m_partsTable->horizontalHeader()->setFixedHeight(AdaptiveUi::px(m_partsTable, 52));
    for (int i = 0; i < m_partsTable->columnCount(); ++i) m_partsTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Interactive);
    m_partsTable->setColumnWidth(PartColIndex, 49);        // №
    m_partsTable->setColumnWidth(PartColDelivered, 46);    // чекбокс доставки
    m_partsTable->setColumnWidth(PartColOrderedAt, 73);    // Дата замовл.
    m_partsTable->setColumnWidth(PartColOem, 67);          // OEM номер
    m_partsTable->setColumnWidth(PartColName, 248);        // Запчастина
    m_partsTable->setColumnWidth(PartColQty, 43);          // Кіл-ть
    m_partsTable->setColumnWidth(PartColPrice, 63);        // Ціна
    m_partsTable->setColumnWidth(PartColAmount, 62);       // Вартість
    m_partsTable->setColumnWidth(PartColStore, 136);       // Магазин
    m_partsTable->setColumnWidth(PartColBrand, 118);       // Бренд
    m_partsTable->setColumnWidth(PartColInvoiceNumber, 88); // №-Н
    m_partsTable->setColumnWidth(PartColAttachment, 54);   // вкладення
    {
        const QFont headerFont = interfaceHeaderFont();
        m_partsTable->horizontalHeader()->setFont(headerFont);
        QFont compactHeaderFont = headerFont;
        compactHeaderFont.setPointSizeF(qMax(6.0, QFontInfo(headerFont).pointSizeF() - 2.0));
        for (int col = 0; col < m_partsTable->columnCount(); ++col) {
            if (auto* headerItem = m_partsTable->horizontalHeaderItem(col)) {
                const bool compactHeader = (col == PartColQty || col == PartColAmount);
                headerItem->setFont(compactHeader ? compactHeaderFont : headerFont);
                headerItem->setTextAlignment(Qt::AlignCenter);
            }
        }
    }
    styleDarkFlatTable(m_partsTable);
    m_partsTable->setStyleSheet(m_partsTable->styleSheet() + QStringLiteral(
        "QTableWidget::indicator { width: 22px; height: 22px; border: 2px solid #102a43; "
        "border-radius: 4px; background: #ffffff; }"
        "QTableWidget::indicator:checked { background: #0d9c5f; border-color: #073b28; }"
        "QTableWidget::indicator:disabled { background: #d9e2ec; border-color: #627d98; }"));
    m_partsTable->setItemDelegate(createWorkPartsItemDelegate(m_partsTable));
    m_partsTable->installEventFilter(createWorkPartsKeyboardNavigator(m_partsTable));
    m_partsTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightLay->addWidget(m_partsTable, 1);

    m_split->addWidget(left);
    m_split->addWidget(right);
    m_split->setStretchFactor(0, 0);
    m_split->setStretchFactor(1, 0);
    m_split->setHandleWidth(1);

    constexpr int kDefaultLeftPaneWidth = 765;
    constexpr int kDefaultRightPaneWidth = 1220;
    left->setMinimumWidth(AdaptiveUi::px(left, 440));
    right->setMinimumWidth(AdaptiveUi::px(right, 520));
    const bool lightUi = ThemeManager::currentTheme() == QStringLiteral("light");
    const QString paneLabelColor = lightUi ? QStringLiteral("#0d1014") : QStringLiteral("#ffffff");
    m_split->setStyleSheet(QString::fromUtf8(
        "QSplitter::handle { background: rgba(255,255,255,0.18); }"
        "QFrame#workPane, QFrame#partsPane {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QFrame#workPane QLabel, QFrame#partsPane QLabel { color: %1; background: transparent; }"
        "QFrame#workPane QTableWidget, QFrame#partsPane QTableWidget { margin-top: 0px; }"
    ).arg(paneLabelColor));
    restoreHeaderState(m_servicesTable->horizontalHeader(), kWorksHeaderStateKey);
    restoreHeaderState(m_partsTable->horizontalHeader(), kPartsHeaderStateKey);
    enforcePersistentInteractiveColumnWidths(m_servicesTable, kWorksColumnWidthsKey);
    enforcePersistentInteractiveColumnWidths(m_partsTable, kPartsColumnWidthsKey);
    m_servicesTable->setColumnHidden(ServiceColIndex, false);
    const int serviceIndexVisual = m_servicesTable->horizontalHeader()->visualIndex(ServiceColIndex);
    if (serviceIndexVisual > 0) {
        m_servicesTable->horizontalHeader()->moveSection(serviceIndexVisual, 0);
    }
    m_partsTable->setColumnHidden(PartColIndex, false);
    const int indexVisual = m_partsTable->horizontalHeader()->visualIndex(PartColIndex);
    if (indexVisual > 0) {
        m_partsTable->horizontalHeader()->moveSection(indexVisual, 0);
    }

    AdaptiveUi::installAdaptiveTable(m_servicesTable, {
        {ServiceColIndex, 42, 49, 0, 70, true},
        {ServiceColTitle, 220, 340, 6, 560, false},
        {ServiceColPrice, 78, 92, 1, 130, false},
        {ServiceColQty, 58, 66, 1, 96, false},
        {ServiceColMaster, 140, 180, 2, 280, false},
        {ServiceColAttachment, 48, 54, 0, 72, true}
    }, QStringLiteral("ui.workparts.services.adaptive"));
    AdaptiveUi::installAdaptiveTable(m_partsTable, {
        {PartColIndex, 42, 49, 0, 70, true},
        {PartColDelivered, 42, 46, 0, 64, true},
        {PartColOrderedAt, 68, 84, 1, 120, false},
        {PartColOem, 92, 120, 2, 180, false},
        {PartColName, 220, 320, 6, 520, false},
        {PartColQty, 48, 54, 1, 84, false},
        {PartColPrice, 62, 74, 1, 110, false},
        {PartColAmount, 70, 82, 1, 120, false},
        {PartColStore, 118, 150, 2, 220, false},
        {PartColBrand, 104, 130, 1, 180, false},
        {PartColInvoiceNumber, 82, 96, 1, 140, false},
        {PartColAttachment, 48, 54, 0, 72, true}
    }, QStringLiteral("ui.workparts.parts.adaptive"));

    restoreHeaderFixedWidth(m_servicesTable->verticalHeader(), kWorksRowHeaderWidthKey, 34);
    restoreSplitterState(m_split, kSplitterStateKey);
    if (UiStateSettingsService::loadString(kSplitterStateKey, QString()).trimmed().isEmpty()) {
        m_split->setSizes({kDefaultLeftPaneWidth, kDefaultRightPaneWidth});
    }

    Q_UNUSED(masterLbl);

    bindHeaderStatePersistence(m_servicesTable->horizontalHeader(), kWorksHeaderStateKey);
    bindHeaderStatePersistence(m_partsTable->horizontalHeader(), kPartsHeaderStateKey);
    bindTableColumnWidthPersistence(m_servicesTable, kWorksColumnWidthsKey);
    bindTableColumnWidthPersistence(m_partsTable, kPartsColumnWidthsKey);
    bindHeaderFixedWidthPersistence(m_servicesTable->verticalHeader(), kWorksRowHeaderWidthKey);
    bindSplitterStatePersistence(m_split, kSplitterStateKey);
    connect(m_partsTable->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logicalIndex, int, int) {
        if (logicalIndex == PartColName || logicalIndex == PartColStore
            || logicalIndex == PartColBrand || logicalIndex == PartColInvoiceNumber
            || logicalIndex == PartColOem) {
            if (!property("csPartsResizeRefreshQueued").toBool()) {
                setProperty("csPartsResizeRefreshQueued", true);
                QPointer<WorkPartsDialog> guard(this);
                QTimer::singleShot(0, this, [guard]() {
                    if (!guard || !guard->m_partsTable) return;
                    guard->setProperty("csPartsResizeRefreshQueued", false);
                    refreshCompactMainPartsCellFonts(guard->m_partsTable);
                    guard->synchronizeAllPairedRowHeights();
                });
            }
        }
        if (logicalIndex == PartColIndex) {
            if (auto* header = m_partsTable ? m_partsTable->horizontalHeader() : nullptr) {
                const bool enforcing = header->property("csEnforcingPartIndexWidth").toBool();
                const int currentWidth = m_partsTable->columnWidth(PartColIndex);
                const int desiredWidth = qMax(49, currentWidth);
                if (!enforcing && currentWidth < desiredWidth) {
                    header->setProperty("csEnforcingPartIndexWidth", true);
                    const QSignalBlocker blocker(header);
                    m_partsTable->setColumnWidth(PartColIndex, desiredWidth);
                    header->setProperty("csEnforcingPartIndexWidth", false);
                }
            }
            if (auto* hbar = m_partsTable->horizontalScrollBar()) hbar->setValue(hbar->minimum());
        }
    });
    QPointer<WorkPartsDialog> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (!self) return;
        self->synchronizeAllPairedRowHeights();
        enforcePersistentInteractiveColumnWidths(self->m_servicesTable, QString::fromLatin1(kWorksColumnWidthsKey));
        enforcePersistentInteractiveColumnWidths(self->m_partsTable, QString::fromLatin1(kPartsColumnWidthsKey));
        self->persistUiLayoutState();
    });

    connect(m_btnDialogOk, &QPushButton::clicked, this, [this]() {
        if (property("csImportBusy").toBool() || property("csImportApplyingRows").toBool()
            || (m_partsTable && (m_partsTable->property("csImportBusy").toBool() || m_partsTable->property("csImportApplyingRows").toBool()))) {
            QMessageBox::warning(this,
                                 QString::fromUtf8("Імпорт накладної"),
                                 QString::fromUtf8("Дочекайтеся повного завершення імпорту накладної перед натисканням OK."));
            return;
        }
        auto* dlg = qobject_cast<QDialog*>(window());
        if (!dlg) return;
        if (dlg->property("csAcceptQueued").toBool()) {
            qWarning() << "WorkPartsDialog OK click ignored: accept already queued order" << m_orderNumber;
            return;
        }
        auto deferAccept = [this, dlg](int delayMs, const char* reason) {
            dlg->setProperty("csAcceptQueued", true);
            if (m_btnDialogOk) m_btnDialogOk->setEnabled(false);
            qWarning() << "WorkPartsDialog deferred OK accept order" << m_orderNumber << "reason" << reason << "delay_ms" << delayMs;
            QPointer<WorkPartsDialog> self(this);
            QPointer<QDialog> dialogGuard(dlg);
            QTimer::singleShot(delayMs, this, [self, dialogGuard]() {
                if (!self) return;
                if (dialogGuard) {
                    dialogGuard->setProperty("csAcceptQueued", false);
                }
                if (self->m_btnDialogOk) self->m_btnDialogOk->setEnabled(true);
                qCDebug(csWorkPartsTraceLog) << "WorkPartsDialog queued requestOk order" << self->m_orderNumber;
                emit self->requestOk();
            });
        };
        if (QWidget* popup = QApplication::activePopupWidget()) {
            popup->close();
            deferAccept(75, "active_popup");
            return;
        }
        flushInlineEditorValueToModel(m_servicesTable);
        flushInlineEditorValueToModel(m_partsTable);
        if (m_servicesTable) m_servicesTable->clearFocus();
        if (m_partsTable) m_partsTable->clearFocus();
        if (QWidget* fw = focusWidget()) fw->clearFocus();
        resetInlineEditorStateForTable(m_servicesTable, "ok_before_accept");
        resetInlineEditorStateForTable(m_partsTable, "ok_before_accept");
        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS editor_state_clean_before_ok order=") << m_orderNumber;
        deferAccept(0, "normal");
    });
    connect(m_btnDialogCancel, &QPushButton::clicked, this, [this]() {
        if (auto* dlg = qobject_cast<QDialog*>(window())) {
            dlg->reject();
        }
    });

    auto importInteractionBusy = [this]() -> bool {
        return property("csImportBusy").toBool() || property("csImportApplyingRows").toBool()
            || (m_partsTable && (m_partsTable->property("csImportBusy").toBool() || m_partsTable->property("csImportApplyingRows").toBool()));
    };

    auto ensureNoActivePopupOrInlineEditor = [this](const char* actionName, std::function<void()> action) {
        if (!action) return;
        if (QWidget* popup = QApplication::activePopupWidget()) {
            popup->close();
            QPointer<WorkPartsDialog> self(this);
            QTimer::singleShot(75, this, [self, actionName, action]() {
                if (!self) return;
                qCDebug(csWorkPartsTraceLog) << "WorkPartsDialog deferred action resumed" << actionName << "order" << self->m_orderNumber;
                action();
            });
            return;
        }
        auto resetInlineEditorFlagsForTable = [this, actionName](QTableWidget* table, const char* reason, bool forceEvenIfVisible) {
            if (!table || !table->property("inlineEditorActive").toBool()) return false;
            bool hasVisibleViewportEditor = false;
            if (table->viewport()) {
                const auto widgets = table->viewport()->findChildren<QWidget*>(QString(), Qt::FindChildrenRecursively);
                for (QWidget* widget : widgets) {
                    if (!widget) continue;
                    if (!(qobject_cast<QLineEdit*>(widget) || qobject_cast<QComboBox*>(widget))) continue;
                    if (!widget->isVisible()) continue;
                    hasVisibleViewportEditor = true;
                    widget->clearFocus();
                }
            }
            if (hasVisibleViewportEditor && !forceEvenIfVisible) {
                return false;
            }
            qWarning() << "WorkPartsDialog inline editor state reset order" << m_orderNumber
                       << "action" << actionName
                       << "table" << (table->objectName().isEmpty() ? QStringLiteral("<unnamed>") : table->objectName())
                       << "reason" << reason
                       << "force" << forceEvenIfVisible;
            table->setProperty("inlineEditorActive", false);
            clearInlineEditingIndex(table);
            table->viewport()->update();
            return true;
        };
        auto normalizeInlineEditorState = [&](bool forceReset) {
            flushInlineEditorValueToModel(m_servicesTable);
            flushInlineEditorValueToModel(m_partsTable);
            if (m_servicesTable) {
                m_servicesTable->clearFocus();
                if (m_servicesTable->property("inlineEditorActive").toBool()) {
                    resetInlineEditorFlagsForTable(m_servicesTable, forceReset ? "forced_action_retry_limit" : "stale_after_flush", forceReset);
                }
            }
            if (m_partsTable) {
                m_partsTable->clearFocus();
                if (m_partsTable->property("inlineEditorActive").toBool()) {
                    resetInlineEditorFlagsForTable(m_partsTable, forceReset ? "forced_action_retry_limit" : "stale_after_flush", forceReset);
                }
            }
            if (QWidget* fw = focusWidget()) fw->clearFocus();
            return (m_servicesTable && m_servicesTable->property("inlineEditorActive").toBool())
                || (m_partsTable && m_partsTable->property("inlineEditorActive").toBool());
        };
        bool inlineEditorActive = normalizeInlineEditorState(false);
        if (inlineEditorActive) {
            const QString retryKey = QStringLiteral("csInlineActionRetry_") + QString::fromLatin1(actionName);
            int retryCount = property(retryKey.toUtf8().constData()).toInt() + 1;
            setProperty(retryKey.toUtf8().constData(), retryCount);
            if (retryCount >= 8) {
                inlineEditorActive = normalizeInlineEditorState(true);
                if (!inlineEditorActive) {
                    setProperty(retryKey.toUtf8().constData(), 0);
                }
            }
            if (inlineEditorActive) {
                QPointer<WorkPartsDialog> self(this);
                QTimer::singleShot(75, this, [self, actionName, action]() {
                    if (!self) return;
                    qCDebug(csWorkPartsTraceLog) << "WorkPartsDialog deferred inline-editor action resumed" << actionName << "order" << self->m_orderNumber;
                    action();
                });
                return;
            }
            setProperty(retryKey.toUtf8().constData(), 0);
        }
        action();
    };

    connect(m_btnSaveMaster, &QPushButton::clicked, this, [this] { savePrimaryMaster(); });
    connect(m_btnPartPercent, &QPushButton::clicked, this, [this, importInteractionBusy, ensureNoActivePopupOrInlineEditor] {
        if (importInteractionBusy()) {
            QMessageBox::warning(this,
                                 QString::fromUtf8("Імпорт накладної"),
                                 QString::fromUtf8("Дочекайтеся повного завершення імпорту накладної перед зміною відсотка запчастини."));
            return;
        }
        ensureNoActivePopupOrInlineEditor("part_percent", [this]() {
            applySelectedPartMarkupPercent();
        });
    });
    connect(m_btnOutsource, &QPushButton::clicked, this, [this, importInteractionBusy, ensureNoActivePopupOrInlineEditor] {
        if (importInteractionBusy()) {
            QMessageBox::warning(this,
                                 QString::fromUtf8("Імпорт накладної"),
                                 QString::fromUtf8("Дочекайтеся повного завершення імпорту накладної перед відкриттям аутсорсу."));
            return;
        }
        ensureNoActivePopupOrInlineEditor("outsource", [this, importInteractionBusy]() {
            if (importInteractionBusy()) {
                QMessageBox::warning(this,
                                     QString::fromUtf8("Імпорт накладної"),
                                     QString::fromUtf8("Дочекайтеся повного завершення імпорту накладної перед відкриттям аутсорсу."));
                return;
            }
            QElapsedTimer timer;
            timer.start();
            QString prepError;
            if (!ensureWorkPartsDialogDbReady(this, false, &prepError)) {
                return;
            }
            QString routeError;
            if (!ensureOrderReadyForEditing(m_orderNumber, &routeError)) {
                QMessageBox::critical(this,
                                      QString::fromUtf8("Помилка"),
                                      QString::fromUtf8("Наряд не готовий до редагування аутсорсу.\n%1").arg(routeError));
                return;
            }

            QString pendingError;
            if (!commitPendingEdits(&pendingError, true)) {
                QMessageBox::critical(this, QString::fromUtf8("Помилка"), pendingError);
                return;
            }

            OutsourceCompanyEntry company;
            OutsourceJobDraft draft;
            if (!chooseOutsourceCompanyAndFill(this, m_orderNumber, company, draft)) return;

            const QString part = normalizeSentence(draft.partName);
            if (part.isEmpty()) return;

            if (!insertService(QString::fromUtf8("Зняття %1").arg(part))) return;
            setServicePrice(draft.removePrice);

            if (!insertService(QString::fromUtf8("Встановлення %1").arg(part))) return;
            setServicePrice(draft.installPrice);

            if (!insertService(part)) return;
            setServicePrice(draft.clientPrice);
            const int outsourceServiceId = m_activeServiceId;
            if (!clearOutsourceServiceMaster(m_orderNumber, outsourceServiceId, this)) return;

            if (!saveOutsourceJob(m_orderNumber, outsourceServiceId, company, draft, this)) return;
            refreshAfterMutation(true, false, true, true);
            publishWorkPartsBackupEvent(QStringLiteral("order.outsource"),
                                        QStringLiteral("outsource_job"),
                                        QString::number(outsourceServiceId),
                                        currentOrderUid(),
                                        m_orderNumber,
                                        QStringLiteral("create"),
                                        QString::fromUtf8("Створено аутсорс-ремонт"),
                                        {QStringLiteral("outsource_jobs"), QStringLiteral("work_order_services"), QStringLiteral("service_masters")});
            qCDebug(csWorkPartsTraceLog) << "WorkPartsDialog outsource flow elapsed_ms" << timer.elapsed() << "order" << m_orderNumber;
        });
    });
    connect(m_btnPdf, &QPushButton::clicked, this, [this, importInteractionBusy, ensureNoActivePopupOrInlineEditor] {
        if (importInteractionBusy()) {
            QMessageBox::warning(this,
                                 QString::fromUtf8("Імпорт накладної"),
                                 QString::fromUtf8("Дочекайтеся повного завершення імпорту накладної перед формуванням PDF."));
            return;
        }
        ensureNoActivePopupOrInlineEditor("pdf", [this, importInteractionBusy]() {
            if (importInteractionBusy()) {
                QMessageBox::warning(this,
                                     QString::fromUtf8("Імпорт накладної"),
                                     QString::fromUtf8("Дочекайтеся повного завершення імпорту накладної перед формуванням PDF."));
                return;
            }
            emit requestPdf();
        });
    });
    connect(m_btnAddService, &QPushButton::clicked, this, &WorkPartsDialog::openAddServicesDialog);
    connect(m_btnEditService, &QPushButton::clicked, this, &WorkPartsDialog::openEditServicesDialog);
    connect(m_btnAddPart, &QPushButton::clicked, this, &WorkPartsDialog::openAddPartsDialog);
    connect(m_btnRepairAi, &QPushButton::clicked, this, &WorkPartsDialog::openRepairAdvisorDialog);
    connect(m_btnEditPart, &QPushButton::clicked, this, &WorkPartsDialog::openEditPartsDialog);
    connect(m_btnImportExcel, &QPushButton::clicked, this, &WorkPartsDialog::importPartsFromExcel);
    connect(m_btnCloseOrder, &QPushButton::clicked, this, [this] {
        emit requestCloseOrder();
    });

    connect(m_servicesTable, &QTableWidget::customContextMenuRequested, this, &WorkPartsDialog::showServicesMenu);
    connect(m_partsTable, &QTableWidget::customContextMenuRequested, this, &WorkPartsDialog::showPartsMenu);
    if (m_servicesTable->viewport()) connect(m_servicesTable->viewport(), &QWidget::customContextMenuRequested, this, &WorkPartsDialog::showServicesMenu);
    if (m_partsTable->viewport()) connect(m_partsTable->viewport(), &QWidget::customContextMenuRequested, this, &WorkPartsDialog::showPartsMenu);

    auto hasForeignActivePopupForTable = [](QTableWidget* table) -> bool {
        if (!table) return false;
        QWidget* popup = QApplication::activePopupWidget();
        if (!popup || popup == table || popup == table->viewport()) return false;
        QWidget* focused = QApplication::focusWidget();
        bool focusBelongsToTable = false;
        for (QWidget* w = focused; w; w = w->parentWidget()) {
            if (w == table->viewport() || w == table) {
                focusBelongsToTable = true;
                break;
            }
        }
        if (!focusBelongsToTable) return false;
        const QString className = QString::fromLatin1(popup->metaObject()->className());
        return className.contains(QStringLiteral("QComboBox"), Qt::CaseInsensitive)
            || className.contains(QStringLiteral("QListView"), Qt::CaseInsensitive)
            || className.contains(QStringLiteral("QAbstractItemView"), Qt::CaseInsensitive)
            || popup->inherits("QComboBoxPrivateContainer");
    };

    auto syncTableCurrentCellFromViewportPos = [this, hasForeignActivePopupForTable](QTableWidget* table, const QPoint& viewportPos) {
        if (!table || !table->viewport()) return;
        if (hasForeignActivePopupForTable(table)) {
            qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("MOUSE_CLICK [table_sync_guard] popup_active order=%1 table=%2")
                                  .arg(table->property("cs_order_number").toInt())
                                  .arg(table->objectName().isEmpty() ? QStringLiteral("<unnamed>") : table->objectName());
            return;
        }
        const QModelIndex index = table->indexAt(viewportPos);
        if (!index.isValid()) return;
        flushInlineEditorValueToModel(table);
        table->setCurrentCell(index.row(), index.column(),
                              QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
        if (auto* item = table->item(index.row(), index.column())) {
            table->scrollToItem(item, QAbstractItemView::EnsureVisible);
        }
        table->viewport()->update();
    };

    if (m_servicesTable && m_servicesTable->viewport()) {
        class TableViewportMouseSyncFilter final : public QObject {
        public:
            TableViewportMouseSyncFilter(QTableWidget* table,
                                        std::function<void(QTableWidget*, const QPoint&)> syncFn)
                : QObject(table), m_table(table), m_syncFn(std::move(syncFn)) {}
        protected:
            bool eventFilter(QObject* watched, QEvent* event) override {
                Q_UNUSED(watched);
                if (!m_table || !m_table->viewport() || !m_syncFn) return QObject::eventFilter(watched, event);
                if (event->type() != QEvent::MouseButtonPress) return QObject::eventFilter(watched, event);
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() != Qt::LeftButton && me->button() != Qt::RightButton) {
                    return QObject::eventFilter(watched, event);
                }
                if (QApplication::activePopupWidget() && QApplication::activePopupWidget() != m_table->viewport()) {
                    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("MOUSE_CLICK [viewport_sync_guard] popup=%1 table=%2 order=%3")
                                          .arg(QString::fromLatin1(QApplication::activePopupWidget()->metaObject()->className()))
                                          .arg(m_table->objectName().isEmpty() ? QStringLiteral("<unnamed>") : m_table->objectName())
                                          .arg(m_table->property("cs_order_number").toInt());
                    return QObject::eventFilter(watched, event);
                }
                m_syncFn(m_table, me->position().toPoint());
                return QObject::eventFilter(watched, event);
            }
        private:
            QPointer<QTableWidget> m_table;
            std::function<void(QTableWidget*, const QPoint&)> m_syncFn;
        };
        m_servicesTable->viewport()->installEventFilter(new TableViewportMouseSyncFilter(m_servicesTable, syncTableCurrentCellFromViewportPos));
    }
    if (m_partsTable && m_partsTable->viewport()) {
        class TableViewportMouseSyncFilter final : public QObject {
        public:
            TableViewportMouseSyncFilter(QTableWidget* table,
                                        std::function<void(QTableWidget*, const QPoint&)> syncFn)
                : QObject(table), m_table(table), m_syncFn(std::move(syncFn)) {}
        protected:
            bool eventFilter(QObject* watched, QEvent* event) override {
                Q_UNUSED(watched);
                if (!m_table || !m_table->viewport() || !m_syncFn) return QObject::eventFilter(watched, event);

                if (event->type() == QEvent::MouseMove) {
                    auto* me = static_cast<QMouseEvent*>(event);
                    const QModelIndex index = m_table->indexAt(me->position().toPoint());
                    bool clickableInvoice = false;
                    if (index.isValid() && index.column() == PartColInvoiceNumber) {
                        if (auto* invoiceItem = m_table->item(index.row(), PartColInvoiceNumber)) {
                            const QString invoiceNumber = invoiceItem->data(RoleAttachmentDocumentNumber).toString().trimmed();
                            QString invoicePath = invoiceItem->data(RoleAttachmentPath).toString().trimmed();
                            if (invoicePath.isEmpty()) {
                                if (auto* attachmentItem = m_table->item(index.row(), PartColAttachment)) {
                                    invoicePath = attachmentItem->data(RoleAttachmentPath).toString().trimmed();
                                    if (invoicePath.isEmpty()) invoicePath = attachmentItem->data(RoleAttachmentSource).toString().trimmed();
                                }
                            }
                            clickableInvoice = !invoiceNumber.isEmpty() || !invoicePath.isEmpty();
                        }
                    }
                    m_table->viewport()->setCursor(clickableInvoice ? Qt::PointingHandCursor : Qt::ArrowCursor);
                    return QObject::eventFilter(watched, event);
                }

                if (event->type() == QEvent::Leave) {
                    m_table->viewport()->unsetCursor();
                    return QObject::eventFilter(watched, event);
                }

                if (event->type() != QEvent::MouseButtonPress) return QObject::eventFilter(watched, event);
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() != Qt::LeftButton && me->button() != Qt::RightButton) {
                    return QObject::eventFilter(watched, event);
                }
                const QModelIndex index = m_table->indexAt(me->position().toPoint());
                qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("MOUSE_CLICK [WorkPartsDialog.partsTable.viewport] button=%1 row=%2 col=%3 pos=(%4,%5) order=%6")
                                      .arg(int(me->button()))
                                      .arg(index.isValid() ? index.row() : -1)
                                      .arg(index.isValid() ? index.column() : -1)
                                      .arg(me->position().toPoint().x())
                                      .arg(me->position().toPoint().y())
                                      .arg(m_table->property("cs_order_number").toInt());
                if (QApplication::activePopupWidget() && QApplication::activePopupWidget() != m_table->viewport()) {
                    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("MOUSE_CLICK [viewport_sync_guard] popup=%1 table=%2 order=%3")
                                          .arg(QString::fromLatin1(QApplication::activePopupWidget()->metaObject()->className()))
                                          .arg(m_table->objectName().isEmpty() ? QStringLiteral("<unnamed>") : m_table->objectName())
                                          .arg(m_table->property("cs_order_number").toInt());
                    return QObject::eventFilter(watched, event);
                }
                m_syncFn(m_table, me->position().toPoint());
                return QObject::eventFilter(watched, event);
            }
        private:
            QPointer<QTableWidget> m_table;
            std::function<void(QTableWidget*, const QPoint&)> m_syncFn;
        };
        m_partsTable->viewport()->setMouseTracking(true);
        m_partsTable->viewport()->installEventFilter(new TableViewportMouseSyncFilter(m_partsTable, syncTableCurrentCellFromViewportPos));
    }

    connect(m_servicesTable, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (row < 0 || col < 0) return;
        m_servicesTable->clearSelection();
        m_servicesTable->setCurrentCell(row, col);
        if (col == ServiceColAttachment) { openServiceAttachmentDialog(row); return; }
        const QPoint clickPos = m_servicesTable->viewport()->mapFromGlobal(QCursor::pos());
        scheduleDelayedActivateEditor(m_servicesTable, row, col, clickPos);
    });
    auto flushPartsInlineEditorIfNeeded = [this](int targetRow, int targetCol) {
        if (!m_partsTable || !m_partsTable->property("inlineEditorActive").toBool()) return;
        const QPersistentModelIndex activeIndex = inlineEditingPersistentIndex(m_partsTable);
        const int activeRow = activeIndex.isValid() ? activeIndex.row() : -1;
        const int activeCol = activeIndex.isValid() ? activeIndex.column() : -1;
        if (activeRow != targetRow || activeCol != targetCol) {
            flushInlineEditorValueToModel(m_partsTable);
        }
    };

    connect(m_partsTable, &QTableWidget::cellClicked, this, [this, flushPartsInlineEditorIfNeeded](int row, int col) {
        if (row < 0 || col < 0) return;
        if (m_partsTable->property("csImportBusy").toBool()) return;
        if (QWidget* popup = QApplication::activePopupWidget()) {
            if (popup != m_partsTable->viewport()) return;
        }
        flushPartsInlineEditorIfNeeded(row, col);
        m_partsTable->clearSelection();
        m_partsTable->setCurrentCell(row, col);
        if (col == PartColAttachment) { openPartAttachmentDialog(row); return; }
        if (col == PartColInvoiceNumber) {
            auto* invoiceItem = m_partsTable->item(row, PartColInvoiceNumber);
            auto* attachmentItem = m_partsTable->item(row, PartColAttachment);
            const QString invoiceNumber = invoiceItem ? invoiceItem->data(RoleAttachmentDocumentNumber).toString().trimmed() : QString();
            QString invoicePath = invoiceItem ? invoiceItem->data(RoleAttachmentPath).toString().trimmed() : QString();
            if (invoicePath.isEmpty() && attachmentItem) {
                invoicePath = attachmentItem->data(RoleAttachmentPath).toString().trimmed();
                if (invoicePath.isEmpty()) invoicePath = attachmentItem->data(RoleAttachmentSource).toString().trimmed();
            }
            if (!invoiceNumber.isEmpty() || !invoicePath.isEmpty()) {
                QPointer<WorkPartsDialog> self(this);
                QTimer::singleShot(0, this, [self, row]() {
                    if (!self) return;
                    self->openPartInvoiceDocument(row);
                });
                return;
            }
        }
        const QPoint clickPos = m_partsTable->viewport()->mapFromGlobal(QCursor::pos());
        scheduleDelayedActivateEditor(m_partsTable, row, col, clickPos);
    });
    connect(m_servicesTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col == ServiceColAttachment) return;
        const QPoint clickPos = m_servicesTable->viewport()->mapFromGlobal(QCursor::pos());
        scheduleDelayedActivateEditor(m_servicesTable, row, col, clickPos);
    });
    connect(m_partsTable, &QTableWidget::cellDoubleClicked, this, [this, flushPartsInlineEditorIfNeeded](int row, int col) {
        if (m_partsTable->property("csImportBusy").toBool()) return;
        if (QWidget* popup = QApplication::activePopupWidget()) {
            if (popup != m_partsTable->viewport()) return;
        }
        if (col == PartColAttachment || col == PartColInvoiceNumber) return;
        flushPartsInlineEditorIfNeeded(row, col);
        const QPoint clickPos = m_partsTable->viewport()->mapFromGlobal(QCursor::pos());
        scheduleDelayedActivateEditor(m_partsTable, row, col, clickPos);
    });

    connect(m_servicesTable, &QTableWidget::currentCellChanged, this, [this](int row, int currentColumn, int previousRow, int) {
        if (m_syncingServices) return;
        if (previousRow >= 0 && previousRow < m_servicesTable->rowCount()) {
            auto* prevTitleItem = m_servicesTable->item(previousRow, ServiceColTitle);
            auto* prevQtyItem = m_servicesTable->item(previousRow, ServiceColQty);
            auto* prevPriceItem = m_servicesTable->item(previousRow, ServiceColPrice);
            const bool hasTitle = prevTitleItem && !prevTitleItem->data(RoleIsHint).toBool() && !prevTitleItem->text().trimmed().isEmpty();
            const bool hasPrice = prevPriceItem && !prevPriceItem->text().trimmed().isEmpty();
            const bool missingQty = prevQtyItem && prevQtyItem->text().trimmed().isEmpty();
            if (hasTitle && hasPrice && missingQty) {
                const QSignalBlocker blocker(m_servicesTable);
                prevQtyItem->setText(QStringLiteral("1"));
            }
        }
        if (row < 0) return;
        if (row == previousRow) return;
        auto* item = m_servicesTable->item(row, ServiceColTitle);
        if (!item || item->data(RoleIsHint).toBool() || isSpacerItem(item)) return;
        Q_UNUSED(currentColumn);
        m_activeServiceId = item->data(RoleId).toInt();
    });

    connect(m_servicesTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!item || m_syncingServices) return;
        const int row = item->row();
        if (row < 0 || row >= m_servicesTable->rowCount()) return;
        const QString normalized = (item->column() == ServiceColTitle) ? canonicalizeServiceTitle(item->text())
                                                          : ((item->column() == ServiceColMaster) ? normalizeSentence(item->text()) : item->text().trimmed());
        if ((item->column() == ServiceColTitle || item->column() == ServiceColMaster) && normalized != item->text()) {
            QSignalBlocker blocker(m_servicesTable);
            item->setText(normalized);
        }
        const bool wasHintRow = rowHasHintMarker(m_servicesTable, row);
        const bool hasMeaningfulData = rowHasMeaningfulServiceData(m_servicesTable, row);
        if (hasMeaningfulData) {
            const QString defaultMasterName = currentPrimaryMasterName(m_masterCombo);
            {
                const QSignalBlocker blocker(m_servicesTable);
                if (auto* qtyItem = m_servicesTable->item(row, ServiceColQty);
                    qtyItem && qtyItem->text().trimmed().isEmpty()) {
                    qtyItem->setText(QStringLiteral("1"));
                }
                if (auto* masterItem = m_servicesTable->item(row, ServiceColMaster);
                    masterItem && masterItem->text().trimmed().isEmpty() && !defaultMasterName.trimmed().isEmpty()) {
                    masterItem->setText(defaultMasterName);
                    masterItem->setData(RoleIsHint, false);
                }
            }
            clearRowHintState(m_servicesTable, row);
            setRowEditingVisual(m_servicesTable, row);
            refreshServiceAttachmentCell(row);
            applyLeftAlignmentToServiceRow(m_servicesTable, row);
            if (wasHintRow) {
                scheduleEnsureNextInputRow(m_servicesTable, row);
            }
        }
        {
            const QSignalBlocker blocker(m_servicesTable);
            renumberServiceItems();
            if (!hasMeaningfulData) {
                if (auto* qtyItem = m_servicesTable->item(row, ServiceColQty)) qtyItem->setText(QString());
                if (auto* priceItem = m_servicesTable->item(row, ServiceColPrice)) priceItem->setText(QString());
                refreshServiceAttachmentCell(row);
            }
            applyLeftAlignmentToServiceRow(m_servicesTable, row);
        }
        if (item->column() == ServiceColTitle || item->column() == ServiceColMaster) {
            synchronizePairedRowHeights(row);
        }
        m_dirtyServiceRows.insert(row);
        m_userTouchedPendingContent = true;
        notifyDraftDirtyChanged();
        if (item->column() == ServiceColTitle && item->data(RoleId).toInt() <= 0) {
            // Wait until the delegate has committed the title and finished its navigation.
            const QPersistentModelIndex index(m_servicesTable->model()->index(row, ServiceColTitle));
            QTimer::singleShot(0, this, [this, index]() {
                if (index.isValid()) autofillDraftPartsForService(index.row());
            });
        }
    });

    connect(m_partsTable, &QTableWidget::currentCellChanged, this, [this](int row, int currentColumn, int previousRow, int previousColumn) {
        if (!m_partsTable || m_loadingParts || m_partsCurrentCellGuard) return;
        QScopedValueRollback<bool> guard(m_partsCurrentCellGuard, true);
        logPartCrashContext(QStringLiteral("PARTS_TABLE"), QStringLiteral("currentCellChanged_enter"), m_partsTable, row, currentColumn, m_orderNumber, currentOrderUid(nullptr), QStringLiteral("previousRow=%1 previousColumn=%2").arg(previousRow).arg(previousColumn));
        const auto validCell = [this](int r, int c) {
            return m_partsTable && r >= 0 && c >= 0 && r < m_partsTable->rowCount() && c < m_partsTable->columnCount();
        };
        if (!validCell(row, currentColumn) && row >= 0) {
            qWarning().noquote() << QStringLiteral("PARTS_TABLE current_cell_invalid order=") << m_orderNumber
                                 << QStringLiteral(" row=") << row
                                 << QStringLiteral(" col=") << currentColumn
                                 << QStringLiteral(" rows=") << (m_partsTable ? m_partsTable->rowCount() : -1)
                                 << QStringLiteral(" cols=") << (m_partsTable ? m_partsTable->columnCount() : -1);
            return;
        }
        if (!m_partsTable->property("inlineEditorActive").toBool()) return;
        const QPersistentModelIndex activeIndex = inlineEditingPersistentIndex(m_partsTable);
        const int activeRow = activeIndex.isValid() ? activeIndex.row() : -1;
        const int activeCol = activeIndex.isValid() ? activeIndex.column() : -1;
        if (activeRow == previousRow && activeCol == previousColumn && (row != previousRow || currentColumn != previousColumn)) {
            qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PARTS_TABLE current_cell_flush_inline order=") << m_orderNumber
                              << QStringLiteral(" fromRow=") << previousRow
                              << QStringLiteral(" fromCol=") << previousColumn
                              << QStringLiteral(" toRow=") << row
                              << QStringLiteral(" toCol=") << currentColumn;
            flushInlineEditorValueToModel(m_partsTable);
        }
    });

    connect(m_partsTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!m_partsTable || !item || m_loadingParts || m_partsItemChangedGuard) return;
        if (item->tableWidget() != m_partsTable) {
            qWarning().noquote() << QStringLiteral("PARTS_TABLE item_changed_foreign_table order=") << m_orderNumber;
            return;
        }
        QScopedValueRollback<bool> guard(m_partsItemChangedGuard, true);
        logPartCrashContext(QStringLiteral("PARTS_TABLE"), QStringLiteral("itemChanged_enter"), m_partsTable, item->row(), item->column(), m_orderNumber, currentOrderUid(nullptr), QStringLiteral("value=%1").arg(clippedImportLogText(item->text(), 240)));
        const int row = item->row();
        const int col = item->column();
        if (row < 0 || row >= m_partsTable->rowCount() || col < 0 || col >= m_partsTable->columnCount()) {
            qWarning().noquote() << QStringLiteral("PARTS_TABLE item_changed_invalid_index order=") << m_orderNumber
                                 << QStringLiteral(" row=") << row
                                 << QStringLiteral(" col=") << col
                                 << QStringLiteral(" rows=") << m_partsTable->rowCount()
                                 << QStringLiteral(" cols=") << m_partsTable->columnCount();
            return;
        }

        if (partRowHasImportedInvoiceMarker(m_partsTable, row)) {
            qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS imported_cell_changed order=") << m_orderNumber
                              << QStringLiteral(" row=") << row
                              << QStringLiteral(" col=") << col
                              << QStringLiteral(" trace=") << importedPartTraceId(m_partsTable, row)
                              << QStringLiteral(" value=") << clippedImportLogText(item->text());
        }

        if (item->column() == PartColDelivered) {
            logPartCrashContext(QStringLiteral("DATE_PARSE"), QStringLiteral("delivery_checkbox_changed"), m_partsTable, row, col, m_orderNumber, currentOrderUid(nullptr));
            const int partId = item->data(RoleId).toInt();
            const QString previousIso = item->data(RoleDeliveryDate).toString();
            const bool installed = item->checkState() == Qt::Checked;
            const QString newIso = installed ? QDate::currentDate().toString(Qt::ISODate) : QString();
            if (partId <= 0) {
                // Unsaved rows do not have a DB id yet.  Do not rollback the
                // user's checkbox click; store the intended installed_at value
                // on the cell and persist it when the row is inserted on OK.
                {
                    const QSignalBlocker blocker(m_partsTable);
                    item->setData(RoleDeliveryDate, newIso);
                }
                m_dirtyPartRows.insert(row);
                m_userTouchedPendingContent = true;
                notifyDraftDirtyChanged();
                qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PARTS_TABLE pending_install_state order=") << m_orderNumber
                                  << QStringLiteral(" row=") << row
                                  << QStringLiteral(" checked=") << installed
                                  << QStringLiteral(" pendingInstalledAt=") << newIso
                                  << QStringLiteral(" dirtyRows=") << m_dirtyPartRows.size();
                return;
            }
            QString updateError;
            if (!WorkPartsMutationService::updatePartDeliveryDateValue(partId, newIso, currentOrderUid(), m_orderNumber, &updateError)) {
                const QSignalBlocker blocker(m_partsTable);
                item->setCheckState(previousIso.trimmed().isEmpty() ? Qt::Unchecked : Qt::Checked);
                QMessageBox::critical(this, QString::fromUtf8("Помилка"), updateError);
                return;
            }
            {
                const QSignalBlocker blocker(m_partsTable);
                item->setData(RoleDeliveryDate, newIso);
            }
            return;
        }


        if (item->column() == PartColName || item->column() == PartColStore || item->column() == PartColBrand) {
            const QString normalized = (item->column() == PartColName)
                ? normalizePartSentence(item->text())
                : normalizeSentence(item->text());
            if (normalized != item->text()) {
                const QSignalBlocker blocker(m_partsTable);
                item->setText(normalized);
            }
        }
        constexpr int RoleAutoOilBrand = Qt::UserRole + 19;
        if (item->column() == PartColBrand) {
            const QString autoBrand = item->data(RoleAutoOilBrand).toString().trimmed();
            if (!autoBrand.isEmpty() && autoBrand != item->text().trimmed()) {
                item->setData(RoleAutoOilBrand, QString());
            }
        }

        if (item->column() == PartColName || item->column() == PartColStore) {
            auto* nameItem = m_partsTable->item(row, PartColName);
            auto* storeItem = m_partsTable->item(row, PartColStore);
            auto* brandItem = m_partsTable->item(row, PartColBrand);
            const QString currentBrand = brandItem ? brandItem->text().trimmed() : QString();
            const QString lastAutoBrand = brandItem ? brandItem->data(RoleAutoOilBrand).toString().trimmed() : QString();
            const bool canApplyAutoBrand = brandItem && (currentBrand.isEmpty() || (!lastAutoBrand.isEmpty() && currentBrand == lastAutoBrand));
            if (canApplyAutoBrand) {
                const QString oilBrand = effectiveOilBrand(nameItem ? nameItem->text() : QString(),
                                                           storeItem ? storeItem->text() : QString()).trimmed();
                if (!oilBrand.isEmpty() && oilBrand != currentBrand) {
                    const QSignalBlocker blocker(m_partsTable);
                    brandItem->setText(oilBrand);
                    brandItem->setData(RoleAutoOilBrand, oilBrand);
                    syncPartManufacturerRoleFromRow(m_partsTable, row);
                }
            }
        }

        if (item->column() == PartColBrand || item->column() == PartColName) {
            syncPartManufacturerRoleFromRow(m_partsTable, row);
        }
        if (item->column() == PartColInvoiceNumber) {
            const QString invoiceNumber = normalizeInvoiceDisplayNumber(item->text());
            {
                const QSignalBlocker blocker(m_partsTable);
                if (item->text() != invoiceNumber) item->setText(invoiceNumber);
            }
            auto* attachmentItem = m_partsTable->item(row, PartColAttachment);
            if (attachmentItem) {
                const QSignalBlocker blocker(m_partsTable);
                attachmentItem->setData(RoleAttachmentDocumentNumber, invoiceNumber);
                attachmentItem->setToolTip(attachmentToolTipText(attachmentItem->data(RoleAttachmentPath).toString(),
                                                                invoiceNumber,
                                                                attachmentItem->data(RoleAttachmentDocumentDate).toString()));
            }
            {
                const QSignalBlocker blocker(m_partsTable);
                item->setData(RoleAttachmentDocumentNumber, invoiceNumber);
                QFont f = item->font();
                if (!invoiceNumber.isEmpty()) {
                    f.setUnderline(true);
                    item->setForeground(QColor(QStringLiteral("#1d4ed8")));
                } else {
                    f.setUnderline(false);
                    item->setForeground(QColor(QStringLiteral("#111111")));
                }
                item->setFont(f);
            }
        }
        const bool wasHintRow = rowHasHintMarker(m_partsTable, row);
        const bool hasMeaningfulData = rowHasMeaningfulPartData(m_partsTable, row);
        if (hasMeaningfulData) {
            if (auto* orderedAtItem = m_partsTable->item(row, PartColOrderedAt);
                orderedAtItem && orderedAtItem->text().trimmed().isEmpty()) {
                const QSignalBlocker blocker(m_partsTable);
                orderedAtItem->setText(currentOrderDateDisplay());
            }
            clearRowHintState(m_partsTable, row);
            setRowEditingVisual(m_partsTable, row);
            if (wasHintRow) {
                scheduleEnsureNextInputRow(m_partsTable, row);
            }
        }
        syncPartDeliveredCellState(m_partsTable, row);
        m_partsTable->viewport()->update(m_partsTable->visualRect(m_partsTable->model()->index(row, PartColDelivered)));
        if (item->column() == PartColQty
            || item->column() == PartColPrice
            || item->column() == PartColName
            || item->column() == PartColStore
            || item->column() == PartColBrand) {
            refreshPartRowClientPricing(m_partsTable, row, item->column());
        }
        if (item->column() == PartColName || item->column() == PartColStore
            || item->column() == PartColOem || item->column() == PartColBrand || item->column() == PartColInvoiceNumber) {
            synchronizePairedRowHeights(row);
        }
        {
            const QSignalBlocker blocker(m_partsTable);
            renumberPartItems();
            if (!rowHasMeaningfulPartData(m_partsTable, row)) {
                if (auto* qtyItem = m_partsTable->item(row, PartColQty)) qtyItem->setText(QString());
                if (auto* priceItem = m_partsTable->item(row, PartColPrice)) priceItem->setText(QString());
                if (auto* amountItem = m_partsTable->item(row, PartColAmount)) amountItem->setText(QString());
            }
        }
        m_dirtyPartRows.insert(row);
        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("SAVE_QUEUE")
                          << QStringLiteral(" table=parts")
                          << QStringLiteral(" order=") << m_orderNumber
                          << QStringLiteral(" row=") << row
                          << QStringLiteral(" col=") << col
                          << QStringLiteral(" dirtyRows=") << m_dirtyPartRows.size()
                          << QStringLiteral(" imported=") << partRowHasImportedInvoiceMarker(m_partsTable, row)
                          << QStringLiteral(" trace=") << importedPartTraceId(m_partsTable, row);
        m_userTouchedPendingContent = true;
        if (partRowHasImportedInvoiceMarker(m_partsTable, row)) {
            qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS imported_cell_changed_done order=") << m_orderNumber
                              << QStringLiteral(" row=") << row
                              << QStringLiteral(" col=") << col
                              << QStringLiteral(" trace=") << importedPartTraceId(m_partsTable, row);
        }
        notifyDraftDirtyChanged();
    });
}

namespace {
quint64 workPartsHeightSignatureForTable(QTableWidget* table, int row, const QVector<int>& textColumns)
{
    if (!table || row < 0 || row >= table->rowCount()) return 0;

    QString signature;
    signature.reserve(256);
    signature += table->objectName();
    signature += QLatin1Char('|');
    signature += QString::number(table->font().pointSizeF(), 'f', 2);
    for (int column : textColumns) {
        if (column < 0 || column >= table->columnCount()) continue;
        signature += QLatin1Char('|');
        signature += QString::number(column);
        signature += QLatin1Char(':');
        signature += QString::number(table->columnWidth(column));
        signature += QLatin1Char(':');
        if (auto* item = table->item(row, column)) {
            signature += item->text().trimmed().left(160);
        }
    }
    return static_cast<quint64>(qHash(signature));
}

quint64 workPartsPairedHeightSignature(QTableWidget* servicesTable, QTableWidget* partsTable, int row)
{
    QString signature;
    signature.reserve(96);
    signature += QString::number(workPartsHeightSignatureForTable(servicesTable, row, {ServiceColTitle}));
    signature += QLatin1Char('|');
    signature += QString::number(workPartsHeightSignatureForTable(partsTable, row, {PartColName, PartColOem, PartColBrand, PartColStore, PartColInvoiceNumber}));
    return static_cast<quint64>(qHash(signature));
}
}

void WorkPartsDialog::invalidateWorkPartsRowHeightCache(int row)
{
    if (row < 0) {
        m_pairedRowHeightSignatureByRow.clear();
        m_pairedRowHeightByRow.clear();
        return;
    }
    m_pairedRowHeightSignatureByRow.remove(row);
    m_pairedRowHeightByRow.remove(row);
}

void WorkPartsDialog::synchronizePairedRowHeights(int row)
{
    if (row < 0) return;

    const quint64 signature = workPartsPairedHeightSignature(m_servicesTable, m_partsTable, row);
    int targetHeight = 0;
    const auto sigIt = m_pairedRowHeightSignatureByRow.constFind(row);
    const auto heightIt = m_pairedRowHeightByRow.constFind(row);
    if (sigIt != m_pairedRowHeightSignatureByRow.constEnd()
        && heightIt != m_pairedRowHeightByRow.constEnd()
        && *sigIt == signature) {
        targetHeight = *heightIt;
    } else {
        targetHeight = kDefaultTableRowHeight;
        if (m_servicesTable && row < m_servicesTable->rowCount()) {
            targetHeight = qMax(targetHeight, calcMainServicesRowHeight(m_servicesTable, row));
        }
        if (m_partsTable && row < m_partsTable->rowCount()) {
            targetHeight = qMax(targetHeight, calcMainPartsRowHeight(m_partsTable, row));
        }
        m_pairedRowHeightSignatureByRow.insert(row, signature);
        m_pairedRowHeightByRow.insert(row, targetHeight);
    }

    if (m_servicesTable && row < m_servicesTable->rowCount() && m_servicesTable->rowHeight(row) != targetHeight) {
        m_servicesTable->setRowHeight(row, targetHeight);
    }
    if (m_partsTable && row < m_partsTable->rowCount() && m_partsTable->rowHeight(row) != targetHeight) {
        m_partsTable->setRowHeight(row, targetHeight);
    }
}

void WorkPartsDialog::synchronizeAllPairedRowHeights()
{
    PerfWatchdog::ScopedTimer perfTimer(QStringLiteral("WorkPartsDialog::synchronizeAllPairedRowHeights"), 80);
    const int serviceRows = m_servicesTable ? m_servicesTable->rowCount() : 0;
    const int partRows = m_partsTable ? m_partsTable->rowCount() : 0;
    const int maxRows = qMax(serviceRows, partRows);
    if (maxRows <= 0) return;

    auto visibleRange = [](QTableWidget* table) -> QPair<int, int> {
        if (!table || !table->viewport() || table->rowCount() <= 0) return qMakePair(0, -1);
        int first = table->rowAt(0);
        int last = table->rowAt(qMax(0, table->viewport()->height() - 1));
        if (first < 0) first = table->verticalScrollBar() ? table->verticalScrollBar()->value() : 0;
        if (last < 0) last = qMin(table->rowCount() - 1, first + 60);
        first = qBound(0, first - 30, table->rowCount() - 1);
        last = qBound(0, last + 30, table->rowCount() - 1);
        return qMakePair(first, last);
    };

    const auto serviceRange = visibleRange(m_servicesTable);
    const auto partRange = visibleRange(m_partsTable);
    int firstRow = maxRows;
    int lastRow = -1;
    if (serviceRange.second >= serviceRange.first) {
        firstRow = qMin(firstRow, serviceRange.first);
        lastRow = qMax(lastRow, serviceRange.second);
    }
    if (partRange.second >= partRange.first) {
        firstRow = qMin(firstRow, partRange.first);
        lastRow = qMax(lastRow, partRange.second);
    }
    if (lastRow < firstRow) return;
    firstRow = qBound(0, firstRow, maxRows - 1);
    lastRow = qBound(0, lastRow, maxRows - 1);

    const bool serviceUpdatesWereEnabled = m_servicesTable ? m_servicesTable->updatesEnabled() : false;
    const bool partUpdatesWereEnabled = m_partsTable ? m_partsTable->updatesEnabled() : false;
    if (m_servicesTable) m_servicesTable->setUpdatesEnabled(false);
    if (m_partsTable) m_partsTable->setUpdatesEnabled(false);

    // P0 performance: sync only viewport rows plus a small buffer. Full-table
    // height synchronization on every rebuild/import was a major UI-freeze
    // source for orders with many works/parts.
    for (int row = firstRow; row <= lastRow; ++row) {
        synchronizePairedRowHeights(row);
    }

    if (m_servicesTable) {
        m_servicesTable->setUpdatesEnabled(serviceUpdatesWereEnabled);
        if (m_servicesTable->viewport()) m_servicesTable->viewport()->update();
    }
    if (m_partsTable) {
        m_partsTable->setUpdatesEnabled(partUpdatesWereEnabled);
        if (m_partsTable->viewport()) m_partsTable->viewport()->update();
    }
}

void WorkPartsDialog::persistUiLayoutState() const
{
    persistHeaderState(m_servicesTable ? m_servicesTable->horizontalHeader() : nullptr, QString::fromLatin1(kWorksHeaderStateKey));
    persistHeaderState(m_partsTable ? m_partsTable->horizontalHeader() : nullptr, QString::fromLatin1(kPartsHeaderStateKey));
    persistTableColumnWidths(m_servicesTable, QString::fromLatin1(kWorksColumnWidthsKey));
    persistTableColumnWidths(m_partsTable, QString::fromLatin1(kPartsColumnWidthsKey));
    if (m_servicesTable && m_servicesTable->verticalHeader()) {
        UiStateSettingsService::saveValue(QString::fromLatin1(kWorksRowHeaderWidthKey), m_servicesTable->verticalHeader()->width());
    }
    persistSplitterState(m_split, QString::fromLatin1(kSplitterStateKey));
}
#include <QPointer>
