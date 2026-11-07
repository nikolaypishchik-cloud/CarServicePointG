#pragma once

class QObject;

class DeferredStartupMaintenance
{
public:
    static void schedule(QObject* owner, int initialDelayMs = 2500);
};
