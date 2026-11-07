#pragma once

#include <QString>

class ThemeManager
{
public:
    static void applySavedTheme();
    static void setTheme(const QString& name); // "graphite" | "light"
    static QString currentTheme(); // returns saved theme (or default)
};
