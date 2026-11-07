"""Source-only migration; originals kept in development backup."""
from pathlib import Path
import shutil
R=Path(__file__).resolve().parents[1]
def edit(name,pairs):
 p=R/name;s=p.read_text(encoding='utf-8-sig')
 for a,b in pairs:
  assert a in s,(name,a);s=s.replace(a,b)
 back=R/'build-single-instance-audit/financial-fix-source-backup'/name
 back.parent.mkdir(parents=True,exist_ok=True)
 if not back.exists():shutil.copy2(p,back)
 p.write_text(s,encoding='utf-8')
for f in ['src/core/services/OrderWorkspaceService.h','src/core/services/orders/OrderSnapshotService.h','src/core/services/orders/OrderDialogStateSignature.h']:
 edit(f,[('double discount = 0.0;','double discount = 0.0;\n    QString discountMode = QStringLiteral("legacy");')])
edit('src/infrastructure/db/sqlite/DbBootstrapEnsureSchema.cpp',[
 ('    // services\n','    if (!ensureColumn("work_orders", "discount_mode", "discount_mode TEXT NOT NULL DEFAULT \'legacy\'")) return false;\n\n    // services\n')])
edit('src/core/services/OrderWorkspaceService.cpp',[
 ('parts_sum, discount) ','parts_sum, discount, discount_mode) '),
 ('VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)','VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)'),
 ('parts_sum=?, discount=?, order_number=?','parts_sum=?, discount=?, discount_mode=?, order_number=?'),
 ('q.addBindValue(input.discount);','q.addBindValue(input.discount);\n        q.addBindValue(input.discountMode);')])
for f in ['src/core/services/orders/OrderSnapshotService.cpp','src/core/services/orders/OrderDialogReadQueryService.cpp']:
 edit(f,[('COALESCE(master_name_closed,\'\'), COALESCE(discount,0) "','COALESCE(master_name_closed,\'\'), COALESCE(discount,0), COALESCE(discount_mode,\'legacy\') "'),
 ('outSnapshot->discount = q.value(22).toDouble();','outSnapshot->discount = q.value(22).toDouble();\n    outSnapshot->discountMode = q.value(23).toString();')])
edit('src/core/services/orders/OrderDialogStateSignature.cpp',[
 ("QString::number(snapshot.discount, 'f', 2)","QString::number(snapshot.discount, 'f', 2), snapshot.discountMode")])
edit('src/ui/common/dialogs/OrderDialog.h',[
 ('QDoubleSpinBox* m_discount = nullptr;','QDoubleSpinBox* m_discount = nullptr;\n    QComboBox* m_discountMode = nullptr;')])
edit('src/ui/common/dialogs/OrderDialogBuildUi.cpp',[
 ('    addFullRow(QString::fromUtf8("Знижка:"), m_discount);','''    m_discountMode = new QComboBox(this);
    m_discountMode->setObjectName(QStringLiteral("orderDiscountMode"));
    m_discountMode->addItem(QString::fromUtf8("грн"), QStringLiteral("uah"));
    m_discountMode->addItem(QStringLiteral("%"), QStringLiteral("percent"));
    m_discountMode->addItem(QString::fromUtf8("Історична (тип не задано)"), QStringLiteral("legacy"));
    auto* discountRow = new QWidget(this);
    auto* discountLayout = new QHBoxLayout(discountRow);
    discountLayout->setContentsMargins(0,0,0,0);
    discountLayout->addWidget(m_discount, 1);
    discountLayout->addWidget(m_discountMode);
    connect(m_discountMode, &QComboBox::currentIndexChanged, this, [this] {
        const bool percent = m_discountMode->currentData().toString() == QStringLiteral("percent");
        m_discount->setSuffix(percent ? QStringLiteral(" %") : QString::fromUtf8(" грн"));
        refreshTotals();
    });
    addFullRow(QString::fromUtf8("Знижка:"), discountRow);'''),
 ('    armUserEditTracking(m_discount);','    armUserEditTracking(m_discount);\n    armUserEditTracking(m_discountMode);')])
edit('src/ui/common/dialogs/OrderDialogLifecycleFlow.cpp',[
 ('m_discount->setValue(vehicle.value(11).toDouble());','m_discount->setValue(vehicle.value(11).toDouble());\n    m_discountMode->setCurrentIndex(m_discountMode->findData(QStringLiteral("percent")));'),
 ('if (m_discount) m_discount->setValue(snapshot.discount);','if (m_discount) m_discount->setValue(snapshot.discount);\n    if (m_discountMode) m_discountMode->setCurrentIndex(m_discountMode->findData(snapshot.discountMode));'),
 ('    m_discount->setValue(snapshot.discount);','    m_discount->setValue(snapshot.discount);\n    m_discountMode->setCurrentIndex(m_discountMode->findData(snapshot.discountMode));'),
 ('if (m_discount) m_discount->setValue(0.0);','if (m_discount) m_discount->setValue(0.0);\n    if (m_discountMode) m_discountMode->setCurrentIndex(0);'),
 ('m_phone, m_master, m_discount};','m_phone, m_master, m_discount, m_discountMode};'),
 ('snapshot.discount = m_discount ? m_discount->value() : 0.0;','snapshot.discount = m_discount ? m_discount->value() : 0.0;\n    snapshot.discountMode = m_discountMode->currentData().toString();'),
 ('input.discount = discount;','input.discount = discount;\n    input.discountMode = m_discountMode->currentData().toString();'),
 ('{QStringLiteral("discount"), m_discount ? m_discount->value() : 0.0}','{QStringLiteral("discount"), m_discount ? m_discount->value() : 0.0},\n        {QStringLiteral("discount_mode"), m_discountMode->currentData().toString()}'),
 ('if (m_discount) m_discount->setValue(payload.value(QStringLiteral("discount")).toDouble(m_discount->value()));','if (m_discount) m_discount->setValue(payload.value(QStringLiteral("discount")).toDouble(m_discount->value()));\n    m_discountMode->setCurrentIndex(m_discountMode->findData(payload.value(QStringLiteral("discount_mode")).toString(QStringLiteral("legacy"))));'),
 ('const double disc = m_discount ? FormatUtils::roundMoneyValue(m_discount->value()) : 0.0;','const double disc = m_discount ? FormatUtils::discountAmount(m_discount->value(), m_discountMode->currentData().toString(), w) : 0.0;')])
