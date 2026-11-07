#pragma once

#include <QObject>
#include <QPointer>
#include <QtGlobal>

#include <atomic>
#include <memory>

class QObject;

class AsyncTaskHandle
{
public:
    AsyncTaskHandle() = default;
    AsyncTaskHandle(QObject* owner,
                    quint64 generation = 0,
                    std::shared_ptr<std::atomic_bool> cancelToken = {})
        : m_owner(owner)
        , m_generation(generation)
        , m_cancelToken(std::move(cancelToken))
    {
    }

    [[nodiscard]] bool ownerAlive() const { return !m_owner.isNull(); }
    [[nodiscard]] bool isCancelled() const { return m_cancelToken && m_cancelToken->load(); }
    [[nodiscard]] bool isLatest(quint64 latestGeneration) const { return m_generation == latestGeneration; }
    [[nodiscard]] bool shouldApply() const { return ownerAlive() && !isCancelled(); }
    [[nodiscard]] bool shouldApply(quint64 latestGeneration) const {
        return ownerAlive() && !isCancelled() && isLatest(latestGeneration);
    }

    [[nodiscard]] quint64 generation() const { return m_generation; }
    [[nodiscard]] QObject* owner() const { return m_owner.data(); }
    [[nodiscard]] const std::shared_ptr<std::atomic_bool>& cancelToken() const { return m_cancelToken; }

    template <typename T>
    [[nodiscard]] T* ownerAs() const { return qobject_cast<T*>(m_owner.data()); }

    static std::shared_ptr<std::atomic_bool> renewCancellationToken(std::shared_ptr<std::atomic_bool>& slot)
    {
        if (slot) {
            slot->store(true);
        }
        slot = std::make_shared<std::atomic_bool>(false);
        return slot;
    }

    static void cancel(std::shared_ptr<std::atomic_bool>& slot)
    {
        if (slot) {
            slot->store(true);
        }
    }

private:
    QPointer<QObject> m_owner;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
};
