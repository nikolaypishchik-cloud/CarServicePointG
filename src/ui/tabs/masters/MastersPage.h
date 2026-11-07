#pragma once

#include <QWidget>
#include <QTimer>

class QLineEdit;
class QToolButton;
class QTableWidget;
class QCheckBox;

class MastersPage : public QWidget
{
    Q_OBJECT
public:
    explicit MastersPage(QWidget* parent=nullptr);

    void refresh();

signals:
    void requestRefreshAll();

private:
    QString selectedMasterId() const;
    void queueRefreshAll(int delayMs = 30);

    void addMaster();
    void editMaster();
    void toggleActive();
    void deleteMaster();

    QLineEdit* m_search = nullptr;
    QTimer* m_searchDebounce = nullptr;
    QCheckBox* m_showArchived = nullptr;
    QTableWidget* m_table = nullptr;

    bool m_loading = false;
    QTimer* m_refreshAllDebounce = nullptr;
};
