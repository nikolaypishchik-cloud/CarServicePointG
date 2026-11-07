#include "ui/tabs/settings/SettingsPersonnelManageSectionSupport.h"
#include <functional>

#include "core/services/settings/PersonnelSettingsService.h"
#include "ui/tabs/settings/SettingsPageDataSupport.h"
#include "ui/tabs/settings/SettingsPersonnelSectionSupport.h"

#include <QApplication>
#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPointer>
#include <QSignalBlocker>
#include <QPushButton>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace {

static QString cPrimary()
{
    return QStringLiteral("#f3f7fc");
}

static QString settingsStandardButtonStyle(bool compactBackButton = false)
{
    const QString borderRadius = compactBackButton ? QStringLiteral("12px") : QStringLiteral("18px");
    const QString padding = compactBackButton ? QStringLiteral("0 12px") : QStringLiteral("0 24px");
    const QString fontSize = compactBackButton ? QStringLiteral("10.5pt") : QStringLiteral("11pt");
    return QString::fromUtf8(
        "QPushButton{" 
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 rgba(214,224,236,0.82),"
        " stop:0.14 rgba(154,168,184,0.92),"
        " stop:0.42 rgba(95,109,127,0.98),"
        " stop:0.48 rgba(40,52,68,1.0),"
        " stop:0.49 rgba(8,17,30,1.0),"
        " stop:1 rgba(4,12,24,1.0));"
        "color:#f8fbff;"
        "border:1px solid rgba(255,255,255,0.90);"
        "border-top:1px solid rgba(255,255,255,0.98);"
        "border-radius:%1;"
        "padding:%2;"
        "font-size:%3;"
        "font-weight:800;"
        "}"
        "QPushButton:hover{" 
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 rgba(228,236,246,0.90),"
        " stop:0.14 rgba(170,184,198,0.96),"
        " stop:0.42 rgba(108,123,141,1.0),"
        " stop:0.48 rgba(48,61,78,1.0),"
        " stop:0.49 rgba(10,21,36,1.0),"
        " stop:1 rgba(6,14,28,1.0));"
        "border:1px solid rgba(255,255,255,0.98);"
        "border-top:1px solid rgba(255,255,255,1.0);"
        "}"
        "QPushButton:pressed{" 
        "padding-top:1px;"
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 rgba(40,52,68,1.0),"
        " stop:0.40 rgba(18,29,43,1.0),"
        " stop:1 rgba(6,14,28,1.0));"
        "border:1px solid rgba(255,255,255,0.74);"
        "}"
        "QPushButton:disabled{" 
        "background:rgba(112,122,136,0.46);"
        "color:#dbe4ef;"
        "border:1px solid rgba(255,255,255,0.28);"
        "}")
        .arg(borderRadius, padding, fontSize);
}

static QPushButton* makeSettingsBackButton(QWidget* parent)
{
    auto* b = new QPushButton(QString::fromUtf8("← Назад"), parent);
    b->setFixedSize(120, 40);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(settingsStandardButtonStyle(false));
    return b;
}

static void applySettingsButtonLikeBack(QPushButton* button, int fixedWidth = 0)
{
    if (!button) {
        return;
    }
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(52);
    button->setMaximumHeight(52);
    if (fixedWidth > 0) {
        button->setFixedSize(fixedWidth, 52);
    } else {
        button->setMinimumWidth(120);
    }
    button->setStyleSheet(settingsStandardButtonStyle(false));
}

class InlineWordDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QString displayText(const QVariant& value, const QLocale& locale) const override
    {
        QString text = QStyledItemDelegate::displayText(value, locale);
        text.replace('\n', ' ');
        return text.simplified();
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(option);
        if (index.column() == 2) {
            auto* editor = new QDoubleSpinBox(parent);
            editor->setRange(0.0, 100.0);
            editor->setDecimals(2);
            editor->setSingleStep(1.0);
            editor->setKeyboardTracking(false);
            editor->setFrame(false);
            editor->setStyleSheet(QStringLiteral(
                "QDoubleSpinBox{background:#ffffff;color:#111111;border:1px solid #4d86d9;"
                "padding:0 6px;selection-background-color:#d7e4ff;selection-color:#111111;}"));
            QTimer::singleShot(0, editor, [editor]() {
                if (editor) {
                    editor->setFocus();
                    editor->selectAll();
                }
            });
            return editor;
        }

        QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto* lineEdit = qobject_cast<QLineEdit*>(editor)) {
            lineEdit->setStyleSheet(QStringLiteral(
                "QLineEdit{background:#ffffff;color:#111111;border:1px solid #4d86d9;"
                "padding:0 6px;selection-background-color:#d7e4ff;selection-color:#111111;}"));
            QTimer::singleShot(0, lineEdit, [lineEdit]() {
                if (lineEdit) {
                    lineEdit->setFocus();
                    lineEdit->selectAll();
                }
            });
        }
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
            QString text = index.model() ? index.model()->data(index, Qt::EditRole).toString().trimmed() : QString();
            text.remove(QChar('%'));
            text.replace(',', '.');
            bool ok = false;
            double value = text.toDouble(&ok);
            if (!ok) {
                value = 0.0;
            }
            value = std::clamp(value, 0.0, 100.0);
            spin->setValue(value);
            QTimer::singleShot(0, spin, [spin]() {
                if (spin) {
                    spin->setFocus();
                    spin->selectAll();
                }
            });
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
        if (auto* lineEdit = qobject_cast<QLineEdit*>(editor)) {
            QTimer::singleShot(0, lineEdit, [lineEdit]() {
                if (lineEdit) {
                    lineEdit->selectAll();
                }
            });
        }
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(editor)) {
            if (model) {
                model->setData(index, QString::number(spin->value(), 'f', 2), Qt::EditRole);
            }
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(index);
        if (editor) {
            editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
        }
    }
};

} // namespace

