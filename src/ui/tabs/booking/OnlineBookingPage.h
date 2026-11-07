#pragma once

#include <QWidget>
#include <QString>

class QTableWidget;

class OnlineBookingPage : public QWidget
{
    Q_OBJECT

public:
    explicit OnlineBookingPage(QWidget* parent = nullptr);

    void refresh();

signals:
    void requestRefreshAll();

private:
    void buildUi();
    void updateSelectedStatus(const QString& status);
    void rescheduleSelected();
    void createOrderFromSelected();
    void messageSelectedClient();
    QString selectedAppointmentUid() const;

    QTableWidget* m_table = nullptr;
};
