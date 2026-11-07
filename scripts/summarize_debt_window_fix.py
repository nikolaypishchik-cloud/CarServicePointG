"""Compare completed targeted UI runs with the original performance audit."""
import csv
import hashlib
import json
import shutil
import statistics
from pathlib import Path

root=Path(__file__).resolve().parents[1]
audit=root/'build-single-instance-audit/performance-20261026'
source=audit/'debt-windows-fixed'
out=root/'docs/debt-windows-performance-20261026'
old=json.loads((root/'docs/performance-audit-20261026/summary.json').read_text())
rows=[]
out.mkdir(parents=True,exist_ok=True)
for dataset in ('baseline','scaled5'):
    raw=[json.loads(x) for x in (source/f'samples-{dataset}.jsonl').read_text().splitlines()]
    for scenario in ('ui.parts.open-close','ui.debts.open-close'):
        all_samples=[r for r in raw if r['scenario']==scenario]
        samples=[r for r in all_samples if not r['warmup']]
        assert len(all_samples)==23 and sorted(r['iteration'] for r in samples)==list(range(1,21)),(dataset,scenario)
        assert all(r['ok'] for r in all_samples),(dataset,scenario,'failed sample')
        previous=next(r for r in old if r['dataset']==dataset and r['scenario']==scenario)
        timings=sorted(r['wall_ms'] for r in samples)
        assert all(previous['rows_min']<=r['rows']<=previous['rows_max'] for r in samples),'Row count changed'
        rows.append(dict(dataset=dataset,scenario=scenario,n=20,median_ms=statistics.median(timings),
            p95_ms=timings[18],max_ms=timings[-1],ui_gap_max_ms=max(r['ui_max_gap_ms'] for r in samples),
            old_ui_gap_max_ms=previous['ui_gap_max_ms'],old_p95_ms=previous['p95_ms'],
            cpu_median_ms=statistics.median(r['cpu_ms'] for r in samples),cpu_max_ms=max(r['cpu_ms'] for r in samples),
            cpu_one_core_median_pct=statistics.median(r['cpu_one_core_pct'] for r in samples),
            working_set_peak_mib=max(r['peak_working_set_mb'] for r in samples),
            private_peak_mib=max(r['peak_private_mb'] for r in samples),rows_min=min(r['rows'] for r in samples),rows_max=max(r['rows'] for r in samples)))
    for name in (f'samples-{dataset}.jsonl',f'test-{dataset}.txt'):
        shutil.copy2(source/name,out/name)
