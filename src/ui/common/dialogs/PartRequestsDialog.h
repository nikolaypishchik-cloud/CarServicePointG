#pragma once

#include <QDialog>
#include <QJsonArray>

class QLabel;
class QPushButton;
class QTableWidget;

class PartRequestsDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit PartRequestsDialog(QWidget* parent = nullptr);
    void setInitialRequest(const QString& requestUid);

signals:
    void requestOrdered();

private:
    void refreshRequests();
    void renderRequests(const QJsonArray& items, bool canDecide);
    void orderRequest(const QString& requestUid, const QString& partName);

    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_refreshButton = nullptr;
    bool m_loading = false;
    QString m_initialRequestUid;
};