namespace SettingsPersonnelManageSectionSupport {

QWidget* build(QWidget* parent, const std::function<void()>& queueRefreshAll)
{
    auto* w = new QWidget(parent);
    auto* rootL = new QVBoxLayout(w);
    rootL->setContentsMargins(12, 12, 12, 12);
    rootL->setSpacing(12);

    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(12);
    auto* btnBack = makeSettingsBackButton(w);
    auto* title = new QLabel(QString::fromUtf8("Майстри та адміністратори"), w);
    title->setStyleSheet(QString("font-size:16pt; font-weight:800; color:%1;").arg(cPrimary()));
    title->setAlignment(Qt::AlignCenter);
    auto* btnCancel = new QPushButton(QString::fromUtf8("Скасувати"), w);
    auto* btnSave = new QPushButton(QString::fromUtf8("Зберегти"), w);
    applySettingsButtonLikeBack(btnCancel, 120);
    applySettingsButtonLikeBack(btnSave, 120);
    btnCancel->setFixedSize(120, 40);
    btnSave->setFixedSize(120, 40);
    header->addWidget(btnBack, 0, Qt::AlignVCenter);
    header->addStretch(1);
    header->addWidget(title, 0, Qt::AlignCenter);
    header->addStretch(1);
    header->addWidget(btnCancel, 0, Qt::AlignVCenter);
    header->addWidget(btnSave, 0, Qt::AlignVCenter);
    rootL->addLayout(header);

    auto* scroll = new QScrollArea(w);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    rootL->addWidget(scroll, 1);

    auto* scrollHost = new QWidget(scroll);
    auto* scrollL = new QVBoxLayout(scrollHost);
    scrollL->setContentsMargins(0, 0, 0, 0);
    scrollL->setSpacing(4);
    scroll->setWidget(scrollHost);

    auto* mastersFrame = new QFrame(scrollHost);
    mastersFrame->setFrameShape(QFrame::StyledPanel);
    auto* mastersL = new QVBoxLayout(mastersFrame);
    mastersL->setContentsMargins(14, 12, 14, 6);
    mastersL->setSpacing(6);

    auto* mastersLbl = new QLabel(QString::fromUtf8("Майстри"), mastersFrame);
    mastersLbl->setStyleSheet(QString("font-size:12pt; font-weight:700; color:%1;").arg(cPrimary()));
    mastersL->addWidget(mastersLbl, 0, Qt::AlignLeft);

    auto* mastersBar = new QHBoxLayout();
    mastersBar->setContentsMargins(0, 0, 0, 0);
    mastersBar->setSpacing(6);
    auto* btnAddMaster = new QPushButton(QString::fromUtf8("+ Додати"), mastersFrame);
    auto* btnDeleteMaster = new QPushButton(QString::fromUtf8("Видалити"), mastersFrame);
    applySettingsButtonLikeBack(btnAddMaster);
    applySettingsButtonLikeBack(btnDeleteMaster);
    mastersBar->addWidget(btnAddMaster);
    mastersBar->addWidget(btnDeleteMaster);
    mastersBar->addStretch(1);
    mastersL->addLayout(mastersBar);

    auto* mastersTable = new QTableWidget(mastersFrame);
    mastersTable->setColumnCount(3);
    mastersTable->setHorizontalHeaderLabels({QStringLiteral("ID"), QString::fromUtf8("Ім'я"), QString::fromUtf8("% майстра")});
    mastersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mastersTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    mastersTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    mastersTable->verticalHeader()->setVisible(false);
    mastersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    mastersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    mastersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    mastersTable->setColumnWidth(2, 170);
    mastersTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    mastersTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mastersTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mastersTable->setAlternatingRowColors(true);
    mastersTable->setItemDelegate(new InlineWordDelegate(mastersTable));
    auto syncMastersTableHeight = [mastersTable]() {
        SettingsPersonnelSectionSupport::syncMastersTableHeight(mastersTable);
    };
    mastersL->addWidget(mastersTable, 0);
    scrollL->addWidget(mastersFrame);

    auto* adminFrame = new QFrame(scrollHost);
    adminFrame->setFrameShape(QFrame::StyledPanel);
    adminFrame->setStyleSheet(QString::fromUtf8(
        "QLineEdit,QComboBox,QDoubleSpinBox{background:#ffffff;color:#111111;border:1px solid #c7ccd4;"
        "border-radius:4px;min-height:38px;padding:6px 12px;font-size:11pt;"
        "selection-background-color:#d7e4ff;selection-color:#111111;}"
        "QLineEdit:focus,QComboBox:focus,QDoubleSpinBox:focus{border:1px solid #4d86d9;}"
        "QLineEdit:disabled,QComboBox:disabled,QDoubleSpinBox:disabled{background:#f4f4f4;color:#555555;}"
        "QComboBox::drop-down{border-left:1px solid #c7ccd4;width:28px;background:#f3f6fa;}"
        "QComboBox QAbstractItemView{background:#ffffff;color:#111111;selection-background-color:#dfe6ef;selection-color:#111111;}"
        "QLabel{font-size:11pt;color:%1;margin:0;padding:0;}").arg(cPrimary()));
    auto* adminL = new QVBoxLayout(adminFrame);
    adminL->setContentsMargins(14, 6, 14, 10);
    adminL->setSpacing(4);

    auto* adminLbl = new QLabel(QString::fromUtf8("Адмін"), adminFrame);
    adminLbl->setStyleSheet(QString("font-size:12pt; font-weight:700; color:%1;").arg(cPrimary()));
    adminL->addWidget(adminLbl, 0, Qt::AlignLeft);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(4);

    auto* adminName = new QLineEdit(adminFrame);
    adminName->setMinimumHeight(38);
    auto* adminPercent = new QDoubleSpinBox(adminFrame);
    adminPercent->setMinimumHeight(38);
    adminPercent->setRange(0.0, 100.0);
    adminPercent->setDecimals(2);
    adminPercent->setSuffix(QString::fromUtf8("%"));
    adminPercent->setSingleStep(1.0);

    auto* adminSource = new QComboBox(adminFrame);
    adminSource->setMinimumHeight(38);
    auto* adminSourceView = new QListView(adminSource);
    adminSource->setView(adminSourceView);
    adminSourceView->setStyleSheet(QStringLiteral(
        "QListView{background:#ffffff;color:#111111;border:1px solid #c7ccd4;outline:none;}"
        "QListView::item{background:#ffffff;color:#111111;padding:8px 10px;min-height:24px;}"
        "QListView::item:selected{background:#dfe6ef;color:#111111;}"));
    QPalette adminSourcePalette = adminSource->palette();
    adminSourcePalette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
    adminSourcePalette.setColor(QPalette::Text, QColor(QStringLiteral("#111111")));
    adminSourcePalette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#111111")));
    adminSourcePalette.setColor(QPalette::Highlight, QColor(QStringLiteral("#dfe6ef")));
    adminSource->setPalette(adminSourcePalette);
    adminSource->addItem(QString::fromUtf8("Повна ціна"), QStringLiteral("client_total"));
    adminSource->addItem(QString::fromUtf8("Маржа"), QStringLiteral("margin"));
    adminSource->addItem(QString::fromUtf8("Ціна закупки"), QStringLiteral("purchase_total"));

    form->addRow(QString::fromUtf8("Ім'я"), adminName);
    form->addRow(QString::fromUtf8("Відсоток"), adminPercent);
    form->addRow(QString::fromUtf8("База нарахування"), adminSource);
    adminL->addLayout(form);

    auto* adminButtons = new QHBoxLayout();
    adminButtons->setContentsMargins(0, 2, 0, 0);
    adminButtons->setSpacing(6);
    auto* btnNewAdmin = new QPushButton(QString::fromUtf8("Новий"), adminFrame);
    auto* btnDeleteAdmin = new QPushButton(QString::fromUtf8("Очистити"), adminFrame);
    applySettingsButtonLikeBack(btnNewAdmin);
    applySettingsButtonLikeBack(btnDeleteAdmin);
    adminButtons->addWidget(btnNewAdmin);
    adminButtons->addWidget(btnDeleteAdmin);
    adminButtons->addStretch(1);
    adminL->addLayout(adminButtons);
    scrollL->addWidget(adminFrame);
    scrollL->addStretch(1);

    auto currentAdminId = std::make_shared<QString>();

    auto reloadUi = [parent, mastersTable, adminName, adminPercent, adminSource, currentAdminId, syncMastersTableHeight]() {
        const auto snapshot = PersonnelSettingsService::loadSnapshot();
        SettingsPersonnelSectionSupport::applySnapshotToUi(mastersTable,
                                                           adminName,
                                                           adminPercent,
                                                           adminSource,
                                                           snapshot,
                                                           currentAdminId.get());
        syncMastersTableHeight();
        Q_UNUSED(parent);
    };

    auto saveUi = [parent, mastersTable, adminName, adminPercent, adminSource, currentAdminId, queueRefreshAll]() {
        if (QWidget* fw = QApplication::focusWidget()) {
            fw->clearFocus();
        }
        mastersTable->clearFocus();
        mastersTable->viewport()->update();

        const auto payload = SettingsPersonnelSectionSupport::buildSavePayload(
            mastersTable,
            adminName,
            adminPercent,
            adminSource,
            currentAdminId ? *currentAdminId : QString());

        const auto result = PersonnelSettingsService::save(payload);
        if (!result.success) {
            QMessageBox::critical(parent,
                                  QString::fromUtf8("Помилка"),
                                  result.errorText.isEmpty() ? QString::fromUtf8("Не вдалося зберегти персонал.") : result.errorText);
            return;
        }

        if (currentAdminId) {
            *currentAdminId = result.currentAdminId;
        }
        if (result.anyCommittedChange) {
            const QString summary = result.adminChanged
                ? QString::fromUtf8("Збережено персонал: майстрів %1, адміністратора оновлено").arg(result.affectedMasterRows)
                : QString::fromUtf8("Збережено персонал: майстрів %1").arg(result.affectedMasterRows);
            SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("staff_manage_save"),
                                                               QStringLiteral("personnel"),
                                                               QStringLiteral("settings.staff.manage"),
                                                               summary,
                                                               {QStringLiteral("masters"), QStringLiteral("settings")});
        }
        if (queueRefreshAll) {
            queueRefreshAll();
        }
        QMessageBox::information(parent,
                                 QString::fromUtf8("Готово"),
                                 QString::fromUtf8("Майстри й адміністратор успішно збережені."));
    };

