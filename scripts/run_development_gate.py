"""Non-release ratchet. Original gates remain mandatory for the desktop executable."""
import argparse, collections, hashlib, json, pathlib, re, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('script');parser.add_argument('--record',action='store_true');args=parser.parse_args()
script=ROOT/'scripts'/args.script
allowed={'check_runtime_blocking_gate.py','check_sql_exec_accountability.py','check_performance_tz_100_completion.py','check_monolith_split_gate.py','check_legacy_tail_inventory.py','check_legacy_marker_ci_gate.py','check_project_layout.py','check_perf_guards.py'}
if args.script not in allowed:raise SystemExit('Unsupported development gate')
run=subprocess.run([sys.executable,'-B',str(script)],cwd=ROOT,capture_output=True,text=True,encoding='utf-8',errors='replace')
print(run.stdout,end='');print(run.stderr,end='',file=sys.stderr)
errors=[l.strip().lstrip('-').strip() for l in (run.stdout+'\n'+run.stderr).splitlines() if l.lstrip().startswith('-')]
if args.script=='check_perf_guards.py':
 errors=[l.strip() for l in (run.stdout+'\n'+run.stderr).splitlines() if l.startswith('[P4] ')]
if run.returncode not in (0,1) or (run.returncode and not errors):raise SystemExit('Gate execution failed; cannot baseline a crash')
def anchor(message):
 def repl(m):
  lines=(ROOT/m[1]).read_text(encoding='utf-8',errors='replace').splitlines();n=int(m[2])
  return m[1]+':'+(' '.join(lines[n-1].split()) if 0<n<=len(lines) else '<invalid>')
 return re.sub(r'(src/[A-Za-z0-9_./-]+):(\d+)',repl,message)
counts=collections.Counter();entries=[]
for error in sorted(errors):
 key=anchor(error);counts[key]+=1
 entries.append(dict(fingerprint=hashlib.sha256((key+'|'+str(counts[key])).encode()).hexdigest(),diagnostic=error,anchor=key))
path=ROOT/'docs'/('development-baseline-'+script.stem+'.json')
if args.record:
 with path.open('x',encoding='utf-8') as f:json.dump(dict(schema=1,gate_sha256=hashlib.sha256(script.read_bytes()).hexdigest(),entries=[dict(e,owner_role='architecture maintainer',stage='R3/R4',review_by='2026-11-28',reason='Known R0 architecture debt; blocks release') for e in entries]),f,ensure_ascii=False,indent=2)
try:
 baseline=json.loads(path.read_text(encoding='utf-8'))
 assert baseline['schema']==1 and baseline['gate_sha256']==hashlib.sha256(script.read_bytes()).hexdigest()
 assert all(e.get('reason') and e.get('review_by') for e in baseline['entries'])
 old={e['fingerprint'] for e in baseline['entries']};new={e['fingerprint'] for e in entries}
except (OSError,ValueError,KeyError,AssertionError):raise SystemExit('Invalid development baseline or changed gate implementation')
print(f'DEVELOPMENT ONLY: debt={len(entries)}, new={len(new-old)}, resolved={len(old-new)}. Release remains blocked by original gate.')
raise SystemExit(1 if old!=new else 0)
