#include "ui/common/dialogs/ServiceMastersDialog.h"

#include "core/services/settings/PersonnelSettingsService.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QLabel>
#include <QtMath>


static QString contrastCheckboxWidgetQss()
{
    return QString::fromUtf8(
        "QCheckBox{spacing:0px; padding:0px; margin:0px; background:transparent;}"
    );
}

static QCheckBox* findRowCheckBox(const QTableWidget* table, int row)
{
    if (!table || row < 0 || row >= table->rowCount()) return nullptr;
    if (auto* cb = qobject_cast<QCheckBox*>(table->cellWidget(row, 0))) return cb;
    if (auto* host = table->cellWidget(row, 0)) return host->findChild<QCheckBox*>();
    return nullptr;
}

static double clamp(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

ServiceMastersDialog::ServiceMastersDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("Майстри для роботи"));
    buildUi();
}

void ServiceMastersDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10,10,10,10);
    root->setSpacing(10);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({QString::fromUtf8("✔"), QString::fromUtf8("Майстер"), QString::fromUtf8("Частка %")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setAlternatingRowColors(true);

    root->addWidget(m_table, 1);

    auto* btnRow = new QHBoxLayout();
    m_btnEqual = new QPushButton(QString::fromUtf8("Рівні частки"), this);
    btnRow->addWidget(m_btnEqual);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(bb);

    connect(m_btnEqual, &QPushButton::clicked, this, &ServiceMastersDialog::distributeEqual);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ServiceMastersDialog::setMasters(const QList<ServiceMasterShare>& shares)
{
    m_shares = shares;

    const auto masters = PersonnelSettingsService::loadPersonnel(true);
    m_table->setRowCount(0);

    auto findShare = [&](const QString& id, ServiceMasterShare& out)->bool {
        for (const auto& s : m_shares) {
            if (s.masterId == id) { out = s; return true; }
        }
        return false;
    };

    for (const auto& m : masters) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);

        // checkbox
        auto* boxHost = new QWidget(m_table);
        auto* boxLayout = new QHBoxLayout(boxHost);
        boxLayout->setContentsMargins(0, 0, 0, 0);
        boxLayout->setSpacing(0);
        boxLayout->setAlignment(Qt::AlignCenter);
        auto* cb = new QCheckBox(boxHost);
        cb->setCursor(Qt::PointingHandCursor);
        cb->setStyleSheet(contrastCheckboxWidgetQss());
        cb->setMinimumSize(24, 24);
        cb->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        boxLayout->addWidget(cb);
        m_table->setCellWidget(r, 0, boxHost);

        // name
        auto* nameIt = new QTableWidgetItem(m.name);
        nameIt->setData(Qt::UserRole + 1, m.id);
        m_table->setItem(r, 1, nameIt);

        // percent
        auto* sp = new QDoubleSpinBox(m_table);
        sp->setRange(0, 100);
        sp->setDecimals(2);
        sp->setSuffix(QString::fromUtf8(" %"));
        m_table->setCellWidget(r, 2, sp);

        ServiceMasterShare ss;
        if (findShare(m.id, ss)) {
            cb->setChecked(true);
            sp->setValue(ss.sharePercent);
        } else {
            cb->setChecked(false);
            sp->setValue(0.0);
        }
    }
}

QList<ServiceMasterShare> ServiceMastersDialog::masters() const
{
    QList<ServiceMasterShare> out;
    if (!m_table) return out;

    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto* nameIt = m_table->item(r, 1);
        if (!nameIt) continue;
        const QString id = nameIt->data(Qt::UserRole + 1).toString();
        const QString name = nameIt->text();

        auto* cb = findRowCheckBox(m_table, r);
        auto* sp = qobject_cast<QDoubleSpinBox*>(m_table->cellWidget(r, 2));
        if (!cb || !sp) continue;

        if (!cb->isChecked()) continue;

        ServiceMasterShare s;
        s.masterId = id;
        s.masterName = name;
        s.sharePercent = sp->value();
        out.append(s);
    }

    // validate / normalize to 100
    double sum = 0.0;
    for (const auto& s : out) sum += s.sharePercent;

    if (out.isEmpty()) return out;

    if (sum <= 0.0001) {
        // fallback 100% to first
        out[0].sharePercent = 100.0;
        return out;
    }

    if (qAbs(sum - 100.0) > 0.01) {
        // normalize automatically
        const double k = 100.0 / sum;
        for (auto& s : out) s.sharePercent = clamp(s.sharePercent * k, 0.0, 100.0);
    }
    return out;
}

void ServiceMastersDialog::distributeEqual()
{
    if (!m_table) return;

    // Count checked
    int checked = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto* cb = findRowCheckBox(m_table, r);
        if (cb && cb->isChecked()) checked++;
    }
    if (checked <= 0) {
        QMessageBox::information(this, QString::fromUtf8("Майстри"), QString::fromUtf8("Спочатку виберіть хоча б одного майстра."));
        return;
    }

    const double each = 100.0 / double(checked);
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto* cb = findRowCheckBox(m_table, r);
        auto* sp = qobject_cast<QDoubleSpinBox*>(m_table->cellWidget(r, 2));
        if (!cb || !sp) continue;
        if (cb->isChecked()) sp->setValue(each);
    }
}
