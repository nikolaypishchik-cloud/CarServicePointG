#pragma once

#include <QStringList>

struct OutsourceReportFilter {
    enum class ReturnPlan { All, Scheduled, Missing };
    QString company;
    ReturnPlan plan = ReturnPlan::All;
    QString search;

    bool accepts(const QString& supplier, const QString& returned, const QString& sent,
                 const QString& order, const QString& part) const {
        if (!company.isEmpty() && company != supplier) return false;
        if (plan == ReturnPlan::Scheduled && returned.isEmpty()) return false;
        if (plan == ReturnPlan::Missing && !returned.isEmpty()) return false;
        if (search.isEmpty()) return true;
        for (const auto& value : {supplier, returned, sent, order, part})
            if (value.contains(search, Qt::CaseInsensitive)) return true;
        return false;
    }

    QString description() const {
        QStringList labels;
        if (!company.isEmpty()) labels << QStringLiteral("Компанія: %1").arg(company);
        if (plan != ReturnPlan::All)
            labels << (plan == ReturnPlan::Scheduled ? QStringLiteral("План повернення: вказано")
                                                     : QStringLiteral("План повернення: не вказано"));
        if (!search.isEmpty()) labels << QStringLiteral("Пошук: %1").arg(search);
        return labels.join(QStringLiteral("  ·  "));
    }
};