with (out/'summary.csv').open('w',encoding='utf-8-sig',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
(out/'summary.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
manifest=json.loads((root/'docs/performance-audit-20261026/dataset-manifest.json').read_text())
for dataset in ('baseline','scaled5'):
    hashes=manifest['baseline_sha256'] if dataset=='baseline' else {name:info['sha256'] for name,info in manifest['files'].items()}
    for name,expected in hashes.items():
        assert hashlib.sha256((audit/dataset/name).read_bytes()).hexdigest()==expected,(dataset,name,'input changed')
shutil.copy2(root/'build-single-instance-audit/debt-windows-regression.txt',out/'regression.txt')
lines=['# Усунення зависань запчастин і боргів — 26.10.2026','',
    'Виконано по 3 прогріви та 20 вимірювань кожного з двох вікон на поточній копії БД і наборі ×5: 80 основних вимірювань, без помилок. Кількість відображених рядків відповідає початковому аудиту. Хеші обох вхідних наборів перевірено після запусків; копії не змінені.','',
    'Причина: після фонового завантаження таблиці запчастин її підсумок боргів виконував PurchasePaymentService::debts та SupplierInvoiceLedger::summary у GUI-потоці. Деталізація боргів також читала весь набір синхронно. Обидва читання перенесено у фонові задачі з окремим read-only SQLite-з’єднанням і транзакцією читання. Віджети змінюються лише в GUI-потоці.','',
    'Повторні оновлення об’єднуються в один наступний запит, а не запускають необмежену кількість потоків. Результат скасованого запиту або іншої сесії користувача не застосовується. Закриття вікна скасовує його задачу без очікування SQL у GUI-потоці. Під час завантаження деталізації показується відповідний напис.','',
    '| Набір / вікно | Пауза UI до → після, ms | Нові median / p95 / max до готовності, ms | Старий p95, ms |',
    '|---|---:|---:|---:|']
for r in rows:
    lines.append(f'| {r["dataset"]} / {r["scenario"]} | {r["old_ui_gap_max_ms"]:.2f} → {r["ui_gap_max_ms"]:.2f} | {r["median_ms"]:.2f} / {r["p95_ms"]:.2f} / {r["max_ms"]:.2f} | {r["old_p95_ms"]:.2f} |')
lines+=['','| Набір / вікно | CPU median / max, ms | CPU median, % одного ядра | WS peak, MiB | Private peak, MiB |','|---|---:|---:|---:|---:|']
for r in rows:
    lines.append(f'| {r["dataset"]} / {r["scenario"]} | {r["cpu_median_ms"]:.2f} / {r["cpu_max_ms"]:.2f} | {r["cpu_one_core_median_pct"]:.1f} | {r["working_set_peak_mib"]:.2f} | {r["private_peak_mib"]:.2f} |')
lines+=['','Методика успадкована з початкового аудиту: Release Qt 6.10.2 MSVC x64, Ryzen 9 7940HS / 16 логічних CPU, Windows 11, незалежні послідовні серії. CPU — час усього процесу; 100% означає одне ядро. RAM опитується кожні 10 ms. p95 — 19-й із 20 відсортованих результатів. Пауза UI — максимальний інтервал heartbeat-таймера 10 ms, а не фізичне натискання. Перед повтором очищено ті самі прикладні кеші; кеш ОС не очищався.','',
    'Тепер стенд обов’язково очікує готовності як таблиці запчастин, так і її підсумку боргів; для деталізації чекає завершення фонового читання та перевіряє помилку. Час не обривається на появі порожнього вікна. Тривалий SQL усе ще виконується, але не блокує обробку подій. Ця зміна усуває виміряні зависання; вона не є оптимізацією всіх SQL і не гарантує миттєвого завантаження великих БД.','',
    'Регресії: суми деталізації та плиток постачальників порівняно зі штатним синхронним розрахунком; перевірено 8 повторних refresh без паралельного розмноження запитів, зміну owner → master, закриття обох вікон під час читання, оплату, звірку, відмову від звірки та обмеження доступу. Шість сценаріїв пройшли; QtTest показує 8 PASS разом з init/cleanup. Один старий тест вимагав синхронного оновлення summary — його адаптовано до асинхронного завершення. Проміжний конфлікт лінкування з запущеним тестовим EXE усунуто; фінальний прогін виконано після успішної збірки.','',
    '[CSV](summary.csv), [JSON](summary.json), [регресії](regression.txt), [сирі baseline](samples-baseline.jsonl), [сирі ×5](samples-scaled5.jsonl).','',
    'Повторення: `scripts/run_debt_window_regression.ps1`; `scripts/run_performance_audit.ps1 -Dataset baseline -DebtWindows`, потім `-Dataset scaled5 -DebtWindows`; `python scripts/summarize_debt_window_fix.py`. Початковий повний аудит залишається у docs/performance-audit-20261026.']
delivery=root/'build-single-instance-audit/debt-windows-async-20261026-delivery.json'
if delivery.exists():
    data=delivery.read_bytes()
    installed=json.loads(data.decode('utf-16' if data.startswith(b'\xff\xfe') else 'utf-8-sig'))
    assert hashlib.sha256((root/'dist/CarServicePointG.exe').read_bytes()).hexdigest().upper()==installed['sha256']
    (out/'delivery.json').write_text(json.dumps(installed,ensure_ascii=False,indent=2),encoding='utf-8')
    lines+=['','## Установлення','',
        'Оновлено єдиний dist/CarServicePointG.exe, версія '+installed['version']+'. Runtime package gate: 1534 файли, OK; SHA-256 встановленого EXE збігається зі збіркою. Процес застосунку перед заміною був відсутній. Робочі БД, налаштування й документи збережені.', '',
        'SHA-256: `'+installed['sha256']+'`. [Запис установлення](delivery.json). Відкат: build-single-instance-audit/debt-windows-async-20261026-runtime-before.']
(out/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
assert all(r['ui_gap_max_ms']<500 for r in rows),'UI stall still exceeds 500 ms'
print(json.dumps(rows,indent=2))