    QObject::connect(mastersTable, &QTableWidget::itemChanged, w, [mastersTable](QTableWidgetItem* item) {
        if (!mastersTable || !item || item->column() != 2) {
            return;
        }
        QString text = item->text().trimmed();
        text.remove(QChar('%'));
        text.replace(',', '.');
        bool ok = false;
        double value = text.toDouble(&ok);
        if (!ok) {
            value = 0.0;
        }
        value = std::clamp(value, 0.0, 100.0);
        const QString normalized = QString::number(value, 'f', 2);
        if (item->text() != normalized) {
            const QSignalBlocker blocker(mastersTable);
            item->setText(normalized);
        }
    });

    QObject::connect(btnAddMaster, &QPushButton::clicked, w, [mastersTable, syncMastersTableHeight]() {
        SettingsPersonnelSectionSupport::appendEmptyMasterRow(mastersTable);
        syncMastersTableHeight();
        const int r = mastersTable->rowCount() - 1;
        mastersTable->setCurrentCell(r, 1);
        mastersTable->editItem(mastersTable->item(r, 1));
    });
    QObject::connect(btnDeleteMaster, &QPushButton::clicked, w, [parent, mastersTable, reloadUi, queueRefreshAll]() {
        const auto rows = mastersTable->selectionModel() ? mastersTable->selectionModel()->selectedRows() : QModelIndexList();
        if (rows.isEmpty()) return;

        QList<int> orderedRows;
        orderedRows.reserve(rows.size());
        for (const QModelIndex& idx : rows) orderedRows.append(idx.row());
        std::sort(orderedRows.begin(), orderedRows.end(), std::greater<int>());

        QStringList deletedIds;
        QStringList deletedNames;
        for (int row : orderedRows) {
            const QString id = mastersTable->item(row, 0) ? mastersTable->item(row, 0)->data(Qt::UserRole).toString() : QString();
            const QString name = mastersTable->item(row, 1) ? mastersTable->item(row, 1)->text().trimmed() : QString();
            if (!id.trimmed().isEmpty()) {
                deletedIds << id.trimmed();
            }
            if (!name.isEmpty()) deletedNames << name;
            mastersTable->removeRow(row);
        }

        QString errorText;
        if (!PersonnelSettingsService::deleteMasters(deletedIds, &errorText)) {
            QMessageBox::critical(parent, QString::fromUtf8("Помилка"), errorText.isEmpty() ? QString::fromUtf8("Не вдалося видалити або архівувати майстра.") : errorText);
            return;
        }

        SettingsPersonnelSectionSupport::renumberMasterRows(mastersTable);

        if (!deletedNames.isEmpty()) {
            SettingsPageDataSupport::publishSettingsBackupEvent(QStringLiteral("staff_master_delete"),
                                                               QStringLiteral("master"),
                                                               QStringLiteral("settings.staff.manage"),
                                                               QString::fromUtf8("Видалено/архівовано майстрів: %1").arg(deletedNames.join(QStringLiteral(", "))),
                                                               {QStringLiteral("masters"), QStringLiteral("settings")});
        }

        reloadUi();
        if (queueRefreshAll) {
            queueRefreshAll();
        }
    });
    QObject::connect(btnDeleteAdmin, &QPushButton::clicked, w, [adminName, adminPercent, adminSource, currentAdminId]() {
        adminName->clear();
        adminPercent->setValue(0.0);
        adminSource->setCurrentIndex(0);
        *currentAdminId = QString();
    });
    QObject::connect(btnNewAdmin, &QPushButton::clicked, w, [adminName, adminPercent, adminSource, currentAdminId]() {
        adminName->clear();
        adminPercent->setValue(0.0);
        adminSource->setCurrentIndex(0);
        *currentAdminId = QString();
        adminName->setFocus();
    });
    QObject::connect(btnCancel, &QPushButton::clicked, w, reloadUi);
    QObject::connect(btnSave, &QPushButton::clicked, w, saveUi);

    QTimer::singleShot(0, w, reloadUi);
    return w;
}

} // namespace SettingsPersonnelManageSectionSupport