#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QString>
#include <functional>

class QTimer;

class PageActivationCoordinator final : public QObject
{
    Q_OBJECT
public:
    explicit PageActivationCoordinator(QObject* parent = nullptr);

    void requestActivation(int pageIndex,
                           const QString& pageName,
                           std::function<void()> callback,
                           int delayMs = 0);

private:
    QTimer* m_timer = nullptr;
    int m_pendingPageIndex = -1;
    int m_generation = 0;
    QString m_pendingPageName;
    std::function<void()> m_pendingCallback;
    QElapsedTimer m_pageTimer;
};
