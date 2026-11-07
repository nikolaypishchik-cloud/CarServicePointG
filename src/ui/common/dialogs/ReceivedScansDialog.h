#pragma once

#include "core/sync/SyncPayloadUtils.h"

#include <QDialog>

class QAbstractTableModel;
class QTableView;
class QLabel;
class QPushButton;

class ReceivedScansDialog : public QDialog
{
    Q_OBJECT
public:
    struct ImportRow {
        int orderNumber = 0;
        WorkOrderPartRow row;
    };

    explicit ReceivedScansDialog(const SyncPayloadUtils::SyncPayload& payload,
                                 int defaultOrderNumber = 0,
                                 QWidget* parent = nullptr);

    QList<ImportRow> importRows() const;

private slots:
    void handleImport();

private:
    void populateRows(int defaultOrderNumber);
    static QString displayDateText(const QString& isoDate);
    static QString normalizeDateText(const QString& text);
    static QString numberText(double value);
    static double parseLooseDouble(const QString& text, bool* ok = nullptr);

private:
    SyncPayloadUtils::SyncPayload m_payload;
    QList<WorkOrderPartRow> m_sourceRows;
    QTableView* m_table = nullptr;
    QAbstractTableModel* m_tableModel = nullptr;
    QLabel* m_hint = nullptr;
};
