#pragma once
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QStringList>
#include <functional>

class QWidget;
class QScreen;
class ExternalDisplayController : public QObject {
public:
    struct View { int id; QString title; };
    struct Page { QWidget* widget = nullptr; std::function<void()> refresh; };
    using Factory = std::function<Page(int, QWidget*)>;
    ExternalDisplayController(QWidget* owner, Factory factory, std::function<bool(int)> allowed);
    QWidget* settingsWidget(QWidget* parent);
    bool display(QScreen* screen, const QList<int>& ids, int layout, QString* error = nullptr);
    void stop(QScreen* screen);
    void stopAll();
    void refresh();
    static QString screenKey(QScreen* screen);
    static QList<View> views();
private:
    bool openDisplay(QScreen* screen, const QList<int>& ids, int layout, QString* error, bool editor);
    bool eventFilter(QObject* watched, QEvent* event) override;
    QWidget* m_owner;
    Factory m_factory;
    std::function<bool(int)> m_allowed;
    QHash<QString, QPointer<QWidget>> m_windows;
    QHash<QString, QList<Page>> m_pages;
};
