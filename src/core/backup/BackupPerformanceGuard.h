#pragma once

class BackupManager;

class BackupPerformanceGuard
{
public:
    explicit BackupPerformanceGuard(BackupManager* manager = nullptr, int holdMs = 15000);
    ~BackupPerformanceGuard();

    BackupPerformanceGuard(const BackupPerformanceGuard&) = delete;
    BackupPerformanceGuard& operator=(const BackupPerformanceGuard&) = delete;

    BackupPerformanceGuard(BackupPerformanceGuard&& other) noexcept;
    BackupPerformanceGuard& operator=(BackupPerformanceGuard&& other) noexcept;

    static void hold(BackupManager* manager, int holdMs = 15000);
    static void release(BackupManager* manager);

private:
    BackupManager* m_manager = nullptr;
    bool m_active = false;
};
