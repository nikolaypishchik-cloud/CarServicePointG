#pragma once
#include "core/import/InvoiceImportTypes.h"
#include "core/services/orders/PartImportTextSupport.h"
#include "core/util/DateFormats.h"
#include "ui/common/widgets/InvoiceImportProgressDialog.h"
#include <QTableView>
#include "ui/common/dialogs/InvoiceReviewTableModel.h"
#include <QHeaderView>
#include <QScrollBar>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QMessageBox>
#include <QCloseEvent>
#include <cmath>
#include <QSignalBlocker>
#include <QLocale>
#include <QBrush>
#include <QStyledItemDelegate>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>

inline void paintInvoiceCheck(QPainter& p, const QRect& bounds, bool checked) {
    p.setRenderHint(QPainter::Antialiasing);const QRectF box(bounds.center().x()-8,bounds.center().y()-8,17,17);
    p.setPen(QPen(QColor("#d9dfe5"),1));p.setBrush(checked?QColor("#d4dce5"):QColor("#303a44"));p.drawRoundedRect(box,3,3);
    if(checked){p.setPen(QPen(QColor("#26313e"),2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));QPainterPath mark;mark.moveTo(box.left()+4,box.top()+9);mark.lineTo(box.left()+7,box.top()+12);mark.lineTo(box.left()+13,box.top()+5);p.drawPath(mark);}
}
class InvoiceSelectAllCheck final : public QCheckBox {
public: using QCheckBox::QCheckBox;
protected:
    bool hitButton(const QPoint& point) const override { return rect().contains(point); }
    void paintEvent(QPaintEvent*) override {QPainter p(this);paintInvoiceCheck(p,rect(),isChecked());}
};
class InvoiceSelectionDelegate final : public QStyledItemDelegate {
public:using QStyledItemDelegate::QStyledItemDelegate;
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option, const QModelIndex& index) override {
        if(index.column()!=0)return QStyledItemDelegate::editorEvent(event,model,option,index);
        if(!(index.flags() & Qt::ItemIsEnabled) || !(index.flags() & Qt::ItemIsUserCheckable))return false;
        // The indicator is painted in the centre, not in the native style's
        // left-aligned check rectangle. Use the entire selection cell as its hit area.
        if(event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseButtonDblClick || event->type()==QEvent::MouseButtonRelease){
            auto* mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()!=Qt::LeftButton || !option.rect.contains(mouse->position().toPoint()))return false;
            if(event->type()!=QEvent::MouseButtonRelease)return true;
        }else if(event->type()==QEvent::KeyPress){
            auto* key=static_cast<QKeyEvent*>(event);
            if(key->key()!=Qt::Key_Space && key->key()!=Qt::Key_Select)return false;
            if(key->isAutoRepeat())return true;
        }else return false;
        return model->setData(index,index.data(Qt::CheckStateRole).toInt()==Qt::Checked?Qt::Unchecked:Qt::Checked,Qt::CheckStateRole);
    }
    QWidget* createEditor(QWidget* parent,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        QWidget* editor=QStyledItemDelegate::createEditor(parent,option,index);
        if(auto* line=qobject_cast<QLineEdit*>(editor)){
            // Form-field padding does not fit inside a compact table row.
            line->setFont(option.font);
            line->setTextMargins(0,0,0,0);
            line->setStyleSheet("QLineEdit {padding:0px 4px;margin:0px;border:1px solid #67717e;border-radius:3px;}");
        }
        return editor;
    }
    void updateEditorGeometry(QWidget* editor,const QStyleOptionViewItem& option,const QModelIndex&) const override {
        editor->setGeometry(option.rect.adjusted(1,1,-1,-1));
    }
    void paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        if(index.column()!=0){QStyledItemDelegate::paint(p,option,index);return;}
        p->save();p->fillRect(option.rect,QColor("#3c4652"));paintInvoiceCheck(*p,option.rect,index.data(Qt::CheckStateRole).toInt()==Qt::Checked);p->restore();
    }
};

