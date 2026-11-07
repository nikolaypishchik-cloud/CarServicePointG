#pragma once

#include <QElapsedTimer>
#include <QDebug>
#include <QVector>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace PerformanceSamples {
inline double processCpuMs()
{
#ifdef Q_OS_WIN
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        const auto ticks = [](const FILETIME& value) {
            return (quint64(value.dwHighDateTime) << 32) | value.dwLowDateTime;
        };
        return (ticks(kernel) + ticks(user)) / 10000.0;
    }
#endif
    return -1;
}

inline double workingSetMb()
{
#ifdef Q_OS_WIN
    using ReadMemory = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    const auto readMemory = reinterpret_cast<ReadMemory>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
    PROCESS_MEMORY_COUNTERS memory{};
    if (readMemory && readMemory(GetCurrentProcess(), &memory, sizeof(memory)))
        return memory.WorkingSetSize / (1024.0 * 1024.0);
#endif
    return -1;
}

template<class Function>
auto measure(const QString& scenario, Function&& function)
{
    const int repetitions = qBound(1, qEnvironmentVariableIntValue("CS_SEARCH_BENCH_REPEATS"), 200);
    QVector<double> samples;
    QElapsedTimer total, sample;
    const double cpuBefore = processCpuMs();
    double observedMemory = workingSetMb();
    total.start(); sample.start();
    auto result = function();
    samples.append(sample.nsecsElapsed() / 1000000.0);
    const double first = samples.first();
    for (int i = 1; i < repetitions; ++i) {
        sample.restart();
        result = function();
        samples.append(sample.nsecsElapsed() / 1000000.0);
        observedMemory = qMax(observedMemory, workingSetMb());
    }
    const double wall = total.nsecsElapsed() / 1000000.0;
    const double cpu = processCpuMs() - cpuBefore;
    std::sort(samples.begin(), samples.end());
    const int middle = samples.size() / 2;
    const double median = samples.size() % 2 ? samples[middle] : (samples[middle - 1] + samples[middle]) / 2;
    const double p95 = samples[qMax(0, int(std::ceil(samples.size() * 0.95)) - 1)];
    qInfo().noquote() << QStringLiteral("BENCH scenario=%1 n=%2 first_ms=%3 median_ms=%4 p95_ms=%5 max_ms=%6 cpu_ms=%7 cpu_one_core_percent=%8 observed_working_set_mb=%9 ui_block_ms=not_measured")
        .arg(scenario).arg(repetitions).arg(first, 0, 'f', 2).arg(median, 0, 'f', 2)
        .arg(p95, 0, 'f', 2).arg(samples.last(), 0, 'f', 2).arg(cpu, 0, 'f', 2)
        .arg(wall > 0 ? cpu * 100 / wall : 0, 0, 'f', 1).arg(observedMemory, 0, 'f', 1);
    if (repetitions < 20) qInfo() << "BENCH p95 is approximate: fewer than 20 observations";
    if (qEnvironmentVariableIsSet("CS_SEARCH_ENFORCE_SLA") && samples.last() >= 1000)
        QTest::qFail(qPrintable("Search exceeded 1 second: " + scenario), __FILE__, __LINE__);
    return result;
}
} // namespace PerformanceSamples
