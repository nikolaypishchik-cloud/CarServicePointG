# DBY-T191 — payroll graph projection and index reuse

DBY-03/08, task base b89fc79; desktop PDF increment 845fa35 integrated without changing its code or test bodies. User-confirmed weekly remaining 43%, stop <=5%. Only PayrollIdentityRead production code changed. PDF page code and PDF test bodies are untouched. No private data, dist, gates or budgets changed.

## Scope

The prior graph loader materialized every column in QMap, including unused payloads. Each employee read rebuilt both complete UID/numeric indexes despite sharing the graph. The loader now prepares metadata with `SELECT * ... LIMIT 0`, checks the same required columns, and selects only the required and available optional fields. Missing optional UID/display columns keep their prior empty-value behavior. All fields consulted by the reader are retained.

The successfully loaded graph owns its two indexes. Employee reads reference those indexes without rebuilding them. Index lifetime is exactly the graph lifetime; there is no cross-request or connection cache. Each reload builds a fresh graph/index pair. Failed schema, execution or row-evaluation reads publish no partial/stale graph. Transaction ownership remains with the caller.

There are five metadata-only statements plus five projected data statements per complete graph load, versus five unrestricted data statements previously. Two indexes are built per graph load, versus two per employee read previously. No claims of fewer SQL statements: the saving is payload materialization and repeated index work. Financial/identity algorithms, allocation ordering and per-work half-up math are unchanged.

## Linux synthetic profile

Command: `bash scripts/profile_payroll_graph.sh`.

The script compiles the unchanged b89fc79 header under a separate test namespace and the current production header into one optimized executable. Separate fresh processes measure each version on the same read-only SQLite fixture. No other build/profile was run during the recorded measurement. Each timed iteration includes transaction begin, graph load, eight employee reads, serialization of every PayrollOrderRow field and commit. Hashing is outside the timed region. Three warmups precede 20 samples; percentiles use nearest rank. Peak RSS is whole-process Linux getrusage high-water RSS, not allocator delta.

The generated 1× source has 200 headers, 800 services and 1600 split assignments plus deliberately unused payload columns; 5× multiplies those counts. These sources are not the user's profiles and these absolute measurements are not comparable to the desktop measurements before/after the UID correction.

| Synthetic scale | p50 before/after ms | p95 before/after ms | Peak RSS before/after MiB | Result rows | Base/pay cents |
|---|---:|---:|---:|---:|---:|
| 1× | 84.346 / 57.592 | 127.225 / 105.817 | 16.176 / 12.656 | 1000 | 889600 / 444800 |
| 5× | 546.361 / 477.158 | 687.632 / 552.709 | 41.066 / 23.734 | 5000 | 4448000 / 2224000 |

All 23 iterations of each run have the same complete-field digest. Before/after digests match and independent financial/count oracles pass. Source file SHA-256 remains unchanged. Only aggregate results are emitted; fixture files are removed afterward. Complete-field digests: 1× `d14310435bc0c3e9e03f0d715f4e11d7740df5192cd02119936d8d2d093cdc44`; 5× `4e4b71356b82f78211dec3d1e78798846d661cfefa9467b35e491f2e9edff56a`.

## Verification

- `bash scripts/run_payroll_graph_projection.sh`: PASS. Unused 1 MiB payload columns excluded, all identity/display columns retained; all ten result fields checked; 20 repeated master/split reads; owner-number reload rebuilds indexes; caller rollback preserved; each missing dependency and required-column error; a real SQLite integer-overflow error during row evaluation; repair/repeat; old schema without optional columns.
- `bash scripts/run_payroll_identity.sh`: PASS. Existing public detail/text/snapshot UID conflicts, split, absent/deleted ownership, exact cents, dependency failures and repeats.
- `bash scripts/run_payroll_read_failures.sh`: PASS. Existing old-schema, missing/denied/corrupt source, nine SQL dependency failures, cache rejection and exact sums.
- Native test and production consumers syntax checked with Qt; static gates unchanged: `python -B scripts/run_change_gates.py --static-only` PASS.

Exact syntax command:

```bash
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
/workspace/qt-sdk/root/usr/lib/qt6/libexec/moc -Isrc -I"$qt" -I"$qt/QtCore" tests/AdditionalExpenseTests.cpp -o build-single-instance-audit/AdditionalExpenseTests.moc
g++ -std=c++17 -fPIC -DQT_WIDGETS_LIB -DQT_GUI_LIB -fsyntax-only -Isrc -Ibuild-single-instance-audit -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" -I"$qt/QtGui" -I"$qt/QtWidgets" -I"$qt/QtNetwork" -I"$qt/QtPrintSupport" -I"$qt/QtTest" tests/AdditionalExpenseTests.cpp src/core/services/ReportsPdfSupportSnapshotFlow.cpp src/core/services/ReportsSyncSupport.cpp src/core/services/ReportsDataLoaderSnapshotFlow.cpp
```

## Windows handoff

New AdditionalExpenseTests slot: `payrollGraphProjectionAndIndexReuse`. Also rerun the unchanged `payrollIdentityOwnersSplitAndRepeat` and `payrollReadFailuresAndRecovery`. Native execution here is NOT_RUN; syntax is not Windows acceptance. Desktop owns PDF tests/display and will repeat its identical financial/time protocol. Its reported pre-task p95 values 11.46/20.58 ms (1×) and 138.70/117.69 ms (5×), memory 7.45/8.57 and 9.79/12.90 MiB, remain desktop evidence, not measurements made in this cloud run.

DBY-03/08 remain IN_PROGRESS pending Windows comparison and wider acceptance. No full application or PDF rendering performance claim is made from this graph microbenchmark.
