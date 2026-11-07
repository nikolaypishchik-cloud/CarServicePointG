"""Independent Decimal oracle and supervised pure C++ tests; no application/DB access."""
import datetime,decimal,hashlib,json,pathlib,random,subprocess,time
ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/'build-single-instance-audit/exact-money';OUT.mkdir(parents=True,exist_ok=True)
decimal.getcontext().prec=90
rng=random.Random(20261028);cases=[]
for q,p in [('0.001','100'),('0.5','100'),('1','100'),('2','100'),('1000','0.00025'),('0.5','0.01'),('1','0.004'),('1','0.005'),('1','0.006'),('92233720368547758','1'),('92233720368547759','1')]:cases.append((q,p))
for _ in range(1000):
    q=f'{rng.randrange(1,10**12)/10**6:.6f}'
    p=f'{rng.randrange(0,10**12)/10**6:.6f}'
    cases.append((q,p))
rows=[]
for q,p in cases:
    expected=int((decimal.Decimal(q)*decimal.Decimal(p)*100).quantize(decimal.Decimal('1'),rounding=decimal.ROUND_HALF_UP))
    rows.append(f'{q} {p} '+(str(expected) if expected<=2**63-1 else 'ERROR'))
for q,p in [('0','100'),('-1','100'),('NaN','1'),('Inf','1'),('1','-1'),('1.0000001','1'),('1','0.0000001'),('1e2','1'),('1','1,25'),('1','9223372036854775808')]:rows.append(f'{q} {p} ERROR')
fixture=OUT/'oracle.txt';fixture.write_text('\n'.join(rows)+'\n',encoding='ascii')
exe=OUT/'ExactMoneyTests.exe';log=OUT/'compile.txt'
command='call "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul && cl /nologo /std:c++17 /EHsc /W4 /I"'+str(ROOT/'src')+'" "'+str(ROOT/'tests/ExactMoneyTests.cpp')+'" /Fe:ExactMoneyTests.exe /Fo:ExactMoneyTests.obj'
with log.open('w',encoding='utf-8') as stream:code=subprocess.call('cmd.exe /d /s /c "'+command+'"',cwd=OUT,stdout=stream,stderr=subprocess.STDOUT)
record={'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'compile_exit':code,'seed':20261028,'oracle':'Python Decimal ROUND_HALF_UP per line','fixture_sha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'scope':'New pure kernel only; production integration NotRun'}
if code==0:
    start=time.monotonic();p=subprocess.Popen([str(exe),str(fixture)],cwd=OUT,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    record['pid']=p.pid
    try:
        stdout,stderr=p.communicate(timeout=60);record.update(exit=p.returncode,stdout=stdout.decode(errors='replace'),stderr=stderr.decode(errors='replace'))
    except subprocess.TimeoutExpired:
        p.kill();stdout,stderr=p.communicate();record.update(exit=p.returncode,error='Timeout',stdout=stdout.decode(errors='replace'),stderr=stderr.decode(errors='replace'))
    record['seconds']=time.monotonic()-start
record['pass']=code==0 and record.get('exit')==0 and 'FAILURES=0' in record.get('stdout','') and 'CHECKS=' in record.get('stdout','')
(OUT/'result.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(record,ensure_ascii=False));raise SystemExit(0 if record['pass'] else 1)
