#pragma once

class RuntimeLoggingControl
{
public:
    static void configureEnvironmentForRuntime();
    static void configureForRuntime();
    static bool deepDebugEnabled();
};