// Edits belong to this disposable preview until its caller commits them.
class InvoiceReviewDialog : public QDialog {
public:
    using Draft = InvoiceImport::ImportedPartDraft;
    InvoiceReviewDialog(const QList<Draft>& drafts, const InvoiceImport::ExcelReadResult& data,
                        QWidget* owner = nullptr, const QString& fixedOrder = QString(), bool manual = false)
        : QDialog(owner), m_original(drafts), m_data(data), m_fixedOrder(fixedOrder), m_manual(manual) {
        setObjectName("invoiceReviewDialog"); setWindowTitle(QString::fromUtf8("Перевірка накладної"));
        if(manual)setWindowTitle(QString::fromUtf8("Додавання запчастин"));
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint); setModal(true);
        setFont(QFont("Segoe UI", 10));
        setStyleSheet("QDialog {background:#21292f; border:1px solid #707b86; border-radius:12px;}"
            "QWidget {font-family:'Segoe UI'; color:#eef0f3;} QLabel {background:transparent; border:0;}"
            "QWidget#reviewHeader {background:#000000; border-bottom:1px solid #58636f;}"
            "QWidget#reviewBody {background:#21292f;}"
            "QLineEdit,QComboBox {background:#343e49; color:#eef0f3; border:1px solid #67717e; border-radius:7px; padding:7px;}"
            "QComboBox QAbstractItemView {background:#343e49; color:white; selection-background-color:#66717d;}"
            "QTableView {background:#3c4652; alternate-background-color:#363f4a; color:#eef0f3; gridline-color:#505a65; border:1px solid #58636f; selection-background-color:#596572;}"
            "QTableView::item {background:#343e49; color:#eef0f3; border:0;} QTableView::item:selected {background:#596572;}"
            "QHeaderView::section {background:#2e3740; color:#eef0f3; border:1px solid #444e59; padding:7px 3px; font:9pt 'Segoe UI';}"
            "QPushButton {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #41454a,stop:0.45 #141619,stop:0.5 #08090b,stop:1 #000000); color:white; border:1px solid #7b8590; border-radius:7px; padding:8px 14px;}"
            "QPushButton:disabled {color:#8d969f;} QPushButton:hover {background:#25282d;}"
            "QCheckBox {background:transparent;} QCheckBox::indicator,QTableView::indicator {width:17px;height:17px;border:1px solid #d9dfe5;border-radius:3px;background:#303a44;}"
            "QScrollBar {background:#303943;} QScrollBar::handle {background:#727e8c;min-height:18px;}");
        auto* outer = new QVBoxLayout(this); outer->setContentsMargins(1,1,1,1); outer->setSpacing(0);
        auto* header=new QWidget(this);header->setObjectName("reviewHeader");outer->addWidget(header);
        auto* body=new QWidget(this);body->setObjectName("reviewBody");outer->addWidget(body);
        auto* root = new QVBoxLayout(body); root->setContentsMargins(15,12,15,12); root->setSpacing(12);
        auto* heading=new QHBoxLayout(header);heading->setContentsMargins(20,9,20,9);heading->setSpacing(16);
        header->setFixedHeight(qRound((71+14+14)*0.70));
        auto* icon=new InvoiceDocumentIcon(header); icon->setStyleSheet("background:transparent;border:0;");icon->setFixedSize(42,50); heading->addWidget(icon);
        auto* titles=new QVBoxLayout; titles->setSpacing(5); auto* title=new QLabel(windowTitle(),header); title->setStyleSheet("font:600 16pt 'Segoe UI';");
        titles->addWidget(title); titles->addWidget(new QLabel(manual?QString::fromUtf8("Додайте або змініть запчастини замовлення"):QString::fromUtf8("Перевірте дані перед імпортом"),this)); titles->addStretch();heading->addLayout(titles); heading->addStretch();
        auto* x=new QPushButton(QString::fromUtf8("×"),header); x->setFixedWidth(33);x->setStyleSheet("border:0;background:transparent;font:17pt 'Segoe UI';padding:0;"); heading->addWidget(x,0,Qt::AlignTop);
        auto field=[this](const QString& caption,QWidget* editor){auto* column=new QVBoxLayout;column->setSpacing(5);column->addWidget(new QLabel(caption,this));column->addWidget(editor);return column;};
        auto* meta=new QHBoxLayout;
        m_number=new QLineEdit(data.documentNumber,this); m_number->setObjectName("reviewInvoiceNumber"); meta->addLayout(field(QString::fromUtf8("Номер накладної"),m_number),2);
        m_date=new QLineEdit(PartImportTextSupport::normalizeImportedDateDisplay(data.documentDate),this); m_date->setObjectName("reviewInvoiceDate"); m_date->setPlaceholderText(QString::fromUtf8("ДД.ММ.ГГ"));meta->addLayout(field(QString::fromUtf8("Дата накладної"),m_date),1);
        auto* positionCount=new QLabel(QString::fromUtf8("Позицій: %1").arg(drafts.size()),this);positionCount->setObjectName("reviewPositionCount");meta->addWidget(positionCount); root->addLayout(meta);
        auto* bulk=new QHBoxLayout;
        auto* bulkTitle=new QLabel(QString::fromUtf8("Для вибраних позицій"),this);bulkTitle->setStyleSheet("font-weight:600; border-top:1px solid #414b55; padding-top:12px;");root->addWidget(bulkTitle);
        m_store=new QComboBox(this); m_store->setEditable(true);m_store->addItems(PartImportTextSupport::knownStoreNames());m_store->setCurrentIndex(-1);m_store->lineEdit()->setPlaceholderText(QString::fromUtf8("Не змінювати"));m_store->setObjectName("reviewBulkStore");bulk->addLayout(field(QString::fromUtf8("Магазин"),m_store),1);
        m_delivery=new QLineEdit(this);m_delivery->setPlaceholderText(QString::fromUtf8("Не змінювати"));m_delivery->setObjectName("reviewBulkDelivery");bulk->addLayout(field(QString::fromUtf8("Дата доставки"),m_delivery),1);
        m_order=new QLineEdit(this);m_order->setPlaceholderText(QString::fromUtf8("Не змінювати"));m_order->setObjectName("reviewBulkOrder");bulk->addLayout(field(QString::fromUtf8("№ наряду"),m_order),1);
        if(!fixedOrder.isEmpty()){m_order->setText(fixedOrder);m_order->setReadOnly(true);}
        auto* apply=new QPushButton(QString::fromUtf8("Застосувати"),this);apply->setToolTip(QString::fromUtf8("Застосувати до вибраних"));apply->setObjectName("reviewApply");bulk->addWidget(apply,0,Qt::AlignBottom);root->addLayout(bulk);
        m_table=new QTableView(this);m_model=new InvoiceReviewTableModel(m_table);m_table->setModel(m_model);m_table->setObjectName("invoiceReviewTable");m_table->setItemDelegate(new InvoiceSelectionDelegate(m_table));
        const QStringList headers={QString(),QString::fromUtf8("Дата доставки"),QString::fromUtf8("№ наряду"),QString::fromUtf8("Магазин"),QString::fromUtf8("OEM номер"),QString::fromUtf8("Найменування"),QString::fromUtf8("Бренд"),QString::fromUtf8("Кіл-ть"),QString::fromUtf8("Ціна, грн")};
        m_table->setFont(QFont("Segoe UI",9));m_table->verticalHeader()->hide();m_table->verticalHeader()->setDefaultSectionSize(32);m_table->horizontalHeader()->setMinimumSectionSize(27);
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);m_table->horizontalHeader()->setSectionResizeMode(5,QHeaderView::Stretch);
        const int widths[]={30,90,75,115,125,170,110,55,80};for(int c=0;c<9;++c)m_table->setColumnWidth(c,widths[c]);
        QList<QStringList> rows;
        for(const auto& d:drafts)rows.append({QString(),d.deliveryDate.isEmpty()?QDate::currentDate().toString("dd.MM.yy"):d.deliveryDate,fixedOrder.isEmpty()?d.orderNumberText:fixedOrder,d.store,d.oemNumber,d.name,d.brandName,d.qtyText,d.priceText});
        m_model->setRows(headers,rows,!fixedOrder.isEmpty());
        m_all=new InvoiceSelectAllCheck(m_table->horizontalHeader()->viewport());m_all->setFixedSize(22,22);m_all->setObjectName("reviewSelectAll");m_all->setChecked(true);m_all->move(9,10);m_all->raise();
        root->addWidget(m_table,1);
        if(manual){
            auto* tools=new QHBoxLayout;auto* add=new QPushButton(QString::fromUtf8("+ Додати рядок"),this);add->setObjectName("reviewAddPartRow");
            auto* remove=new QPushButton(QString::fromUtf8("Прибрати поточний рядок"),this);remove->setObjectName("reviewRemovePartRow");
            tools->addWidget(add);tools->addWidget(remove);root->addLayout(tools);
            connect(add,&QPushButton::clicked,this,[this]{
                const int row=m_model->rowCount();m_original.append(Draft{});
                QStringList values={QString(),QDate::currentDate().toString("dd.MM.yy"),m_order->text(),m_store->currentText(),QString(),QString(),QString(),"1","0.00"};
                m_model->appendRow(values);
                m_table->setCurrentIndex(m_model->index(row,5));m_table->scrollTo(m_model->index(row,5));m_dirty=true;updateSummary();
            });
            connect(remove,&QPushButton::clicked,this,[this]{int row=m_table->currentIndex().row();if(row<0)return;m_model->removeRows(row,1);m_original.removeAt(row);m_dirty=true;updateSummary();});
        }
        auto* totals=new QHBoxLayout;
        m_documentTotal=new QLineEdit(this);m_documentTotal->setObjectName("reviewDocumentTotal");m_documentTotal->setPlaceholderText(QString::fromUtf8("Не розпізнано"));totals->addLayout(field(QString::fromUtf8("Сума документа, грн"),m_documentTotal));
        for(const auto& row:data.rows){const QString joined=row.join(' ').toLower();if((joined.contains(QString::fromUtf8("всього"))||joined.contains(QString::fromUtf8("разом"))||joined.contains(QString::fromUtf8("итого")))&&!joined.contains(QString::fromUtf8("без пдв"))&&!joined.contains(QString::fromUtf8("без ндс"))){for(int c=row.size()-1;c>=0;--c){bool ok=false;double value=PartImportTextSupport::parseLooseNumber(row[c],&ok);if(ok&&value>=0){m_documentTotal->setText(QString::number(value,'f',2));break;}}}}
        m_difference=new QLabel(this);m_difference->setWordWrap(true);totals->addWidget(m_difference,2);root->addLayout(totals);
        m_verified=new QCheckBox(QString::fromUtf8("Різницю перевірено за оригіналом накладної"),this);m_verified->setObjectName("reviewDifferenceVerified");root->addWidget(m_verified);m_verified->hide();
        connect(m_documentTotal,&QLineEdit::textEdited,this,[this]{m_dirty=true;m_verified->setChecked(false);updateSummary();});
        m_warning=new QLabel(this);m_warning->setWordWrap(true);m_warning->setStyleSheet("color:#ffc98b;");m_warning->hide();root->addWidget(m_warning);
        auto* footer=new QHBoxLayout;m_summary=new QLabel(this);footer->addWidget(m_summary);footer->addStretch();auto* cancel=new QPushButton(QString::fromUtf8("Скасувати"),this);m_import=new QPushButton(this);m_import->setObjectName("reviewImport");footer->addWidget(cancel);footer->addWidget(m_import);root->addLayout(footer);
        for(auto* b:{x,apply,cancel,m_import})b->setAutoDefault(false);
        connect(x,&QPushButton::clicked,this,&QDialog::reject);connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
        connect(m_import,&QPushButton::clicked,this,[this]{if(m_saving||!validate())return;m_saving=true;m_import->setEnabled(false);QString error;
            if(commit&&!commit(&error)){m_saving=false;m_warning->setText(error);m_warning->show();m_import->setEnabled(true);return;}m_dirty=false;accept();});
        connect(m_all,&QCheckBox::clicked,this,[this](bool checked){m_model->setAllChecked(checked);});
        connect(apply,&QPushButton::clicked,this,[this]{
            if(!m_store->currentText().trimmed().isEmpty())m_model->setColumnForSelected(3,m_store->currentText().trimmed());
            if(!m_delivery->text().trimmed().isEmpty())m_model->setColumnForSelected(1,m_delivery->text().trimmed());
            if(!m_order->text().trimmed().isEmpty())m_model->setColumnForSelected(2,m_order->text().trimmed());
        });
        connect(m_model,&QAbstractItemModel::dataChanged,this,[this](const QModelIndex& index,const QModelIndex&,const QList<int>& roles){
            if(!roles.contains(Qt::EditRole) && !roles.contains(Qt::CheckStateRole))return;
            if(index.column()!=0){m_dirty=true;m_verified->setChecked(false);}updateSummary();
        });
        for(auto* e:{m_number,m_date})connect(e,&QLineEdit::textEdited,this,[this]{m_dirty=true;});
        if(!manual&&invoiceNumber().isEmpty())m_number->setStyleSheet("border:2px solid #df957c;");
        if(invoiceDate().isEmpty())m_date->setStyleSheet("border:2px solid #df957c;");
        updateSummary();
        const QRect screenRect=screen()->availableGeometry(); m_table->setFixedHeight(50+32*qMax(1,qMin(8,drafts.size())));
        setFixedWidth(qRound(qMin(1280,screenRect.width()-40)*0.75));
        resize(width(),sizeHint().height());
        const QPoint target=owner?owner->mapToGlobal(owner->rect().center()):screenRect.center();
        move(qBound(screenRect.left(),target.x()-width()/2,qMax(screenRect.left(),screenRect.right()-width()+1)),
             qBound(screenRect.top(),target.y()-height()/2,qMax(screenRect.top(),screenRect.bottom()-height()+1)));
        header->installEventFilter(this);
        for(auto* child:header->findChildren<QWidget*>())if(!qobject_cast<QPushButton*>(child))child->installEventFilter(this);
        m_geometryReady=true;
    }
    std::function<bool(QString*)> commit;
    void useWhiteInputs(){setStyleSheet(styleSheet()+QStringLiteral(" QLineEdit,QComboBox,QComboBox QAbstractItemView,QTableView,QTableView::item {background:white;color:black;} QTableView::item:selected {background:#dbe8fa;color:black;}"));}
    QList<Draft> selectedDrafts() const {QList<Draft> out;for(int r=0;r<m_model->rowCount();++r)if(selected(r)){auto d=m_original[r];d.orderedAt=invoiceDate();d.deliveryDate=text(r,1);d.orderNumberText=text(r,2);d.store=text(r,3);d.oemNumber=text(r,4);d.name=text(r,5);d.brandName=text(r,6);d.qtyText=text(r,7);d.priceText=text(r,8);out.append(d);}return out;}
    QList<int> selectedRows() const {QList<int> rows;for(int r=0;r<m_model->rowCount();++r)if(selected(r))rows.append(r);return rows;}
    QString text(int row,int col) const {return m_model->index(row,col).data().toString().trimmed();}
    QString invoiceNumber() const {return m_number->text().trimmed();}
    QString invoiceDate() const {return DateFormats::parseDate(m_date->text()).toString(Qt::ISODate);}
    void reject() override {if(m_saving)return;if(m_dirty&&QMessageBox::question(this,windowTitle(),QString::fromUtf8("Відкинути внесені зміни?"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;QDialog::reject();}
protected:
    void showEvent(QShowEvent* event) override {
        QDialog::showEvent(event);fitToWorkArea(true);
    }
    bool event(QEvent* event) override {
        const bool result=QDialog::event(event);
        if(event->type()==QEvent::LayoutRequest&&m_geometryReady&&!m_fitPending){
            m_fitPending=true;QTimer::singleShot(0,this,[this]{m_fitPending=false;if(isVisible())fitToWorkArea(false);});
        }
        return result;
    }
    bool eventFilter(QObject* watched,QEvent* event) override {
        if(event->type()==QEvent::MouseButtonPress){
            auto* mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()==Qt::LeftButton){m_dragging=true;m_dragOffset=mouse->globalPosition().toPoint()-pos();return true;}
        }else if(event->type()==QEvent::MouseMove&&m_dragging){
            move(static_cast<QMouseEvent*>(event)->globalPosition().toPoint()-m_dragOffset);return true;
        }else if(event->type()==QEvent::MouseButtonRelease&&m_dragging){m_dragging=false;return true;}
        return QDialog::eventFilter(watched,event);
    }
    void fitToWorkArea(bool initial) {
        const QRect area=screen()->availableGeometry();
        const int gap=qRound(screen()->logicalDotsPerInchY()*5.0/25.4);
        const int available=area.height()-gap;
        // Reserve space for the actual header, footer and visible validation messages.
        layout()->activate();
        const int overhead=layout()->sizeHint().height()-m_table->height();
        const int visibleRows = qBound(1, m_model->rowCount(), 8);
        const auto* horizontalBar = m_table->horizontalScrollBar();
        const int scrollHeight = horizontalBar->maximum() > 0 ? horizontalBar->sizeHint().height() : 0;
        const int desired = m_table->horizontalHeader()->sizeHint().height() + 2 * m_table->frameWidth()
            + scrollHeight + visibleRows * m_table->verticalHeader()->defaultSectionSize();
        const int tableHeight=qMax(50,qMin(desired,available-overhead));
        if(m_table->height()!=tableHeight)m_table->setFixedHeight(tableHeight);
        layout()->activate();resize(width(),layout()->sizeHint().height());
        const int bottomY=area.bottom()+1-gap-height();
        move(qBound(area.left(),x(),qMax(area.left(),area.right()+1-width())),
             initial?bottomY:qBound(area.top(),y(),qMax(area.top(),bottomY)));
    }
    bool selected(int row) const {return m_model->index(row,0).data(Qt::CheckStateRole).toInt()==Qt::Checked;}
    qint64 cents(int row) const {const double amount=PartImportTextSupport::parseLooseNumber(text(row,7),nullptr)*PartImportTextSupport::parseLooseNumber(text(row,8),nullptr)*100.0;return std::isfinite(amount)&&std::abs(amount)<9e18?qRound64(amount):0;}
    void updateSummary(){qint64 total=0;int count=0;for(int r=0;r<m_model->rowCount();++r)if(selected(r)){++count;total+=cents(r);}m_summary->setText(QString::fromUtf8("Вибрано: %1     |     Разом: %2 грн").arg(count).arg(QLocale(QLocale::Ukrainian).toString(total/100.0,'f',2)));const QString ending=(count%10==1&&count%100!=11)?QString::fromUtf8("позицію"):((count%10>=2&&count%10<=4&&(count%100<12||count%100>14))?QString::fromUtf8("позиції"):QString::fromUtf8("позицій"));m_import->setText(QString::fromUtf8("Імпортувати %1 %2").arg(count).arg(ending));m_import->setEnabled(count>0&&!m_saving);QSignalBlocker b(m_all);m_all->setChecked(count==m_model->rowCount());
        if(m_manual)m_import->setText(QString::fromUtf8("Зберегти позиції (%1)").arg(count));
        if(auto* label=findChild<QLabel*>("reviewPositionCount"))label->setText(QString::fromUtf8("Позицій: %1").arg(m_model->rowCount()));
        qint64 all=0;for(int r=0;r<m_model->rowCount();++r)all+=cents(r);bool ok=false;double doc=PartImportTextSupport::parseLooseNumber(m_documentTotal->text(),&ok);
        const qint64 difference=ok?qRound64(doc*100)-all:0;m_mismatch=ok&&difference!=0;m_verified->setVisible(m_mismatch);
        m_difference->setText(QString::fromUtf8("Усі позиції: %1 грн").arg(QLocale(QLocale::Ukrainian).toString(all/100.0,'f',2))+(m_mismatch?QString::fromUtf8(" · Різниця: %1 грн").arg(QLocale(QLocale::Ukrainian).toString(difference/100.0,'f',2)):QString()));}
    bool validate(){bool valid=true;QStringList errors;
        const bool numberOk=m_manual||!invoiceNumber().isEmpty(),dateOk=!invoiceDate().isEmpty();
        m_number->setStyleSheet(numberOk?QString():"border:2px solid #df957c;");m_date->setStyleSheet(dateOk?QString():"border:2px solid #df957c;");
        if(!numberOk||!dateOk){valid=false;errors<<QString::fromUtf8("Укажіть номер і правильну дату накладної.");}
        for(int r=0;r<m_model->rowCount();++r){for(int c=1;c<9;++c){m_model->setData(m_model->index(r,c),QBrush(),Qt::BackgroundRole);m_model->setData(m_model->index(r,c),QString(),Qt::ToolTipRole);}if(!selected(r))continue;
            auto bad=[&](int col,const QString& reason){m_model->setData(m_model->index(r,col),QColor("#754b43"),Qt::BackgroundRole);m_model->setData(m_model->index(r,col),reason,Qt::ToolTipRole);valid=false;};
            if(!DateFormats::parseDate(text(r,1)).isValid())bad(1,QString::fromUtf8("Укажіть правильну дату доставки."));
            if(!text(r,2).isEmpty()){bool ok=false;int number=text(r,2).toInt(&ok);if(!ok||number<=0)bad(2,QString::fromUtf8("Номер наряду має бути додатним цілим числом."));}
            for(int c:{3,5})if(text(r,c).isEmpty())bad(c,QString::fromUtf8("Обов’язкове поле."));
            for(int c:{7,8}){bool ok=false;double value=PartImportTextSupport::parseLooseNumber(text(r,c),&ok);if(!ok||!std::isfinite(value)||value<0||(c==7&&value==0)||value>100000000)bad(c,QString::fromUtf8("Перевірте кількість або ціну."));}}
        if(m_mismatch&&!m_verified->isChecked()){valid=false;errors<<QString::fromUtf8("Звірте суму документа та всі позиції й підтвердьте перевірку різниці.");}
        if(!m_documentTotal->text().trimmed().isEmpty()){bool ok=false;double value=PartImportTextSupport::parseLooseNumber(m_documentTotal->text(),&ok);if(!ok||!std::isfinite(value)||value<0){valid=false;errors<<QString::fromUtf8("Неправильна сума документа.");}}
        if(!valid)errors<<QString::fromUtf8("Виправте позначені поля. Пояснення — у підказці клітинки.");m_warning->setText(errors.join(' '));m_warning->setVisible(!valid);return valid;}
    QList<Draft> m_original;InvoiceImport::ExcelReadResult m_data;
private:
    bool m_geometryReady=false,m_fitPending=false,m_dragging=false;
    QPoint m_dragOffset;
    QLineEdit* m_documentTotal;QLabel* m_difference;QCheckBox* m_verified;bool m_mismatch=false;
    QString m_fixedOrder;QTableView* m_table;InvoiceReviewTableModel* m_model;QLineEdit *m_number,*m_date,*m_delivery,*m_order;QComboBox* m_store;QCheckBox* m_all;QLabel *m_summary,*m_warning;QPushButton* m_import;bool m_dirty=false,m_saving=false,m_manual=false;
};
