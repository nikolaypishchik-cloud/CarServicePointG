#pragma once

#include <QDialog>
#include <QList>
#include <QString>

struct ServiceMasterShare {
    QString masterId;
    QString masterName;
    double sharePercent = 0.0;
};

class QTableWidget;
class QPushButton;

class ServiceMastersDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ServiceMastersDialog(QWidget* parent=nullptr);

    void setMasters(const QList<ServiceMasterShare>& shares);
    QList<ServiceMasterShare> masters() const;

private slots:
    void distributeEqual();

private:
    void buildUi();

    QTableWidget* m_table = nullptr;
    QPushButton* m_btnEqual = nullptr;

    QList<ServiceMasterShare> m_shares;
};
