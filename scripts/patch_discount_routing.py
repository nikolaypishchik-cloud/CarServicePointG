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
edit('src/infrastructure/db/sqlite/DbArchiveRoutingSupport.cpp',[
 ('        ensureRouterCacheColumn(QStringLiteral("order_uid")','        if (!ensureRouterCacheColumn(QStringLiteral("discount_mode"), QStringLiteral("discount_mode TEXT NOT NULL DEFAULT \'legacy\'"))) return false;\n        ensureRouterCacheColumn(QStringLiteral("order_uid")'),
 ('            ensureActiveOrderColumn(QStringLiteral("client_uid")','            if (!ensureActiveOrderColumn(QStringLiteral("discount_mode"), QStringLiteral("discount_mode TEXT NOT NULL DEFAULT \'legacy\'"))) return false;\n            ensureActiveOrderColumn(QStringLiteral("client_uid")'),
 ('o.crm_backfilled_at FROM work_orders','o.crm_backfilled_at, o.discount_mode FROM work_orders'),
 ('c.crm_backfilled_at "','c.crm_backfilled_at, c.discount_mode "')])
edit('src/infrastructure/db/sqlite/DbArchiveRouterCacheImportSupport.cpp',[
 ('    add(QStringLiteral("discount"),','    add(QStringLiteral("discount_mode"), firstAvailableExpr(srcOrders, {QStringLiteral("discount_mode")}, QStringLiteral("\'legacy\'")));\n    add(QStringLiteral("discount"),'),
 ('car_color, owner_name) "','car_color, owner_name, discount_mode) "'),
 ('COALESCE(owner_name,\'\') "','COALESCE(owner_name,\'\'), COALESCE(discount_mode,\'legacy\') "')])
edit('src/infrastructure/db/sqlite/DbArchiveHydrationSupport.cpp',[
 ('car_color, owner_name "','car_color, owner_name, discount_mode "'),
 ('car_color, owner_name) "','car_color, owner_name, discount_mode) "'),
 ('VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)','VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)'),
 ('i <= 28;','i <= 29;')])
edit('src/infrastructure/db/sqlite/OrderArchiveShardSqlSupport.cpp',[
 ('car_color, owner_name) "','car_color, owner_name, discount_mode) "'),
 ('car_color, owner_name FROM work_orders','car_color, owner_name, discount_mode FROM work_orders')])
edit('src/infrastructure/db/sqlite/internal/DbArchiveRoutingWorkspaceState.ipp',[
 ('        add("discount",','        add("discount_mode", firstAvailableExpr(legacyOrders, {"discount_mode"}, "\'legacy\'"));\n        add("discount",'),
 ('%29 AS owner_name "','%29 AS owner_name, %30 AS discount_mode "'),
 ('firstAvailableExpr(legacyOrders, {"owner_name", "client"}, "\'\'")));','firstAvailableExpr(legacyOrders, {"owner_name", "client"}, "\'\'"), firstAvailableExpr(legacyOrders, {"discount_mode"}, "\'legacy\'")));'),
 ('car_color, owner_name) "','car_color, owner_name, discount_mode) "'),
 ('VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)','VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)'),
 ('i <= 28;','i <= 29;')])
