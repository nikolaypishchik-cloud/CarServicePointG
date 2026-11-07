#pragma once
#include "PerformanceSamples.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QThread>
#include <QCoreApplication>
#include <QEventLoop>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>

namespace PerformanceAudit {
struct Result { bool ok=true; int rows=-1; QString error; };
inline Result checked(int rows, const QString& error={}) { return {error.isEmpty(),rows,error}; }
inline bool waitUntil(const std::function<bool()>& ready, int limit=60000) {
    QElapsedTimer timer; timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        if(ready()) return true;
        QThread::msleep(2);
    } while(timer.elapsed()<limit);
    return false;
}
inline double privateMb() {
#ifdef Q_OS_WIN
    using Fn=BOOL(WINAPI*)(HANDLE,PPROCESS_MEMORY_COUNTERS,DWORD);
    auto fn=reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));
    PROCESS_MEMORY_COUNTERS_EX memory{};
    if(fn && fn(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&memory),sizeof(memory)))
        return memory.PrivateUsage/1048576.0;
#endif
    return -1;
}
class Recorder {
    QFile output;
    QString dataset;
public:
    int failures=0;
    explicit Recorder(const QString& path, const QString& label):output(path),dataset(label) {
        if(!output.open(QIODevice::WriteOnly|QIODevice::Truncate)) qFatal("Cannot open performance output");
    }
    void run(const QString& name, const QString& kind, const std::function<Result()>& action,
             const std::function<void()>& prepare={}) {
        const auto selection=qEnvironmentVariable("CS_PERF_SCENARIOS");
        if(!selection.isEmpty() && !selection.split(',').contains(name)) return;
        fprintf(stdout,"AUDIT %s %s\n",qPrintable(dataset),qPrintable(name));fflush(stdout);
        const int repetitions=qEnvironmentVariableIsSet("CS_PERF_PILOT")?1:20;
        const int warmups=qEnvironmentVariableIsSet("CS_PERF_PILOT")?0:3;
        for(int i=-warmups;i<repetitions;++i) {
            if(prepare)prepare();
            QCoreApplication::processEvents();
            std::atomic<bool> stop{false};
            double peakRam=PerformanceSamples::workingSetMb(), peakPrivate=privateMb();
            std::thread sampler([&]{while(!stop.load()){
                peakRam=qMax(peakRam,PerformanceSamples::workingSetMb());
                peakPrivate=qMax(peakPrivate,privateMb());
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }});
            QElapsedTimer elapsed;elapsed.start();
            double lastBeat=0, maxGap=0;
            QTimer heartbeat;heartbeat.setTimerType(Qt::PreciseTimer);heartbeat.setInterval(10);
            QObject::connect(&heartbeat,&QTimer::timeout,[&]{const double now=elapsed.nsecsElapsed()/1e6;maxGap=qMax(maxGap,now-lastBeat);lastBeat=now;});
            heartbeat.start();
            const double cpuStart=PerformanceSamples::processCpuMs();
            const Result result=action();
            const double ms=elapsed.nsecsElapsed()/1e6;
            const double cpu=PerformanceSamples::processCpuMs()-cpuStart;
            maxGap=qMax(maxGap,ms-lastBeat);heartbeat.stop();
            stop=true;sampler.join();
            peakRam=qMax(peakRam,PerformanceSamples::workingSetMb());peakPrivate=qMax(peakPrivate,privateMb());
            QJsonObject row{{"dataset",dataset},{"scenario",name},{"kind",kind},{"iteration",i+1},
                {"warmup",i<0},{"ok",result.ok},{"rows",result.rows},{"error",result.error},
                {"wall_ms",ms},{"cpu_ms",cpu},{"cpu_one_core_pct",ms>0?cpu*100/ms:0},
                {"peak_working_set_mb",peakRam},{"peak_private_mb",peakPrivate},
                {"ui_max_gap_ms",kind=="ui"?QJsonValue(maxGap):QJsonValue(QJsonValue::Null)}};
            output.write(QJsonDocument(row).toJson(QJsonDocument::Compact)+'\n');output.flush();
            if(!result.ok && i>=0)++failures;
        }
    }
};
}
