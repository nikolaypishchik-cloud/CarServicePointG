// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/schedule/PlanningPageModulePrelude.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QButtonGroup>
#include <QStyledItemDelegate>

namespace {
class PlanningHourDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.data(Qt::UserRole + 1).toBool()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem styled(option); initStyleOption(&styled, index);
        painter->save();
        painter->setClipRect(option.rect);
        painter->fillRect(option.rect, index.data(Qt::BackgroundRole).value<QBrush>());
        painter->setPen(QColor("#ffffff"));
        painter->drawLine(option.rect.topLeft(), option.rect.topRight());
        painter->setPen(QColor("#62816d"));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
        painter->setFont(styled.font); painter->setPen(QColor("#101418"));
        painter->drawText(option.rect.adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, styled.text);
        painter->restore();
    }
};
}

void PlanningPage::buildUi()
{
    setObjectName("planning_page");
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    // Local stylesheet for Planning page (kept on the widget so it wins over app stylesheet)
    // (removed old page-level stylesheet; styles are applied locally per-widget above)
    auto* rootL = new QVBoxLayout(this);
    rootL->setContentsMargins(6,8,6,8);
    rootL->setSpacing(7);
    // top bar
    auto* top = new QFrame(this);
    top->setObjectName("planning_topbar");
    top->setAttribute(Qt::WA_StyledBackground, true);
    top->setStyleSheet(R"QSS(
        #planning_topbar {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 rgba(22,27,33,0.82),
                                        stop:0.18 rgba(52,61,72,0.72),
                                        stop:0.50 rgba(112,124,138,0.58),
                                        stop:0.82 rgba(54,63,74,0.72),
                                        stop:1 rgba(26,31,37,0.82));
            border: 1px solid rgba(255,255,255,0.24);
            border-radius: 16px;
        }
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,0.14),
                                        stop:0.12 rgba(255,255,255,0.07),
                                        stop:0.13 rgba(63,72,82,0.92),
                                        stop:0.56 rgba(42,48,56,0.96),
                                        stop:1 rgba(26,31,37,0.98));
            border: 1px solid rgba(255,255,255,0.18);
            border-radius: 10px;
            color: #eef3fa;
            font-weight: 600;
            padding: 4px 10px;
        }
        QPushButton:hover { border: 1px solid rgba(220,236,255,0.46); }
        QPushButton:checked { background: #efa409; color: #111111; border: 2px solid #ffffff; }
        QComboBox, QSpinBox {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,0.13),
                                        stop:0.12 rgba(255,255,255,0.06),
                                        stop:0.13 rgba(57,66,76,0.94),
                                        stop:0.58 rgba(35,41,49,0.98),
                                        stop:1 rgba(20,25,31,0.99));
            border: 1px solid rgba(255,255,255,0.20);
            border-radius: 10px;
            padding: 4px 10px;
            color: #eef3fa;
            font-weight: 600;
        }
        QComboBox:hover, QSpinBox:hover {
            border: 1px solid rgba(220,236,255,0.42);
        }
        QComboBox QAbstractItemView {
            background: #202833;
            color: #eef3fa;
            border: 1px solid rgba(220,236,255,0.22);
            selection-background-color: #3a4655;
            selection-color: #ffffff;
            outline: none;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox::down-arrow {
            image: none;
            border-left: 6px solid transparent;
            border-right: 6px solid transparent;
            border-top: 8px solid #eef3fa;
            margin-right: 8px;
        }
    )QSS");
    top->setAttribute(Qt::WA_StyledBackground, true);
    auto* topL = new QHBoxLayout(top);
    topL->setContentsMargins(8,6,8,6);
    topL->setSpacing(8);
    auto* current = new QPushButton(QString::fromUtf8("Поточне планування"), top);
    auto* weekly = new QPushButton(QString::fromUtf8("Тижневе планування"), top);
    current->setObjectName("planningCurrentMode"); weekly->setObjectName("planningWeeklyMode");
    auto* mode = new QButtonGroup(top);
    for (auto* button : {current, weekly}) { button->setCheckable(true); mode->addButton(button); topL->addWidget(button); }
    weekly->setChecked(true);
    topL->addStretch(1);
    m_cbMonth = new QComboBox(top);
    m_cbMonth->setObjectName("planning_month_cb");
    for (int m=1; m<=12; ++m) m_cbMonth->addItem(uaMonth(m), m);
    m_spYear = new QSpinBox(top);
    m_spYear->setObjectName("planning_year_sp");
    m_spYear->setRange(2000, 2100);
    m_spYear->setAlignment(Qt::AlignCenter);
    m_spYear->setMinimumWidth(AdaptiveUi::px(m_spYear, 120));
    m_spYear->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_spYear->setStyleSheet(QStringLiteral(
        "QSpinBox#planning_year_sp { min-width: 5em; padding-right: 10px; }"
        "QSpinBox#planning_year_sp QLineEdit { background: transparent; color: #eef3fa;"
        " border: none; padding: 0px; margin: 0px; min-width: 0px; min-height: 0px; }"));
    m_btnToday = new QPushButton(QString::fromUtf8("Сьогодні"), top);
    m_btnToday->setObjectName("planning_today_btn");
    m_cbMonth->setMinimumHeight(AdaptiveUi::px(m_cbMonth, 30));
    m_spYear->setMinimumHeight(AdaptiveUi::px(m_spYear, 30));
    m_btnToday->setMinimumHeight(AdaptiveUi::px(m_btnToday, 30));
    topL->addWidget(m_cbMonth);
    topL->addWidget(m_spYear);
    topL->addWidget(m_btnToday);
    rootL->addWidget(top);
    // grid
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName("planning_scroll");
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setAttribute(Qt::WA_StyledBackground, true);
    m_scroll->viewport()->setObjectName("planning_scroll_viewport");
    m_scroll->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    m_gridHost = new QWidget(m_scroll);
    m_gridHost->setObjectName("planning_grid_host");
    m_gridHost->setAttribute(Qt::WA_StyledBackground, true);
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setContentsMargins(0,0,0,0);
    m_grid->setHorizontalSpacing(2);
    m_grid->setVerticalSpacing(2);
    m_scroll->setWidget(m_gridHost);
    rootL->addWidget(m_scroll, 1);
    m_dailyPlanning = new QTableWidget(11, 2, this);
    m_dailyPlanning->setObjectName("planningDailyTable");
    m_dailyPlanning->setItemDelegate(new PlanningHourDelegate(m_dailyPlanning));
    m_dailyPlanning->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dailyPlanning->setStyleSheet("QTableWidget{background:white;color:black;} QHeaderView::section{background:#e6edf2;color:black;}");
    m_dailyPlanning->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_dailyPlanning->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_dailyPlanning->verticalHeader()->hide();
    m_dailyPlanning->viewport()->installEventFilter(this);
    rootL->addWidget(m_dailyPlanning, 1); m_dailyPlanning->hide();
    connect(current, &QPushButton::clicked, this, [this] {
        m_cbMonth->hide(); m_spYear->hide(); m_btnToday->hide();
        m_scroll->hide(); m_dailyPlanning->show(); refreshDailyPlanning();
        m_dailyPlanning->scrollToTop();
    });
    connect(weekly, &QPushButton::clicked, this, [this] {
        m_cbMonth->show(); m_spYear->show(); m_btnToday->show();
        m_dailyPlanning->hide(); m_scroll->show(); refresh();
    });
    connect(m_dailyPlanning, &QTableWidget::cellDoubleClicked, this, [this](int, int) { refreshDailyPlanning(); openEditorFor(QDate::currentDate()); });
    auto* dailyTimer = new QTimer(this); dailyTimer->setInterval(60000);
    connect(dailyTimer, &QTimer::timeout, this, [this] {
        if (!isVisible()) return;
        const auto now = QDateTime::currentDateTime();
        const auto previous = m_displayTime.date();
        if (previous.isValid() && previous != now.date() && m_year == previous.year() && m_month == previous.month()) setMonth(now.date());
        refreshDailyPlanning();
        updatePlanningClock(now);
    }); dailyTimer->start();
    // preview popup (tool)
    m_preview = new QFrame(window(), Qt::Tool | Qt::FramelessWindowHint);
    m_preview->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    m_preview->setAttribute(Qt::WA_ShowWithoutActivating);
    m_preview->setAttribute(Qt::WA_StyledBackground);
    m_preview->setObjectName("planning_preview");
    m_preview->setAttribute(Qt::WA_StyledBackground, true);
    m_preview->setAutoFillBackground(true);
    m_preview->setStyleSheet(R"QSS(
        #planning_preview {
            background: rgba(255,255,255,245);
            border: 2px solid #000000;
            border-radius: 18px;
        }
        /*
           ВАЖЛИВО: глобальна темна тема може задавати QLabel/QAbstractButton фон або колір.
           Тут примусово робимо текст видимим і фон прозорим.
        */
        QLabel#planning_preview_title,
        QLabel#planning_preview_body,
        QLabel#planning_preview_hint {
            background: transparent;
            color: #000000;
        }
        QLabel#planning_preview_title {
            font-size: 16pt;
            font-weight: 700;
        }
        #planning_preview_line {
            background: #e6e6e6;
            border: 1px solid #c9c9c9;
            border-radius: 10px;
            padding: 6px 14px;
            color: #000000;
            font-size: 13pt;
        }
        QLabel#planning_preview_body {
            font-size: 13pt;
        }
        QLabel#planning_preview_hint {
            font-size: 12pt;
        }
    )QSS");
    m_preview->setAttribute(Qt::WA_StyledBackground, true);
    auto* pvL = new QVBoxLayout(m_preview);
    pvL->setContentsMargins(18,14,18,14);
    pvL->setSpacing(10);
    m_previewTitle = new QLabel(m_preview);
    m_previewTitle->setObjectName("planning_preview_title");
    m_previewTitle->setAlignment(Qt::AlignHCenter);
    pvL->addWidget(m_previewTitle);
    auto* pvBox = new QFrame(m_preview);
    pvBox->setObjectName("planning_preview_line");
    pvBox->setAttribute(Qt::WA_StyledBackground, true);
    auto* pvBoxL = new QVBoxLayout(pvBox);
    pvBoxL->setContentsMargins(14,10,14,10);
    m_previewBody = new QLabel(pvBox);
    m_previewBody->setObjectName("planning_preview_body");
    m_previewBody->setWordWrap(true);
    pvBoxL->addWidget(m_previewBody);
    pvL->addWidget(pvBox);
    m_previewHint = new QLabel(QString::fromUtf8("Клікніть — щоб відкрити/редагувати"), m_preview);
    m_previewHint->setObjectName("planning_preview_hint");
    m_previewHint->setObjectName("planning_preview_hint");
    m_previewHint->setAlignment(Qt::AlignHCenter);
    pvL->addWidget(m_previewHint);
    m_preview->hide();
    // editor overlay (child, centered)
    m_editor = new QFrame(this);
    m_editor->setObjectName("planning_editor");
    m_editor->setAttribute(Qt::WA_StyledBackground, true);
    m_editor->setAutoFillBackground(true);
    m_editor->setStyleSheet(R"QSS(
        #planning_editor {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #d4d7dc, stop:0.18 #f4f5f7, stop:0.48 #cfd3d8,
                stop:0.72 #eceef0, stop:1 #bcc2c8);
            border: 2px solid #2e7d32;
            border-radius: 18px;
        }
        #planning_editor_header {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #bfc5cb, stop:0.22 #eef0f2, stop:0.52 #c9ced3, stop:1 #adb4bb);
            border-bottom: 1px solid #b9c0c7;
            border-top-left-radius: 18px;
            border-top-right-radius: 18px;
        }
        /* Контрастний текст (не залежить від глобальних тем) */
        QLabel#planning_editor_date {
            background: transparent;
            color: #111111; /* світлий фон -> темний текст */
            font-size: 14pt;
            font-weight: 700;
        }
        QLabel#planning_editor_info {
            background: transparent;
            color: #222222; /* світлий фон -> темний текст */
            font-style: italic;
            font-size: 12pt;
        }
        QLabel#planning_editor_time {
            background: transparent;
            color: #000000;
        }
        #planning_editor_clear {
            background: #efefef;
            border: 1px solid #8a8a8a;
            border-radius: 8px;
            padding: 8px 16px;
            color: #000000;
            font-weight: 600;
        }
        #planning_editor_clear:hover {
            background: #f7f7f7;
        }
        #planning_editor_close {
            background: transparent;
            border: none;
            color: #000000;
            font-size: 18pt;
            font-weight: 900;
            padding: 0px 10px;
        }
        #planning_editor_close:hover {
            background: rgba(0,0,0,30);
            border-radius: 8px;
        }
        QLabel#planning_editor_time {
            font-size: 12pt;
            font-weight: 600;
        }
        QLineEdit {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #fbfbfc, stop:0.25 #ffffff, stop:0.68 #eef1f4, stop:1 #e0e5ea);
            border: 1px solid #b0b7bf;
            border-radius: 10px;
            padding: 6px 12px;
            color: #000000;
            font-size: 12pt;
        }
        QLineEdit:focus {
            border: 2px solid #2e7d32;
        }
    )QSS");
    m_editor->setAttribute(Qt::WA_StyledBackground);
    m_editor->hide();
    // editor open/close animation
    m_editorOpacity = new QGraphicsOpacityEffect(m_editor);
    m_editorOpacity->setOpacity(0.0);
    m_editor->setGraphicsEffect(m_editorOpacity);
    m_editorGeomAnim = new QPropertyAnimation(m_editor, "geometry", m_editor);
    m_editorOpacityAnim = new QPropertyAnimation(m_editorOpacity, "opacity", m_editor);
    m_editorAnimGroup = new QParallelAnimationGroup(m_editor);
    m_editorAnimGroup->addAnimation(m_editorGeomAnim);
    m_editorAnimGroup->addAnimation(m_editorOpacityAnim);
    m_editorGeomAnim->setDuration(220);
    m_editorOpacityAnim->setDuration(220);
    m_editorGeomAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_editorOpacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_editorAnimGroup, &QParallelAnimationGroup::finished, this, [this]() {
        // if opacity is 0 at end, ensure it's hidden (close animation)
        if (m_editorOpacity && m_editorOpacity->opacity() <= 0.001) {
            m_editor->hide();
        }
    });
    auto* edRoot = new QVBoxLayout(m_editor);
    edRoot->setContentsMargins(14,14,14,14);
    edRoot->setSpacing(6);
    auto* edHeader = new QFrame(m_editor);
    edHeader->setObjectName("planning_editor_header");
    auto* edHL = new QHBoxLayout(edHeader);
    edHL->setContentsMargins(14,10,14,10);
    edHL->setSpacing(8);
    m_editorDate = new QLabel(edHeader);
    m_editorDate->setObjectName("planning_editor_date");
    m_editorDate->setMinimumWidth(AdaptiveUi::px(m_editorDate, 110));
    edHL->addWidget(m_editorDate);
    edHL->addStretch(1);
    m_btnClear = new QPushButton(QString::fromUtf8("Очистити"), edHeader);
    m_btnClear->setObjectName("planning_editor_clear");
    m_btnClear->setMinimumHeight(AdaptiveUi::px(m_btnClear, 34));
    m_btnClose = new QPushButton(QString::fromUtf8("✕"), edHeader);
    m_btnClose->setObjectName("planning_editor_close");
    m_btnClose->setMinimumSize(AdaptiveUi::px(m_btnClose, 34), AdaptiveUi::px(m_btnClose, 34));
    edHL->addWidget(m_btnClear);
    edHL->addWidget(m_btnClose);
    edRoot->addWidget(edHeader);
    m_editorInfo = new QLabel(QString::fromUtf8("Запланованих робіт нема"), m_editor);
    m_editorInfo->setObjectName("planning_editor_info");
    edRoot->addWidget(m_editorInfo);
    auto* slotsHost = new QWidget(m_editor);
    slotsHost->setObjectName("planning_editor_slots");
    auto* slotsL = new QGridLayout(slotsHost);
    slotsL->setContentsMargins(0,0,0,0);
    slotsL->setHorizontalSpacing(1);
    slotsL->setVerticalSpacing(1);
    const int startHour = 8;
    const int endHour = 18;
    for (int h=startHour, row=0; h<=endHour; ++h, ++row) {
        SlotRow r;
        r.timeLabel = new QLabel(QString("%1:00").arg(h, 2, 10, QChar('0')), slotsHost);
        r.timeLabel->setObjectName("planning_editor_time");
        r.timeLabel->setMinimumWidth(AdaptiveUi::px(r.timeLabel, 52));
        r.timeLabel->setMaximumWidth(AdaptiveUi::px(r.timeLabel, 72));
        r.timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        r.edit = new QLineEdit(slotsHost);
        r.edit->setObjectName("planning_editor_edit");
        r.edit->setProperty("inputCorrectionRole", "planning");
        r.edit->setMinimumHeight(32);
        r.edit->setContextMenuPolicy(Qt::CustomContextMenu);
        const int slotHour = h;
        connect(r.edit, &QWidget::customContextMenuRequested, this, [this, slotHour, edit=r.edit](const QPoint& pos) {
            QMenu menu;
            QAction* actMove = menu.addAction(QString::fromUtf8("Перенести…"));
            QAction* actCancel = menu.addAction(QString::fromUtf8("Скасувати запис"));
            menu.addSeparator();
            QAction* actCopy = menu.addAction(QString::fromUtf8("Копіювати текст"));
            QAction* actPaste = menu.addAction(QString::fromUtf8("Вставити текст"));
            QAction* picked = menu.exec(edit->mapToGlobal(pos));
            if (!picked) return;
            if (picked == actCopy) { edit->copy(); return; }
            if (picked == actPaste) { edit->paste(); return; }
            if (picked == actCancel) {
                if (edit->text().trimmed().isEmpty()) return;
                if (QMessageBox::question(this,
                        QString::fromUtf8("Підтвердження"),
                        QString::fromUtf8("Скасувати запис о %1:00 на %2?")
                            .arg(slotHour,2,10,QChar('0'))
                            .arg(m_editDate.toString("dd.MM.yy")))
                    != QMessageBox::Yes) return;
                edit->clear();
                updateEditorHeader();
                return;
            }
            if (picked == actMove) {
                if (edit->text().trimmed().isEmpty()) return;
                QDialog dlg(this);
                dlg.setWindowTitle(QString::fromUtf8("Перенесення запису"));
                QVBoxLayout* l = new QVBoxLayout(&dlg);
                QDateEdit* d = new QDateEdit(m_editDate, &dlg);
                d->setCalendarPopup(true);
                QSpinBox* hh = new QSpinBox(&dlg);
                hh->setRange(8,18);
                hh->setValue(slotHour);
                hh->setSuffix(QString::fromUtf8(" год"));
                QHBoxLayout* row = new QHBoxLayout();
                row->addWidget(new QLabel(QString::fromUtf8("Дата:"), &dlg));
                row->addWidget(d);
                row->addSpacing(12);
                row->addWidget(new QLabel(QString::fromUtf8("Час:"), &dlg));
                row->addWidget(hh);
                l->addLayout(row);
                QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel, &dlg);
                l->addWidget(bb);
                connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
                connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
                if (dlg.exec() != QDialog::Accepted) return;
                const QDate nd = d->date();
                const int nh = hh->value();
                if (m_repo && m_repo->getDayMap(nd).contains(nh)) {
                    QMessageBox::warning(this, QString::fromUtf8("Планування"), QString::fromUtf8("Цей час зайнятий. Оберіть інший час."));
                    return;
                }
                const QString txt = edit->text().trimmed();
                // remove from source slot
                edit->clear();
                // load destination map from cache/db
                QMap<int, QString> dest = m_plan.value(nd);
                if (dest.isEmpty() && m_repo) dest = m_repo->visibleDayMap(nd);
                dest[nh] = txt;
                m_plan[nd] = dest;
                if (m_repo) m_repo->setVisibleDayMap(nd, dest);
                if (m_reminderDay == m_editDate || m_reminderDay == nd) {
                    const QString sourcePrefix = QStringLiteral("%1|").arg(m_editDate.toString(Qt::ISODate));
                    const QString destPrefix = QStringLiteral("%1|").arg(nd.toString(Qt::ISODate));
                    for (auto itKey = m_remindedKeys.begin(); itKey != m_remindedKeys.end(); ) {
                        if (itKey->startsWith(sourcePrefix) || itKey->startsWith(destPrefix)) itKey = m_remindedKeys.erase(itKey);
                        else ++itKey;
                    }
                }
                updateEditorHeader();
                rebuildGrid();
                if (nd.year()==m_year && nd.month()==m_month) openEditorFor(nd);
                return;
            }
        });
        slotsL->addWidget(r.timeLabel, row, 0);
        slotsL->addWidget(r.edit, row, 1);
        m_rows.push_back(r);
    }
    edRoot->addWidget(slotsHost, 1);
    // connections
    connect(m_btnToday, &QPushButton::clicked, this, [this]() {
        setMonth(QDate::currentDate());
    });
    connect(m_cbMonth, &QComboBox::currentIndexChanged, this, [this](int) {
        const int m = m_cbMonth->currentData().toInt();
        if (m != m_month) setMonth(QDate(m_year, m, 1));
    });
    connect(m_spYear, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int y) {
        if (y != m_year) setMonth(QDate(y, m_month, 1));
    });
    connect(m_btnClose, &QPushButton::clicked, this, [this]() { closeEditor(true); });
    connect(m_btnClear, &QPushButton::clicked, this, [this]() {
        if (!m_editDate.isValid()) return;
        if (QMessageBox::question(this,
                QString::fromUtf8("Підтвердження"),
                QString::fromUtf8("Скасувати всі записи на %1?\n\nДія незворотна.")
                    .arg(m_editDate.toString("dd.MM.yy")))
            != QMessageBox::Yes) {
            return;
        }
        for (auto& r : m_rows) r.edit->clear();
        m_plan.remove(m_editDate);
        if (m_repo) m_repo->setVisibleDayMap(m_editDate, {});
        updateEditorHeader();
    });
    // style (local to planning)
    setStyleSheet(QString::fromUtf8(R"QSS(
        #planning_page,
        QScrollArea#planning_scroll,
        QWidget#planning_scroll_viewport,
        QWidget#planning_grid_host {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(25,31,39,0.98),
                                        stop:0.08 rgba(56,67,81,0.98),
                                        stop:0.18 rgba(96,109,125,0.92),
                                        stop:0.28 rgba(69,82,97,0.96),
                                        stop:0.64 rgba(39,48,60,0.98),
                                        stop:1 rgba(55,67,81,0.98));
            border: none;
        }
        #planning_topbar {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 rgba(22,27,33,0.82),
                                        stop:0.18 rgba(52,61,72,0.72),
                                        stop:0.50 rgba(112,124,138,0.58),
                                        stop:0.82 rgba(54,63,74,0.72),
                                        stop:1 rgba(26,31,37,0.82));
            border: 1px solid rgba(255,255,255,0.24);
            border-radius: 16px;
        }
        #planning_today_btn {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,0.14),
                                        stop:0.12 rgba(255,255,255,0.07),
                                        stop:0.13 rgba(63,72,82,0.92),
                                        stop:0.56 rgba(42,48,56,0.96),
                                        stop:1 rgba(26,31,37,0.98));
            border: 1px solid rgba(255,255,255,0.18);
            border-radius: 10px;
            font-weight: 700;
            color: #eef3fa;
            padding: 4px 10px;
            min-height: 30px;
        }
        #planning_today_btn:hover { border: 1px solid rgba(220,236,255,0.46); }
        #planning_month_cb, #planning_year_sp {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(255,255,255,0.13),
                                        stop:0.12 rgba(255,255,255,0.06),
                                        stop:0.13 rgba(57,66,76,0.94),
                                        stop:0.58 rgba(35,41,49,0.98),
                                        stop:1 rgba(20,25,31,0.99));
            border: 1px solid rgba(255,255,255,0.20);
            border-radius: 10px;
            padding: 4px 10px;
            min-height: 30px;
            color: #eef3fa;
            font-weight: 700;
        }
        #planning_month_cb:hover, #planning_year_sp:hover {
            border: 1px solid rgba(220,236,255,0.42);
        }
        #planning_month_cb QAbstractItemView {
            background: #202833;
            color: #eef3fa;
            border: 1px solid rgba(220,236,255,0.22);
            selection-background-color: #3a4655;
            selection-color: #ffffff;
            outline: none;
        }
        #planning_daycell {
            background: #ffffff;
            border: 1px solid rgba(76,84,92,0.55);
            border-radius: 8px;
        }
        #planning_daycell[selected="true"] {
            border: 2px solid #2e7d32;
        }
        #planning_daycell_header {
            background: #f2f2f2;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            border-bottom: 1px solid rgba(120,128,136,0.45);
            color: #111111;
            font-weight: 800;
        }
        #planning_daycell_body { color: rgba(0,0,0,0.55); }
        /* preview */
        #planning_preview {
            background: #ffffff;
            border: 2px solid rgba(0,0,0,0.70);
            border-radius: 14px;
        }
        #planning_preview_title { font-weight: 800; color: #111; }
        #planning_preview_box {
            background: #f1f1f1;
            border: 1px solid rgba(0,0,0,0.18);
            border-radius: 10px;
        }
        #planning_preview_body { color: rgba(0,0,0,0.75); }
        #planning_preview_hint { color: rgba(0,0,0,0.65); }
        /* editor overlay */
        #planning_editor {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #d4d7dc, stop:0.18 #f4f5f7, stop:0.48 #cfd3d8,
                stop:0.72 #eceef0, stop:1 #bcc2c8);
            border: 2px solid #2e7d32;
            border-radius: 18px;
        }
        #planning_editor_header {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #bfc5cb, stop:0.22 #eef0f2, stop:0.52 #c9ced3, stop:1 #adb4bb);
            border: 1px solid rgba(120,128,136,0.28);
            border-radius: 12px;
        }
        QLabel#planning_editor_date {
            font-size: 18px;
            font-weight: 900;
            background: transparent;
            color: #111111;
            border: none;
            border-radius: 0px;
            padding: 2px 6px;
        }
        #planning_editor_clear, #planning_editor_close {
            background: #f0f0f0;
            border: 1px solid rgba(0,0,0,0.35);
            border-radius: 10px;
            font-weight: 800;
            color: #111111;
        }
        #planning_editor_clear:hover, #planning_editor_close:hover { background: #ffffff; }
        QLabel#planning_editor_info {
            color: rgba(0,0,0,0.92);
            font-style: italic;
            background: transparent;
            margin-left: 0px;
            margin-bottom: 0px;
        }
        #planning_editor_slots {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #83909d, stop:0.18 #a4b0bb, stop:0.48 #c6d0d9,
                stop:0.78 #b3bec8, stop:1 #96a2af);
            border: 1px solid rgba(255,255,255,0.24);
            border-radius: 0px;
        }
        QLabel#planning_editor_time {
            font-size: 18px;
            font-weight: 800;
            background: transparent;
            color: #000000;
            border: none;
            border-radius: 0px;
            padding: 0px 8px 0px 0px;
        }
        #planning_editor_edit {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 #fdfefe, stop:0.20 #ffffff, stop:0.50 #f0f4f8, stop:0.82 #e3e9ef, stop:1 #d6dde5);
            border: 1px solid rgba(105,114,123,0.34);
            border-radius: 12px;
            padding-left: 10px;
            color: #111111;
        }
        #planning_editor_edit:focus { border: 1px solid #2e7d32; }
    )QSS"));
}
