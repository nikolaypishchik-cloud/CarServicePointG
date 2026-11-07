#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-single-instance-audit/payroll-graph
python - <<'PY'
import subprocess
from pathlib import Path
s=subprocess.check_output(['git','show','b89fc79:src/core/services/PayrollIdentityRead.h'],text=True)
Path('build-single-instance-audit/payroll-graph/PreviousPayrollGraph.h').write_text(s.replace('namespace PayrollIdentityRead {','namespace PreviousPayrollGraph {'))
PY
export LD_LIBRARY_PATH=/workspace/qt-sdk/root/usr/lib/x86_64-linux-gnu
export QT_PLUGIN_PATH=$LD_LIBRARY_PATH/qt6/plugins
qt=/workspace/qt-sdk/root/usr/include/x86_64-linux-gnu/qt6
g++ -O2 -std=c++17 -fPIC -Isrc -Ibuild-single-instance-audit/payroll-graph -I"$qt" -I"$qt/QtCore" -I"$qt/QtSql" tests/PayrollGraphBenchmark.cpp src/core/services/ReportsDateUtils.cpp src/core/util/DateFormats.cpp src/core/util/FormatUtils.cpp src/core/util/PlateUtils.cpp -L"$LD_LIBRARY_PATH" -lQt6Sql -lQt6Core -o build-single-instance-audit/payroll-graph/benchmark
fixture_root=$(mktemp -d "$PWD/build-single-instance-audit/payroll-graph/synthetic.XXXXXX")
trap 'rm -rf "$fixture_root"' EXIT
for scale in 1 5; do
    build-single-instance-audit/payroll-graph/benchmark setup "$fixture_root/$scale.db" "$scale"
    sha256sum "$fixture_root/$scale.db" > "$fixture_root/$scale.sha"
    printf 'synthetic_scale=%s\n' "$scale"
    build-single-instance-audit/payroll-graph/benchmark before "$fixture_root/$scale.db" "$scale" 2>&1 | tee "$fixture_root/before.log"
    build-single-instance-audit/payroll-graph/benchmark after "$fixture_root/$scale.db" "$scale" 2>&1 | tee "$fixture_root/after.log"
    python - "$fixture_root/before.log" "$fixture_root/after.log" "$scale" <<'PYTEST'
import re,sys
from pathlib import Path
before,after=[dict(re.findall(r'(\w+)=([^ ]+)',Path(p).read_text().strip())) for p in sys.argv[1:3]]
for field in ('rows','base_cents','pay_cents','digest'):
    assert before[field]==after[field],(field,before[field],after[field])
scale=int(sys.argv[3])
assert int(after['rows'])==1000*scale
assert int(after['base_cents'])==889600*scale
assert int(after['pay_cents'])==444800*scale
print('all_fields_and_independent_totals_match=1')
PYTEST
    sha256sum --check --status "$fixture_root/$scale.sha"
    printf 'source_bytes_unchanged=1\n'
done
