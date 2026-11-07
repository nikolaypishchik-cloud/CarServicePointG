#pragma once
#include "core/services/PeriodReportGraph.h"
#include "core/services/ReportsPdfSupportLocalSupport.h"
#include "core/services/ReportsDateUtils.h"
#include "core/util/FormatUtils.h"
#include <algorithm>
namespace PeriodReportRows {
struct Result {QList<ReportsPdfSupport::PeriodPdfRow> rows;double parts=0,works=0,margin=0;};
inline Result build(const PeriodReportGraph::Graph& graph,const ReportsPdfSupport::ReportMarkupSnapshot& markupSnapshot) {
    using namespace ReportsPdfSupport;
    using namespace ReportsPdfSupport::LocalSupport;
    Result result;auto& out=result.rows;auto& partsTotal=result.parts;auto& worksTotal=result.works;auto& partsMargin=result.margin;
    struct ServiceRow { int id = 0; QString title; double amount = 0.0; };
    struct PartRow {
        int id = 0;
        int serviceId = 0;
        QString name;
        QString qty;
        double clientAmount = 0.0;
        double added = 0.0;
        bool assigned = false;
    };
    for(const auto& ord:graph.orders) {
        const QString orderUid=ord.value(6).toString();
        const QString orderNumber=ord.value(1).toString(),car=ord.value(2).toString(),plate=formatPlateForUi(ord.value(3).toString()),orderDate=ord.value(4).toString();
        QList<ServiceRow> services;for(const auto& row:graph.services.value(orderUid))services.append({row.value(0).toInt(),row.value(1).toString(),row.value(2).toDouble()});
        QList<PartRow> parts;
        for(const auto& qp:graph.parts.value(orderUid)) {
            const double qty = qp.value(3).toDouble();
            const PartFinancials financials = resolvePartFinancialsForReports(markupSnapshot,
                                                                              qp.value(1).toString(),
                                                                              qp.value(2).toString(),
                                                                              qty,
                                                                              qp.value(4).toDouble(),
                                                                              qp.value(5).toDouble(),
                                                                              qp.value(6).toDouble(),
                                                                              qp.value(7).toDouble(),
                                                                              qp.value(8).toInt() != 0, qp.value(10).toInt());
            PartRow pr;
            pr.id = qp.value(0).toInt();
            pr.name = qp.value(1).toString();
            pr.qty = qty > 0.0 ? FormatUtils::smartQty(qty) : QString();
            pr.clientAmount = financials.clientAmount;
            pr.added = financials.added;
            pr.serviceId = qp.value(9).toInt();
            parts.append(pr);
        }
        if (services.isEmpty() && parts.isEmpty()) { PeriodPdfRow r; r.date=ReportsDateUtils::displayDate(orderDate); r.orderNumber=orderNumber; r.car=car; r.plate=plate; out.append(r); continue; }
        for (const ServiceRow& service : services) {
            QList<int> matched;
            for (int i=0;i<parts.size();++i) if (!parts[i].assigned && parts[i].serviceId>0 && parts[i].serviceId==service.id) matched.append(i);
            if (matched.isEmpty()) {
                QList<QPair<int,int>> scored;
                for (int i=0;i<parts.size();++i) { if (parts[i].assigned) continue; const int score=partServiceMatchScore(service.title, parts[i].name); if (score>0) scored.append({score,i}); }
                std::sort(scored.begin(), scored.end(), [](const auto& a,const auto& b){ return a.first>b.first; });
                for (const auto& pair : scored) matched.append(pair.second);
            }
            if (matched.isEmpty()) { PeriodPdfRow r; r.date=ReportsDateUtils::displayDate(orderDate); r.orderNumber=orderNumber; r.car=car; r.plate=plate; r.workTitle=service.title; r.worksPrice=service.amount; r.total=service.amount; out.append(r); worksTotal += service.amount; continue; }
            bool first=true; double partsSum=0.0; double addedSum=0.0;
            for (int idx : matched) { auto &pr=parts[idx]; pr.assigned=true; PeriodPdfRow r; r.date=ReportsDateUtils::displayDate(orderDate); r.orderNumber=orderNumber; r.car=car; r.plate=plate; if(first){ r.workTitle=service.title; r.worksPrice=service.amount; first=false; } r.partName=pr.name; r.qty=pr.qty; r.partsPrice=pr.clientAmount; r.added=pr.added; partsSum+=pr.clientAmount; addedSum+=pr.added; r.total=r.worksPrice + r.partsPrice; out.append(r); }
            worksTotal += service.amount; partsTotal += partsSum; partsMargin += addedSum;
        }
        for (auto &pr : parts) if (!pr.assigned) { PeriodPdfRow r; r.date=ReportsDateUtils::displayDate(orderDate); r.orderNumber=orderNumber; r.car=car; r.plate=plate; r.partName=pr.name; r.qty=pr.qty; r.partsPrice=pr.clientAmount; r.added=pr.added; r.total=pr.clientAmount; out.append(r); partsTotal += pr.clientAmount; partsMargin += pr.added; }
    }
    return result;
}
}
