"""One-time, backed-up source migration for the 2026-10-28 financial fixes."""
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[1]
BACK=ROOT/'build-single-instance-audit/financial-fix-source-backup'
def edit(name, replacements):
    p=ROOT/name; s=p.read_text(encoding='utf-8-sig')
    for old,new in replacements:
        assert old in s,(name,old)
        s=s.replace(old,new)
    b=BACK/name;b.parent.mkdir(parents=True,exist_ok=True)
    if not b.exists():shutil.copy2(p,b)
    p.write_text(s,encoding='utf-8')
for name in ['src/core/services/ReportsDataLoaderSnapshotFlow.cpp','src/core/services/ReportsSyncSupport.cpp','src/core/services/StatisticsSnapshotService.cpp']:
    p=ROOT/name;s=p.read_text(encoding='utf-8-sig')
    pairs=[]
    for expr in ['baseSum * percent / 100.0','discountedAmount * payrollPercent / 100.0','row.amount * percent / 100.0','discounted * q.value(5).toDouble() / 100.0']:
        if 'qRound64('+expr+')' in s:pairs.append(('qRound64('+expr+')','FormatUtils::roundMoneyValue('+expr+')'))
    if pairs:edit(name,pairs)
edit('src/core/services/StatisticsSnapshotService.cpp',[
 ('FormatUtils::money(v, 0) +','FormatUtils::money(v, 2) +'),
 ('    if (structure) *structure =', '''    if (db.tables().contains(QStringLiteral("additional_expenses"))) {
        QSqlQuery extra(db);
        extra.prepare(QStringLiteral("SELECT COALESCE(SUM(amount_cents),0) FROM additional_expenses WHERE date(spent_on)>=date(?) AND date(spent_on)<=date(?)"));
        extra.addBindValue(from.toString(Qt::ISODate));
        extra.addBindValue(to.toString(Qt::ISODate));
        if (extra.exec() && extra.next()) other += extra.value(0).toLongLong() / 100.0;
    }
    if (structure) *structure ='''),
 ('COALESCE(closed_at,\'\') "','COALESCE(closed_at,\'\'), COALESCE(discount,0) "'),
 ('const double works = closedQ.value(2).toDouble();','const double grossWorks = closedQ.value(2).toDouble();\n            const double works = FormatUtils::roundMoneyValue(grossWorks * (1.0 - FormatUtils::inferDiscountPercent(closedQ.value(6).toDouble(), grossWorks) / 100.0));')])
