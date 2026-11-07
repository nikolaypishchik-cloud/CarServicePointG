"""Build auditable result tables from recorded runs; never infer PASS from missing output."""
import csv, json, re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build-single-instance-audit/financial-audit'
DEST=ROOT/'docs/financial-audit-20261028'
DEST.mkdir(exist_ok=True)
actual=json.loads((OUT/'actual.json').read_text(encoding='utf-8'))
with (DEST/'calculations.csv').open('w',encoding='utf-8-sig',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=list(actual[0]))
    writer.writeheader();writer.writerows(actual)
lines=['# Незалежний еталон: розрахунки','',
       'Усі грошові значення нижче — у копійках. Рядок discount_percent — соті частки відсотка, не гроші.',
       'Еталон: Decimal ROUND_HALF_UP до 0,01 грн; ціна одиниці, ціна після націнки, потім сума рядка. Для знижки 50% — явно задане трактування; поточний код трактує її як 50 грн.',
       '', '| Показник | Сценарій | Очікувано | Фактично | Різниця |','|---|---|---:|---:|---:|']
for row in actual:
    lines.append('| {metric} | {scenario} | {expected_cents} | {actual_cents} | {difference_cents} |'.format(**row))
(DEST/'calculations.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
cases=json.loads((OUT/'case-results.json').read_text(encoding='utf-8'))
lines=['# Повторний запуск тестів','', '| Сценарій | Код виходу | Результат |','|---|---:|---|']
for row in cases:
    lines.append(f"| {row['case']} | {row['exit']} | {'PASS' if row['exit']==0 else 'FAIL / див. лог'} |")
lines+=['','## Набори тестів','']
for name in ['LocalWorkshopBackendTests','OrderClosureDialogTests','AdditionalExpenseTests',
             'RegressionMarkupRecalculationTests','ReportsOrdersTests','returns-and-debt-recheck','returns-final','db-context']:
    path=OUT/(name+'.txt')
    if not path.exists():continue
    text=path.read_bytes().decode('utf-8',errors='replace')
    totals=re.findall(r'Totals:.*',text)
    lines.append(f"- {name}: {totals[-1] if totals else 'нет завершённого итога; не считать PASS'}")
lines+=['','## Ошибки и предупреждения','',
        'Полный PurchasePaymentTests прерван после зависания elitCredentialsSettings; неполный запуск не считается успешным. Далее финансовые сценарии запускались отдельно с тайм-аутом 45 секунд.', '']
for path in sorted(OUT.glob('*.txt')):
    if path.name.endswith('.stderr.txt'):continue
    text=path.read_bytes().decode('utf-8',errors='replace')
    errors=re.findall(r'^FAIL!.*',text,re.M)
    connection=[line for line in text.splitlines() if 'QCRITICAL' in line and 'DB_CONNECTION' in line]
    rollback=[line for line in text.splitlines() if 'QCRITICAL' in line and 'rollback' in line]
    if errors or connection or rollback:
        lines+=['### '+path.name,'',f'DB_CONNECTION QCRITICAL: {len(connection)}; QCRITICAL rollback: {len(rollback)}.']
        for error in errors:lines.append('- '+error)
        if connection:lines+=['', 'Пример: `'+connection[0]+'`']
        lines.append('')
(DEST/'test-results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('oracle metrics',len(actual),'different',sum(r['difference_cents']!=0 for r in actual))
print('isolated cases',len(cases),'passed',sum(r['exit']==0 for r in cases))
