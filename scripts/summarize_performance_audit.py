"""Validate complete runs and publish reproducible performance evidence."""
import csv
import hashlib
import json
import math
import shutil
import statistics
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / 'build-single-instance-audit/performance-20261026'
RESULTS = AUDIT / 'results'
OUT = ROOT / 'docs/performance-audit-20261026'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    summary = []
    for dataset in ('baseline', 'scaled5'):
        groups = defaultdict(list)
        raw = [json.loads(line) for line in (RESULTS / f'samples-{dataset}.jsonl').read_text().splitlines()]
        for row in raw:
            groups[row['scenario']].append(row)
        if not groups:
            raise ValueError('No samples')
        for scenario, all_rows in groups.items():
            rows = [r for r in all_rows if not r['warmup']]
            if len(all_rows) != 23 or sorted(r['iteration'] for r in rows) != list(range(1, 21)):
                raise ValueError(f'Incomplete series: {dataset}/{scenario}')
            values = sorted(r['wall_ms'] for r in rows)
            summary.append(dict(dataset=dataset, scenario=scenario, kind=rows[0]['kind'], n=len(rows),
                failed=sum(not r['ok'] for r in rows), warmup_failed=sum(not r['ok'] for r in all_rows if r['warmup']),
                median_ms=statistics.median(values), p95_ms=values[math.ceil(.95*len(values))-1], max_ms=max(values),
                cpu_median_ms=statistics.median(r['cpu_ms'] for r in rows), cpu_max_ms=max(r['cpu_ms'] for r in rows),
                cpu_one_core_median_pct=statistics.median(r['cpu_one_core_pct'] for r in rows),
                working_set_peak_mib=max(r['peak_working_set_mb'] for r in rows),
                private_peak_mib=max(r['peak_private_mb'] for r in rows),
                ui_gap_max_ms=max((r['ui_max_gap_ms'] or 0) for r in rows) if rows[0]['kind']=='ui' else None,
                rows_min=min(r['rows'] for r in rows), rows_max=max(r['rows'] for r in rows)))
        for name in (f'samples-{dataset}.jsonl', f'sql-profile-{dataset}.json', f'runtime-counts-{dataset}.json', f'test-{dataset}.txt'):
            shutil.copy2(RESULTS / name, OUT / name)
    baseline = {r['scenario']: r for r in summary if r['dataset']=='baseline'}
    scaled = {r['scenario']: r for r in summary if r['dataset']=='scaled5'}
    if baseline.keys() != scaled.keys():
        raise ValueError('Scenario mismatch')
    for row in summary:
        row['scale_median_ratio'] = scaled[row['scenario']]['median_ms']/baseline[row['scenario']]['median_ms']
    with (OUT/'summary.csv').open('w', newline='', encoding='utf-8-sig') as f:
        writer=csv.DictWriter(f, fieldnames=summary[0].keys());writer.writeheader();writer.writerows(summary)
    (OUT/'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    env=json.loads((AUDIT/'environment.json').read_text(encoding='utf-8-sig'))
    env['testHash']=digest(ROOT/'build-single-instance-audit/tests/PurchasePaymentTests.exe')
    env['currentDistHash']=digest(ROOT/'dist/CarServicePointG.exe')
    env['distUnchanged']=env['currentDistHash'].lower()==env['exeHash'].lower()
    (OUT/'environment.json').write_text(json.dumps(env, ensure_ascii=False, indent=2), encoding='utf-8')
    manifest=json.loads((AUDIT/'scaled5/dataset-manifest.json').read_text())
    for filename, data in manifest['files'].items():
        if digest(AUDIT/'scaled5'/filename) != data['sha256']:
            raise ValueError('Scaled source changed: '+filename)
    expected={'CarService_archive_legacy.db':'9f0c0c8a6f689c2ad98226cdb8eca42b7bed0911d1f00125f47aa729c542d92b',
              'CarService_registry.db':'f89eebf85080b5cf7adf50f10adf0d7aac5c5a87d9612e9da52bc91a1b8a415d',
              'CarService_workspace.db':'216ae76b33b1e41c5d2a750eb9bbccc08089a57b4bd32444df491affe6f12d38'}
    for filename, sha in expected.items():
        if digest(AUDIT/'baseline'/filename)!=sha:
            raise ValueError('Baseline source changed: '+filename)
    manifest['baseline_sha256']=expected
    manifest['input_hashes_verified_after_runs']=True
    (OUT/'dataset-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    n=len(baseline)
    lines=['# Аудит швидкодії CarServicePointG — 26.10.2026', '',
      f'Виконано {n} сценаріїв на двох ізольованих наборах: по 3 прогрівальні та 20 вимірюваних повторів. Разом {n*40} вимірювань; помилок у вимірюваних повторах: {sum(r["failed"] for r in summary)}. Помилок прогріву: {sum(r["warmup_failed"] for r in summary)}.', '',
      '## Дані та методика', '',
      'Поточну БД отримано через SQLite backup з робочого runtime. Кожен запуск додатково копіює набір в окремий профіль; маршрути БД переписані на цю копію. Синхронізацію та email вимкнено. Робочі БД не використовувалися для запису. Вхідні SHA-256 повторно перевірені після вимірювань. Робочий dist не оновлювався.', '',
      '×5 означає кількість бізнес-записів, а не байти файлів: наряди, запчастини, роботи, платежі, CRM, каталог та пов’язані записи дубльовані зі зміною ключів. Конфігурація, персонал, календарні дати, журнали та зовнішні знімки постачальників спільні. Це синтетичне повторення поточного розподілу, а не незалежна вибірка нових клієнтів/дат. Цілісність SQLite — ok, порушень FK — 0. Повний склад: [dataset-manifest.json](dataset-manifest.json).', '',
      '| Фактична маршрутизована таблиця | Поточна | ×5 |', '|---|---:|---:|']
    a=json.loads((OUT/'runtime-counts-baseline.json').read_text());b=json.loads((OUT/'runtime-counts-scaled5.json').read_text())
    for key in a:
        lines.append(f'| {key} | {a[key]} | {b[key]} |')
    lines += ['', 'Архів і workspace містять записи, що перетинаються: їх фізичні кількості не підсумовуються як унікальні наряди.', '',
      'Стенд: Ryzen 9 7940HS, 8 ядер / 16 логічних процесорів, 31.22 GiB RAM; Windows 11 Pro 10.0.26200; Qt 6.10.2 MSVC x64, Release. Набори запускалися послідовно. Це локальні вимірювання на робочому комп’ютері без ексклюзивного резервування CPU; фонова активність може впливати на максимум.', '',
      'Використано тестовий виконуваний файл із сервісами та QWidget застосунку. Це не повний життєвий цикл dist EXE. Перед кожним сервісним/UI повтором очищено кеші PartsQueryService, ReportsSnapshotCache, OrderSnapshotCache; кеш ОС/SQLite та інші кеші не примусово очищалися. Debug/info журнали вимкнені, warnings увімкнені. 3 прогріви виключені зі статистики. p95 — 19-й елемент відсортованих 20 вимірювань (nearest rank). Медіана — середнє 10-го та 11-го.', '',
      'CPU — різниця user+kernel часу всього тестового процесу через GetProcessTimes. CPU % нормовано на одне логічне ядро (для частки всієї 16-потокової машини поділити на 16); короткі операції мають квантування, тому 0 ms не означає відсутність роботи. RAM — найбільше спостережене working set / private bytes через K32GetProcessMemoryInfo, опитування кожні 10 ms та на межах; короткий пік може бути пропущений. Це пам’ять всього процесу, а не приріст окремої функції; алокатор може утримувати пам’ять попередніх сценаріїв.', '',
      'Сервісні звіти охоплюють 2000–2099 роки; history обмежено сторінкою 200 записів. PDF — місячний звіт за жовтень 2026, включає формування та запис. UI сторінки використовують свої початкові фільтри, окрім invoices (весь період); час охоплює створення, перше готове наповнення та закриття. Пауза UI — найбільший інтервал таймера 10 ms всередині цього сценарію, а не час відповіді на фізичне натискання. backup — VACUUM основної workspace БД, не всіх файлів і не тест відновлення.', '',
      '## Час виконання', '', '| Сценарій | Поточна median / p95 / max, ms | ×5 median / p95 / max, ms | Відношення медіан |', '|---|---:|---:|---:|']
    for key, r in baseline.items():
        s=scaled[key]
        def times(x):return ' / '.join(f'{x[k]:.2f}' for k in ('median_ms','p95_ms','max_ms'))
        lines.append(f'| {key} | {times(r)} | {times(s)} | {r["scale_median_ratio"]:.2f} |')
    lines += ['', '## CPU та пам’ять', '', '| Набір / сценарій | CPU median / max, ms | CPU median, % одного ядра | WS peak, MiB | Private peak, MiB | UI gap max, ms |', '|---|---:|---:|---:|---:|---:|']
    for r in summary:
        gap='—' if r['ui_gap_max_ms'] is None else f'{r["ui_gap_max_ms"]:.2f}'
        lines.append(f'| {r["dataset"]} / {r["scenario"]} | {r["cpu_median_ms"]:.2f} / {r["cpu_max_ms"]:.2f} | {r["cpu_one_core_median_pct"]:.1f} | {r["working_set_peak_mib"]:.2f} | {r["private_peak_mib"]:.2f} | {gap} |')
    lines += ['', '## SQL-профілювання', '', 'Для п’яти запитів знято EXPLAIN QUERY PLAN та по 20 виконань із повним читанням результату на тому самому Qt SQLite з’єднанні. history — SQL, сформований реальним HistoryQueryService з доступним пошуковим індексом; інші три — діагностичні запити до маршрутизованих подань. Це вибіркове профілювання, а не трасування кожного SQL усіх сервісів. Параметр UID у матеріалах приховано. Повні SQL і плани: [поточна](sql-profile-baseline.json), [×5](sql-profile-scaled5.json).', '']
    for dataset in ('baseline','scaled5'):
        plans=json.loads((OUT/f'sql-profile-{dataset}.json').read_text())
        for p in plans:
            details=[row['detail'] for row in p['plan']]
            lines.append(f'- {dataset} / {p["name"]}: SCAN — {sum("SCAN " in d for d in details)}, SEARCH — {sum("SEARCH " in d for d in details)}, TEMP B-TREE — {sum("TEMP B-TREE" in d for d in details)}; помилка плану: {p["error"] or "немає"}.')
    lines += ['', '## Оцінка та межі висновків', '',
      'Орієнтири з наявної документації: search p95 < 1 s (docs/audit-stage1-2026-09-30/README.md) та паузи UI до 500 ms (docs/GLOBAL_PERFORMANCE_STABILITY_AUDIT_TZ_2026_06_06.md). Тут пошук виміряно на сервісному рівні, а паузи — при відкритті сторінок, тому це діагностичне порівняння, а не повне приймання користувацького сценарію чи старту програми.', '']
    for dataset in ('baseline','scaled5'):
        searches=[r for r in summary if r['dataset']==dataset and r['scenario'] in ('current.number','history.number','current.missing','history.missing')]
        worst=max(searches,key=lambda r:r['p95_ms'])
        lines.append(f'- {dataset}: найбільший p95 пошуку {worst["p95_ms"]:.2f} ms ({worst["scenario"]}); орієнтир 1000 ms {"виконано" if worst["p95_ms"]<1000 else "перевищено"}.')
        slow=[r for r in summary if r['dataset']==dataset and r['kind']=='ui' and r['ui_gap_max_ms']>500]
        lines.append(f'- {dataset}: UI gap > 500 ms: '+(', '.join(f'{r["scenario"]} ({r["ui_gap_max_ms"]:.0f} ms)' for r in slow) or 'не зафіксовано')+'.')
    lines += ['', 'Найповільніші сценарії ×5 за p95:', '']
    for r in sorted(scaled.values(),key=lambda r:r['p95_ms'],reverse=True)[:5]:
        lines.append(f'- {r["scenario"]}: p95 {r["p95_ms"]:.2f} ms, max {r["max_ms"]:.2f} ms, медіана зросла ×{r["scale_median_ratio"]:.2f}.')
    lines += ['', 'Рекомендації: першими профілювати найдовші UI сценарії, розділивши завантаження даних, побудову віджетів та відмальовування; перевірити повторні refresh і перенесення важких обчислень з UI-потоку. Для звітів перевірити кількість повторних запитів на наряд/рядок. SCAN або TEMP B-TREE самі по собі не доводять потреби нового індексу: зміни перевіряти окремим порівняльним прогоном.', '',
      'Окрема гіпотеза за кодом: src/core/services/PartsQueryService.cpp:69–150 об’єднує три джерела через UNION ALL і корельовані NOT EXISTS з TRIM/COALESCE та OR; у loadRows (рядок 842) додатково виконується JOIN з альтернативними умовами ідентифікації наряду. Такі умови можуть ускладнювати використання простих індексів і спричиняти надлінійне зростання. Це кандидат для наступного точкового SQL-трасування, не доведена причина всього виміряного часу parts.all: вибіркові плани у цьому звіті не містять цього повного складеного запиту.', '',
      'Не перевірялися холодний запуск і splash, фізичне введення/натискання, мережа/ELIT/cloud/email, імпорт/OCR, оновлювач, закриття головного застосунку, аварії та відновлення, конкуренція і 8-годинна надійність. Тому цей звіт завершує описані локальні серії вимірювань, але не підтверджує виконання всього ширшого ТЗ. Початкові пілоти не включено у статистику; зокрема разовий PDF за весь архів ~21.8 s був іншим обсягом роботи й не є p95. Перший повний запуск ×5 обірвав глобальний тайм-аут QtTest 300 s під час reports.orders. Його JSONL і лог збережено в build-single-instance-audit/performance-20261026/interrupted-timeout; після збільшення QTEST_FUNCTION_TIMEOUT до 3600000 ms серію ×5 запущено заново. Статистика не змішує перерваний та повторний прогони.', '',
      '## Відтворення та артефакти', '',
      '`scripts/run_performance_audit.ps1 -Dataset baseline`, потім `-Dataset scaled5`; після завершення обох — `python scripts/summarize_performance_audit.py`. Вхідні копії з приватними даними залишаються лише у build-single-instance-audit/performance-20261026; у звіті немає копій БД чи PDF.', '',
      '[CSV](summary.csv), [JSON зведення](summary.json), [середовище й SHA-256 виконуваних файлів](environment.json), [сирі поточні вимірювання](samples-baseline.jsonl), [сирі ×5 вимірювання](samples-scaled5.jsonl), [лог baseline](test-baseline.txt), [лог ×5](test-scaled5.txt). Пам’ять/час кожного повтору, прогріви, кількість рядків та помилки збережені в JSONL.']
    headline = ['', 'Ключовий результат: збільшення основних даних у 5 разів спричинило надлінійне уповільнення частини операцій. Медіана parts.all зросла у '+f'{scaled["parts.all"]["scale_median_ratio"]:.2f}'+ ' раза. Найвищий p95 у ×5 — '+f'{max(r["p95_ms"] for r in scaled.values())/1000:.2f}'+ ' s. Максимальна пауза UI: запчастини '+f'{scaled["ui.parts.open-close"]["ui_gap_max_ms"]/1000:.2f}'+ ' s, борги '+f'{scaled["ui.debts.open-close"]["ui_gap_max_ms"]/1000:.2f}'+ ' s. Ці сценарії потребують оптимізації; відсутність помилок не означає прийнятної швидкодії.', '',
        'Найбільший спостережений working set за обома серіями: '+f'{max(r["working_set_peak_mib"] for r in summary):.2f}'+ ' MiB; private bytes: '+f'{max(r["private_peak_mib"] for r in summary):.2f}'+ ' MiB. Повні CPU/RAM, медіана/p95/максимум і сирі повтори наведені нижче та в CSV/JSON.']
    lines[3:3]=headline
    (OUT/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(json.dumps({'scenarios_per_dataset':n,'measurements':n*40,'failures':sum(r['failed'] for r in summary),'dist_unchanged':env['distUnchanged'],'report':str(OUT/'README.md')},ensure_ascii=False))


if __name__=='__main__':
    main()
