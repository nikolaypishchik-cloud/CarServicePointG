#include "core/backup/BackupPerformanceGuard.h"

#include "core/backup/BackupManager.h"

BackupPerformanceGuard::BackupPerformanceGuard(BackupManager* manager, int holdMs)
    : m_manager(manager)
    , m_active(manager != nullptr)
{
    if (m_manager) {
        m_manager->holdInteractiveMode(holdMs);
    }
}

BackupPerformanceGuard::~BackupPerformanceGuard()
{
    if (m_active && m_manager) {
        m_manager->releaseInteractiveMode();
    }
}

BackupPerformanceGuard::BackupPerformanceGuard(BackupPerformanceGuard&& other) noexcept
    : m_manager(other.m_manager)
    , m_active(other.m_active)
{
    other.m_manager = nullptr;
    other.m_active = false;
}

BackupPerformanceGuard& BackupPerformanceGuard::operator=(BackupPerformanceGuard&& other) noexcept
{
    if (this == &other) return *this;
    if (m_active && m_manager) {
        m_manager->releaseInteractiveMode();
    }
    m_manager = other.m_manager;
    m_active = other.m_active;
    other.m_manager = nullptr;
    other.m_active = false;
    return *this;
}

void BackupPerformanceGuard::hold(BackupManager* manager, int holdMs)
{
    if (manager) manager->holdInteractiveMode(holdMs);
}

void BackupPerformanceGuard::release(BackupManager* manager)
{
    if (manager) manager->releaseInteractiveMode();
}
