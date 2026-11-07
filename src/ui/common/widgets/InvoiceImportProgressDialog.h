#pragma once

#include "core/import/ImportJob.h"
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>

// The uncompleted portion is deliberately continuous, not a row of empty cells.
class InvoiceSegmentProgress final : public QWidget {
public:
    explicit InvoiceSegmentProgress(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName("invoiceSegmentProgress"); setFixedHeight(10);
    }
    void setValue(int value) { m_value = qBound(0, value, 100); update(); }
    int value() const { return m_value; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const int side = height(), gap = 3, count = qMax(1, (width() + gap) / (side + gap));
        const int filled = m_value * count / 100;
        const int rest = filled * (side + gap);
        if (filled < count) p.fillRect(QRect(rest, 0, width() - rest, side), QColor("#536172"));
        for (int i = 0; i < filled; ++i) {
            QLinearGradient g(0, 0, 0, side);
            g.setColorAt(0, QColor("#9ce6f2")); g.setColorAt(1, QColor("#55bed4"));
            p.fillRect(QRect(i * (side + gap), 0, side, side), g);
        }
    }
private:
    int m_value = 0;
};

class InvoiceDocumentIcon final : public QWidget {
public:
    explicit InvoiceDocumentIcon(QWidget* parent) : QWidget(parent) { setFixedSize(47, 56); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.scale(width()/78.0, height()/94.0);
        p.setPen(QPen(QColor("#e3e8ee"), 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath outline; outline.moveTo(49, 7); outline.lineTo(15, 7);
        outline.quadTo(8, 7, 8, 15); outline.lineTo(8, 79); outline.quadTo(8, 87, 16, 87);
        outline.lineTo(63, 87); outline.quadTo(70, 87, 70, 79); outline.lineTo(70, 29);
        outline.lineTo(49, 7); outline.lineTo(49, 29); outline.lineTo(70, 29); p.drawPath(outline);
        p.drawLine(23, 44, 54, 44); p.drawLine(23, 57, 54, 57); p.drawLine(23, 70, 43, 70);
    }
};

// One lifecycle for every invoice reader. Failed attempts stay here for retry;
// callers only receive success or cancellation, and never an abandoned result.
class InvoiceImportProgressDialog final : public QDialog {
public:
    using Result = InvoiceImport::ImportJobResult;
    InvoiceImportProgressDialog(QWidget* owner, InvoiceImport::ImportJob* job,
                                std::function<void(const Result&)> completed)
        : QDialog(owner), m_job(job), m_completed(std::move(completed)) {
        setObjectName("invoiceImportProgressDialog");
        setWindowTitle(QString::fromUtf8("Зчитування накладної"));
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setWindowModality(Qt::WindowModal); setAttribute(Qt::WA_TranslucentBackground);
        setFont(QFont("Segoe UI", 10));
        setStyleSheet("QLabel {color:#dce2e8; background:transparent; border:0; font-family:'Segoe UI';}"
                      "QPushButton {color:#eef1f5; background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #343e4b,stop:1 #252d37); border:1px solid #66717e; border-radius:6px; padding:6px 13px; font:600 10pt 'Segoe UI';}"
                      "QPushButton:hover {background:#414c59;} QPushButton:focus {border:2px solid #b5bec8;}"
                      "QPushButton#invoiceReadClose {border:0; background:transparent; padding:0; font:12pt 'Segoe UI';}");
        auto* layout = new QVBoxLayout(this); layout->setContentsMargins(18, 10, 18, 14); layout->setSpacing(12);
        auto* top = new QHBoxLayout;
        top->addStretch(); auto* close = new QPushButton(QString::fromUtf8("×"), this);
        close->setObjectName("invoiceReadClose"); close->setFixedSize(20, 20);
        close->setAccessibleName(QString::fromUtf8("Скасувати зчитування")); top->addWidget(close); layout->addLayout(top);
        auto* content = new QHBoxLayout; content->setSpacing(17); content->addWidget(new InvoiceDocumentIcon(this));
        auto* text = new QVBoxLayout; text->setSpacing(4);
        auto* title = new QLabel(windowTitle(), this); title->setStyleSheet("font:600 12pt 'Segoe UI'; color:#f2f4f6;"); title->setWordWrap(true);
        m_file = new QLabel(this); m_file->setWordWrap(true); m_file->setTextFormat(Qt::PlainText);
        m_status = new QLabel(this); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText);
        text->addWidget(title); text->addWidget(m_file); text->addWidget(m_status); content->addLayout(text, 1); layout->addLayout(content);
        m_bar = new InvoiceSegmentProgress(this); layout->addWidget(m_bar);
        auto* buttons = new QHBoxLayout; buttons->addStretch();
        m_retry = new QPushButton(QString::fromUtf8("Повторити"), this); m_retry->setObjectName("invoiceReadRetry");
        m_cancel = new QPushButton(QString::fromUtf8("Скасувати"), this); m_cancel->setObjectName("invoiceReadCancel");
        buttons->addWidget(m_retry); buttons->addWidget(m_cancel); layout->addLayout(buttons); m_retry->hide();
        for (auto* b : {close, m_retry, m_cancel}) b->setAutoDefault(false);
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_retry, &QPushButton::clicked, this, [this] { start(m_request); });
        connect(job, &InvoiceImport::ImportJob::progressChanged, this, [this](const auto& progress) {
            if (!m_active || progress.cancelRequested || progress.percent >= 100) return;
            m_bar->setValue(qMax(m_bar->value(), progress.percent));
            if (!progress.message.isEmpty()) m_status->setText(progress.message);
        });
        connect(job, &InvoiceImport::ImportJob::finished, this, [this](const Result& result) {
            if (!m_active) return;
            if (result.ok()) {
                m_bar->setValue(100);
                m_status->setText(QString::fromUtf8("Накладну зчитано. Дані готові до перевірки."));
                m_bar->repaint();
                const auto attempt = m_attempt;
                QTimer::singleShot(qMax(0, 2500 - int(m_visibleTime.elapsed())), this, [this, attempt, result] {
                    if (!m_active || attempt != m_attempt) return;
                    m_active = false; hide(); m_completed(result);
                });
                return;
            }
            if (result.status == InvoiceImport::ImportJobStatus::Cancelled) { reject(); return; }
            m_status->setText(result.error.message.isEmpty() ? QString::fromUtf8("Не вдалося зчитати накладну. Перевірте файл і повторіть спробу.") : result.error.message);
            m_retry->show(); m_cancel->setText(QString::fromUtf8("Закрити")); adjustSize(); center();
        });
    }
    ~InvoiceImportProgressDialog() override { if (m_job && m_active) { disconnect(m_job, nullptr, this, nullptr); m_job->cancel(); } }
    void start(const InvoiceImport::ImportJobRequest& request) {
        if (!m_job || m_job->isRunning()) return;
        m_request = request; m_active = true; ++m_attempt; m_visibleTime.restart(); m_bar->setValue(0);
        m_file->setText(QFileInfo(request.filePath).fileName()); m_file->setToolTip(request.filePath);
        m_status->setText(QString::fromUtf8("Читаємо та розпізнаємо накладну…"));
        m_retry->hide(); m_cancel->setText(QString::fromUtf8("Скасувати"));
        const int available = screen()->availableGeometry().width() - 40;
        setFixedWidth(qMin(456, available)); adjustSize(); center(); show();
        m_job->start(request);
    }
    void reject() override {
        if (!m_active) { hide(); return; }
        m_active = false; hide();
        if (m_job) m_job->cancel();
        Result result; result.status = InvoiceImport::ImportJobStatus::Cancelled;
        result.filePath = m_request.filePath; result.error.code = InvoiceImport::ImportJobErrorCode::Cancelled;
        m_completed(result);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#75818e"), 1)); p.setBrush(QColor("#21292f"));
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 15, 15);
    }
private:
    void center() {
        const QRect available = screen()->availableGeometry();
        const QPoint target = parentWidget() ? parentWidget()->mapToGlobal(parentWidget()->rect().center()) : available.center();
        move(qBound(available.left(), target.x() - width()/2, qMax(available.left(), available.right()-width()+1)),
             qBound(available.top(), target.y()-height()/2, qMax(available.top(), available.bottom()-height()+1)));
    }
    QPointer<InvoiceImport::ImportJob> m_job;
    std::function<void(const Result&)> m_completed;
    InvoiceImport::ImportJobRequest m_request;
    bool m_active = false;
    quint64 m_attempt = 0;
    QElapsedTimer m_visibleTime;
    QLabel *m_file, *m_status;
    InvoiceSegmentProgress* m_bar;
    QPushButton *m_retry, *m_cancel;
};
