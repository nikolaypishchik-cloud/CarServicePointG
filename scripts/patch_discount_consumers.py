from pathlib import Path
import shutil
R=Path(__file__).resolve().parents[1]
def edit(name,pairs):
 p=R/name;s=p.read_text(encoding='utf-8-sig')
 for a,b in pairs:
  assert a in s,(name,a);s=s.replace(a,b)
 back=R/'build-single-instance-audit/financial-fix-source-backup'/name
 back.parent.mkdir(parents=True,exist_ok=True)
 if not back.exists():shutil.copy2(p,back)
 p.write_text(s,encoding='utf-8')
edit('src/core/services/LegacyMutationLogSupport.cpp',[
 ('COALESCE(discount,0) AS discount "','COALESCE(discount,0) AS discount, COALESCE(discount_mode,\'legacy\') AS discount_mode "')])
edit('src/core/services/ReportsDataLoaderSnapshotFlow.cpp',[
 ('COALESCE(wo.discount,0), COALESCE(wo.works_sum,0) "','COALESCE(wo.discount,0), COALESCE(wo.works_sum,0), COALESCE(wo.discount_mode,\'legacy\') "'),
 ('FormatUtils::inferDiscountPercent(discountRaw, worksTotal)','FormatUtils::discountPercent(discountRaw, q.value(4).toString(), worksTotal)')])
edit('src/core/services/ReportsSyncSupport.cpp',[
 ('COALESCE(works_sum,0) FROM router_work_orders','COALESCE(works_sum,0), COALESCE(discount_mode,\'legacy\') FROM router_work_orders'),
 ('FormatUtils::inferDiscountPercent(dq.value(0).toDouble(), dq.value(1).toDouble())','FormatUtils::discountPercent(dq.value(0).toDouble(), dq.value(2).toString(), dq.value(1).toDouble())'),
 ('COALESCE(adj.absent,0) "','COALESCE(adj.absent,0), COALESCE(wo.discount_mode,\'legacy\') "'),
 ('FormatUtils::inferDiscountPercent(discountRaw, worksTotal)','FormatUtils::discountPercent(discountRaw, q.value(10).toString(), worksTotal)')])
edit('src/core/services/StatisticsSnapshotService.cpp',[
 ('COALESCE(m.admin_percent,0) "','COALESCE(m.admin_percent,0), COALESCE(wo.discount_mode,\'legacy\') "'),
 ('FormatUtils::inferDiscountPercent(q.value(1).toDouble(), q.value(2).toDouble())','FormatUtils::discountPercent(q.value(1).toDouble(), q.value(6).toString(), q.value(2).toDouble())'),
 ('COALESCE(discount,0) "','COALESCE(discount,0), COALESCE(discount_mode,\'legacy\') "'),
 ('FormatUtils::inferDiscountPercent(closedQ.value(6).toDouble(), grossWorks)','FormatUtils::discountPercent(closedQ.value(6).toDouble(), closedQ.value(7).toString(), grossWorks)')])
edit('src/core/services/orders/LocalWorkshopBackend.cpp',[
 ('#include "LocalWorkshopBackend.h"','#include "LocalWorkshopBackend.h"\n#include "core/util/FormatUtils.h"'),
 ('cents(number(o["discount"]))','cents(FormatUtils::discountAmount(number(o["discount"]),o["discount_mode"].toString(),subtotal(services)/100.0))'),
 ('{{"discount",o["discount"]},{"parts",bp}','{{"discount",o["discount"]},{"discount_mode",o["discount_mode"]},{"parts",bp}')])
edit('server/app/services/order_closure_service.py',[
 ('total = max(0, subtotal(part_data) + subtotal(service_data) - cents(number(data.get("discount", 0))))','''labor_cents = subtotal(service_data)
    discount = number(data.get("discount", 0))
    if data.get("discount_mode") == "percent":
        if discount > 100:
            raise HTTPException(422, "Знижка у відсотках має бути від 0 до 100")
        discount = Decimal(labor_cents) / 100 * discount / 100
    total = max(0, subtotal(part_data) + labor_cents - cents(discount))'''),
 ('dict(discount=str(data.get("discount", 0)),','dict(discount=str(data.get("discount", 0)), discount_mode=data.get("discount_mode", "legacy"),')])
edit('src/core/services/ReportsPdfSupportSnapshotFlow.cpp',[
 ('COALESCE(wo.works_sum,0) FROM router_work_orders','COALESCE(wo.works_sum,0), COALESCE(wo.discount_mode,\'legacy\') FROM router_work_orders'),
 ('row.amount = ordersQ.value(8).toDouble();','row.amount = FormatUtils::roundMoneyValue(ordersQ.value(8).toDouble() * (1.0 - FormatUtils::discountPercent(ordersQ.value(9).toDouble(), ordersQ.value(11).toString(), ordersQ.value(10).toDouble()) / 100.0));'),
 ('qRound64(row.amount * pm.percent / 100.0)','FormatUtils::roundMoneyValue(row.amount * pm.percent / 100.0)'),
 ('qRound64(pm.worksTotal * pm.percent / 100.0)','FormatUtils::roundMoneyValue(pm.worksTotal * pm.percent / 100.0)')])
