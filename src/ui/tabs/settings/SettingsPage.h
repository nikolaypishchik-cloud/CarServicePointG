#pragma once

#include <QWidget>

#include <atomic>
#include <memory>
#include <QList>
#include <QMap>

class QLabel;
class QListWidget;
class QStackedWidget;
class QButtonGroup;
class QVBoxLayout;
class QTimer;
class QAbstractButton;
class QPushButton;

class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent=nullptr);
    void refreshThemeStyles();
    Q_INVOKABLE void setPageFrozen(bool frozen);
    Q_INVOKABLE void openDirectoriesSettings();

signals:
    void requestRefreshAll();

private:
    enum class Category {
        General,
        Ui,
        Personnel,
        Integrations,
        Finance,
        Reports,
        Outsource,
        Security,
        Synchronization,
        ExternalDisplay
    };

    struct Item {
        QString id;
        QString title;
    };

    void buildUi();
    void buildCategoryButtons(QWidget* parent);
    void buildRightPane(QWidget* parent);
    QWidget* ensureCategoryItemsPage(Category c);

    void setCategory(Category c);
    void rebuildItems();
    void showItem(const QString& id);
    void returnToItemsMenu();

    QWidget* buildDetailWidget(const QString& id);
    QWidget* buildPersonnelManageSection();
    QWidget* buildIntegrationSyncSection();
    void executeLegacyImport(const QString& picked, QWidget* sectionGuard);
    QWidget* buildPlaceholderWidget(const QString& text);

    // Helpers
    static QString categoryTitle(Category c);
    void queueRefreshAll(int delayMs = 30);

private:
    Category m_category = Category::General;

    QButtonGroup* m_catGroup = nullptr;
    QLabel* m_hdrTitle = nullptr;
    QWidget* m_leftPane = nullptr;
    QWidget* m_rightPane = nullptr;
    QListWidget* m_items = nullptr;
    QStackedWidget* m_detail = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    QWidget* m_itemsPage = nullptr;
    QWidget* m_detailPage = nullptr;
    QStackedWidget* m_itemsCategoryStack = nullptr;
    QButtonGroup* m_itemGroup = nullptr;
    QPushButton* m_sendReportButton = nullptr;
    QLabel* m_footer = nullptr;
    QTimer* m_refreshAllDebounce = nullptr;

    QMap<QString, int> m_detailIndexById;
    QMap<Category, QList<Item>> m_itemsByCategory;
    QMap<Category, QWidget*> m_categoryItemsPages;
    QMap<Category, QButtonGroup*> m_categoryButtonGroups;
    QMap<QString, QAbstractButton*> m_itemButtonById;
    QString m_lastThemeRefreshSignature;
    QString m_cachedLeftQss;
    QString m_cachedRightQss;
    QString m_cachedItemsQss;
    std::shared_ptr<std::atomic_bool> m_legacyImportCancelToken;
    bool m_pageFrozen = false;
    bool m_themeRefreshPending = false;
};
