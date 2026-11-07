// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

// extracted from WorkPartsDialogSupport.cpp as part of p.4 decomposition
#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"

#include "core/services/orders/PartMarkupPolicyService.h"

QStringList loadPartStoreNamesFromSettings()
{
    return PartMarkupPolicyService::knownStoreNames();
}

double effectivePartMarkupPercent(const QString& partName, const QString& store)
{
    return PartMarkupPolicyService::effectivePartMarkupPercent(partName, store);
}

QString effectiveOilBrand(const QString& partName, const QString& store)
{
    return PartMarkupPolicyService::effectiveOilBrand(partName, store);
}

double applyMarkup(double purchasePrice, double pct)
{
    return PartMarkupPolicyService::applyMarkup(purchasePrice, pct);
}


void WorkPartsDialog::applySelectedPartMarkupPercent()
{
    if (!m_partsTable) return;

    flushInlineEditorValueToModel(m_partsTable);
    m_partsTable->clearFocus();

    int row = m_partsTable->currentRow();
    if (row < 0 || row >= m_partsTable->rowCount() || !rowHasMeaningfulPartData(m_partsTable, row)) {
        row = -1;
        const auto selectedItems = m_partsTable->selectedItems();
        for (QTableWidgetItem* selectedItem : selectedItems) {
            if (!selectedItem) continue;
            const int candidate = selectedItem->row();
            if (candidate >= 0 && candidate < m_partsTable->rowCount() && rowHasMeaningfulPartData(m_partsTable, candidate)) {
                row = candidate;
                break;
            }
        }
    }

    if (row < 0 || row >= m_partsTable->rowCount() || !rowHasMeaningfulPartData(m_partsTable, row)) {
        QMessageBox::information(this,
                                 QString::fromUtf8("Доданий відсоток"),
                                 QString::fromUtf8("Спочатку виберіть запчастину в наряді."));
        return;
    }

    auto* priceItem = m_partsTable->item(row, PartColPrice);
    auto* nameItem = m_partsTable->item(row, PartColName);
    auto* storeItem = m_partsTable->item(row, PartColStore);

    const QString partName = nameItem ? normalizeSentence(nameItem->text()) : QString();
    const QString store = storeItem ? normalizeSentence(storeItem->text()) : QString();

    bool currentPctOk = false;
    const bool currentManualOverride = priceItem && priceItem->data(RoleManualMarkupOverride).toBool();
    double currentPct = priceItem ? priceItem->data(RoleMarkupPercent).toDouble(&currentPctOk) : 0.0;
    if (!currentManualOverride || !currentPctOk || currentPct < 0.0) {
        currentPct = effectivePartMarkupPercent(partName, store);
    }

    QDialog dlg(this);
    dlg.setWindowTitle(QString::fromUtf8("Доданий відсоток запчастини"));
    dlg.setModal(true);
    dlg.setMinimumWidth(420);
    dlg.setStyleSheet(QString::fromUtf8(
        "QDialog { background-color: rgba(82,91,106,246); }"
        "QLabel { color:#f5f7fa; font-weight:700; }"
        "QDoubleSpinBox { background:#ffffff; color:#101214; border:1px solid rgba(205,215,225,0.75);"
        " border-radius:12px; padding:8px 12px; min-height:34px; font-weight:700;"
        " selection-background-color: rgba(105,145,210,0.55); }"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width:28px; }"
        "QPushButton { color:#f8fbff; font-weight:800; min-width:118px; min-height:34px; border-radius:14px;"
        " padding:7px 18px; border:1px solid rgba(255,255,255,0.58);"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #536170, stop:0.12 #7b8998, stop:0.46 #313b48, stop:0.52 #111825, stop:1 #091018); }"
        "QPushButton:hover { border-color:rgba(255,255,255,0.82);"
        " background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #5f6d7c, stop:0.14 #8b98a5, stop:0.48 #394452, stop:0.54 #151d2b, stop:1 #0b1220); }"
        "QPushButton:pressed { padding-top:9px; padding-left:20px; }"));

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(14);

    auto* title = new QLabel(QString::fromUtf8("Введіть доданий відсоток для вибраної запчастини:"), &dlg);
    title->setWordWrap(true);
    root->addWidget(title);

    auto* partInfo = new QLabel(QString::fromUtf8("Запчастина: %1").arg(partName.isEmpty() ? QString::fromUtf8("обраний рядок") : partName), &dlg);
    partInfo->setWordWrap(true);
    QFont partInfoFont = partInfo->font();
    partInfoFont.setPointSizeF(qMax(6.0, QFontInfo(partInfoFont).pointSizeF() + 2.0));
    partInfoFont.setWeight(QFont::DemiBold);
    partInfo->setFont(partInfoFont);
    partInfo->setStyleSheet(QString::fromUtf8("color:#ffd84a; font-weight:600;"));
    root->addWidget(partInfo);

    auto* pctEdit = new QDoubleSpinBox(&dlg);
    pctEdit->setRange(0.0, 1000.0);
    pctEdit->setDecimals(1);
    pctEdit->setSingleStep(1.0);
    pctEdit->setSuffix(QString::fromUtf8(" %"));
    pctEdit->setValue(qMax(0.0, currentPct));
    pctEdit->selectAll();
    root->addWidget(pctEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bool resetToPolicy = false;
    auto* resetBtn = buttons->addButton(QString::fromUtf8("Повернути глобальну націнку"), QDialogButtonBox::ResetRole);
    if (auto* okBtn = buttons->button(QDialogButtonBox::Ok)) {
        okBtn->setText(QString::fromUtf8("Застосувати"));
        okBtn->setCursor(Qt::PointingHandCursor);
    }
    if (auto* cancelBtn = buttons->button(QDialogButtonBox::Cancel)) {
        cancelBtn->setText(QString::fromUtf8("Скасувати"));
        cancelBtn->setCursor(Qt::PointingHandCursor);
    }
    if (resetBtn) {
        resetBtn->setCursor(Qt::PointingHandCursor);
    }
    root->addWidget(buttons, 0, Qt::AlignRight);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(resetBtn, &QPushButton::clicked, &dlg, [&dlg, &resetToPolicy]() {
        resetToPolicy = true;
        dlg.accept();
    });

    if (dlg.exec() != QDialog::Accepted) return;

    const double manualPct = qMax(0.0, qRound(pctEdit->value() * 10.0) / 10.0);
    if (!resetToPolicy && qFuzzyIsNull(manualPct)) {
        const auto answer = QMessageBox::question(this,
            QString::fromUtf8("Індивідуальна націнка 0%"),
            QString::fromUtf8("Індивідуальна 0% означає, що клієнтська ціна дорівнюватиме закупівельній. Продовжити?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

    if (!priceItem) {
        priceItem = makeTextItem(QString());
        priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_partsTable->setItem(row, PartColPrice, priceItem);
    }

    {
        const QSignalBlocker blocker(m_partsTable);
        if (resetToPolicy) {
            priceItem->setData(RoleManualMarkupOverride, false);
            priceItem->setData(RoleMarkupPercent, 0.0);
            priceItem->setToolTip(QString::fromUtf8("Індивідуальну націнку скинуто. Буде застосована поточна глобальна націнка."));
        } else {
            priceItem->setData(RoleManualMarkupOverride, true);
            priceItem->setData(RoleMarkupPercent, manualPct);
            priceItem->setToolTip(QString::fromUtf8("Індивідуальний доданий відсоток: %1 %").arg(manualPct, 0, 'f', 1));
        }
    }

    refreshPartRowClientPricing(m_partsTable, row, PartColStore);
    synchronizePairedRowHeights(row);

    m_dirtyPartRows.insert(row);
    m_userTouchedPendingContent = true;
    notifyDraftDirtyChanged();

    if (resetToPolicy) {
        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_PRICING op=recalc order=%1 partId=0 trace=manual_reset name=\"%2\" store=\"%3\" source=policy override=0")
            .arg(m_orderNumber)
            .arg(partName)
            .arg(store);
    } else {
        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_PRICING op=recalc order=%1 partId=0 trace=manual_set name=\"%2\" store=\"%3\" activePct=%4 source=manual override=1")
            .arg(m_orderNumber)
            .arg(partName)
            .arg(store)
            .arg(manualPct, 0, 'f', 1);
    }
}

