#include "ui/tabs/schedule/widgets/PlanningDayCell.h"

#include "ui/tabs/schedule/PlanningPageSupport.h"

#include <QAbstractButton>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>

using namespace PlanningPageSupport;

namespace {
class BellToggleButton final : public QAbstractButton
{
public:
    explicit BellToggleButton(QWidget* parent=nullptr)
        : QAbstractButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setMouseTracking(true);
        setMinimumSize(22, 22);
        setMaximumSize(30, 30);
        setAccessibleName(QString::fromUtf8("Нагадування дня"));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect().adjusted(2.0, 2.0, -2.0, -2.0);
        const bool active = isChecked();
        QColor bell = active ? QColor(245, 194, 45) : QColor(132, 139, 148);
        QColor outline = active ? QColor(132, 91, 0) : QColor(84, 91, 99);
        if (underMouse()) {
            bell = active ? QColor(255, 212, 58) : QColor(162, 169, 178);
        }

        QPainterPath body;
        body.moveTo(r.center().x(), r.top() + r.height() * 0.20);
        body.cubicTo(r.left() + r.width() * 0.28, r.top() + r.height() * 0.22,
                     r.left() + r.width() * 0.25, r.top() + r.height() * 0.45,
                     r.left() + r.width() * 0.25, r.top() + r.height() * 0.64);
        body.lineTo(r.left() + r.width() * 0.17, r.top() + r.height() * 0.78);
        body.lineTo(r.right() - r.width() * 0.17, r.top() + r.height() * 0.78);
        body.lineTo(r.right() - r.width() * 0.25, r.top() + r.height() * 0.64);
        body.cubicTo(r.right() - r.width() * 0.25, r.top() + r.height() * 0.45,
                     r.right() - r.width() * 0.28, r.top() + r.height() * 0.22,
                     r.center().x(), r.top() + r.height() * 0.20);
        body.closeSubpath();

        p.setPen(QPen(outline, 1.25));
        p.setBrush(bell);
        p.drawPath(body);

        p.setPen(QPen(outline, 1.15));
        p.setBrush(bell.lighter(active ? 116 : 108));
        const QRectF cap(r.center().x() - r.width() * 0.11, r.top() + r.height() * 0.10,
                         r.width() * 0.22, r.height() * 0.16);
        p.drawEllipse(cap);

        p.setBrush(active ? QColor(255, 230, 112) : QColor(181, 187, 194));
        p.setPen(QPen(outline, 1.0));
        const QRectF clapper(r.center().x() - r.width() * 0.085, r.top() + r.height() * 0.76,
                             r.width() * 0.17, r.height() * 0.14);
        p.drawEllipse(clapper);

        if (active) {
            p.setPen(QPen(QColor(255, 235, 120, 170), 1.3));
            p.drawArc(QRectF(r.left() + r.width() * 0.05, r.top() + r.height() * 0.23,
                             r.width() * 0.28, r.height() * 0.38), 92 * 16, 76 * 16);
            p.drawArc(QRectF(r.right() - r.width() * 0.33, r.top() + r.height() * 0.23,
                             r.width() * 0.28, r.height() * 0.38), 12 * 16, 76 * 16);
        }
    }
};
} // namespace

