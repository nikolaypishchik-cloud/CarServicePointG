#pragma once

#include <QWidget>

class UnpaidBannerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit UnpaidBannerWidget(QWidget* parent=nullptr);

    void setDebt(bool hasDebt, const QString& qtyText, const QString& sumText);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    bool m_hasDebt = false;
    QString m_qty;
    QString m_sum;

};