PlanningDayCell::PlanningDayCell(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("planning_daycell");
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    setStyleSheet(R"QSS(
        #planning_daycell {
            background: #ffffff;
            border: 1px solid #7f858c;
            border-radius: 8px;
        }
        #planning_daycell[current="true"] {
            border: 2px solid #2e9d45;
        }
        #planning_daycell[selected="true"] {
            border: 2px solid #2e7d32;
        }
        #planning_daycell_header {
            background: #f2f2f2;
            color: #111111;
            font-weight: 700;
            padding-left: 0px;
            border-top-left-radius: 8px;
            border-top-right-radius: 8px;
            border-bottom: 1px solid #c9ced4;
        }
        #planning_daycell_header[current="true"] {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #dfffe7,stop:0.2 #b4ecc2,stop:1 #70cb89);
        }
        #planning_daycell_body {
            background: transparent;
            color: #111111;
            font-size: 11pt;
        }
    )QSS");
    setFrameShape(QFrame::NoFrame);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);

    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0,0,0,0);
    l->setSpacing(0);

    m_header = new QLabel(this);
    m_header->setObjectName("planning_daycell_header");
    m_header->setWordWrap(true);
    m_header->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_header->setContentsMargins(8,4,42,4);
    m_header->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    l->addWidget(m_header);

    m_body = new QLabel(this);
    m_body->setObjectName("planning_daycell_body");
    m_body->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_body->setContentsMargins(8,6,8,6);
    m_body->setWordWrap(true);
    m_body->setTextFormat(Qt::PlainText);
    m_body->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    l->addWidget(m_body, 1);

    m_bell = new BellToggleButton(this);
    connect(m_bell, &QAbstractButton::clicked, this, [this](bool checked) {
        setReminderActive(checked);
        emit reminderBellToggled(m_date, checked);
    });
    refreshBell();

    setMinimumSize(176, 150);
}

void PlanningDayCell::setDate(const QDate& date)
{
    if (m_date == date) return;
    m_date = date;
    refreshText();
}

QDate PlanningDayCell::date() const
{
    return m_date;
}

void PlanningDayCell::setSelected(bool on)
{
    if (m_selected == on) return;
    m_selected = on;
    setProperty("selected", on);
    repolish();
}

void PlanningDayCell::setCurrentDate(bool on)
{
    if (m_current == on) return;
    m_current = on;
    setProperty("current", on);
    m_header->setProperty("current", on);
    setMinimumSize(on ? QSize(210, 185) : QSize(176, 150));
    if (on) {
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(10); shadow->setOffset(0, 3); shadow->setColor(QColor(0, 45, 15, 150));
        setGraphicsEffect(shadow);
        raise();
    } else setGraphicsEffect(nullptr);
    updateGeometry();
    repolish();
}

void PlanningDayCell::setPreviewText(const QString& text)
{
    if (m_body->text() == text) return;
    m_body->setText(text);
}

void PlanningDayCell::setReminderActive(bool on)
{
    if (m_reminderActive == on && (!m_bell || m_bell->isChecked() == on)) return;
    m_reminderActive = on;
    if (m_bell) {
        QSignalBlocker blocker(m_bell);
        m_bell->setChecked(on);
    }
    refreshBell();
}

bool PlanningDayCell::reminderActive() const
{
    return m_reminderActive;
}

void PlanningDayCell::resetVisualState()
{
    setSelected(false);
    setCurrentDate(false);
    setPreviewText(QString());
    setReminderActive(false);
}

void PlanningDayCell::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    if (!m_bell) return;
    const int s = qBound(22, qMin(width(), 30), 30);
    m_bell->setGeometry(width() - s - 10, 3, s, s);
    m_bell->raise();
}

void PlanningDayCell::refreshText()
{
    const QString title = QString("%1.%2.%3 - %4")
        .arg(m_date.day(), 2, 10, QChar('0'))
        .arg(m_date.month(), 2, 10, QChar('0'))
        .arg(m_date.year()%100, 2, 10, QChar('0'))
        .arg(uaWeekday(m_date.dayOfWeek()));
    m_header->setText(title);
}

void PlanningDayCell::refreshBell()
{
    if (!m_bell) return;
    m_bell->setToolTip(m_reminderActive
        ? QString::fromUtf8("Нагадування на цей день активне")
        : QString::fromUtf8("Нагадування на цей день вимкнене"));
    m_bell->update();
}

void PlanningDayCell::repolish()
{
    style()->unpolish(this);
    style()->polish(this);
    style()->unpolish(m_header);
    style()->polish(m_header);
    update();
}
