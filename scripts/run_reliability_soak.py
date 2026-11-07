"""Isolated desktop-handler soak. 120 minutes means completed active test time, not setup.
Run --pilot first. This is a partial UI matrix, never a full end-to-end certification.
"""
import argparse, ctypes, hashlib, json, os, shutil, subprocess, time
from datetime import datetime, timezone
from pathlib import Path
from run_qt_test import diagnostic_error

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'build-single-instance-audit/reliability-20261028'
CASES=[
 ('planningDailyAndWeeklyViews','planning','UI mode switch; seeded appointments, not full booking creation'),
 ('invoiceImportPreviewKeepsScreenBounds','invoice','UI review, selection/bulk editing; no file recognition'),
 ('dialogPartialAndCancel','supplier_payment','UI partial/cancel/full, persisted supplier balance'),
 ('inventoryJournalPayment','inventory','UI journal/payment'),
 ('spareStockReceiptIssueAndRollback','stock','UI receipt/issue with service assertions and rollback'),
 ('spareStockDifferentVehicleConfirmation','stock','UI wrong-vehicle confirmation'),
 ('invoiceRegistryUiLoadsAndFilters','invoice','UI filters and model'),
 ('supplierReconciliationDialogSavesEditedAmount','supplier_debt','UI reconciliation'),
 ('historyReopenPreservesPaymentsAndInstalledParts','order','service reopen + independent DB assertions; not UI'),
 ('financialReturnsRefundAndCredit','returns','service refund/credit + DB assertions; not UI'),
 ('spareBatchMixedInvoiceIsAtomic','invoice','service mixed invoice import and repeat; not UI'),
 ('paymentPersistsAfterReopen','supplier_payment','service payment/reopen DB; not application restart'),
]

def stamp():return datetime.now(timezone.utc).isoformat()
def write(path,data):
    tmp=path.with_suffix('.tmp');tmp.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8');tmp.replace(path)
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

class Memory(ctypes.Structure):
    _fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)]+[(n,ctypes.c_size_t) for n in
       ['peak_working','working','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile','private']]
def memory(pid):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.restype=ctypes.c_void_p
    kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    handle=kernel.OpenProcess(0x410,False,pid)
    if not handle:return None
    try:
        value=Memory();value.cb=ctypes.sizeof(value)
        api=ctypes.WinDLL('psapi');api.GetProcessMemoryInfo.argtypes=[ctypes.c_void_p,ctypes.POINTER(Memory),ctypes.c_ulong]
        if api.GetProcessMemoryInfo(handle,ctypes.byref(value),value.cb):
            return {'private_bytes':value.private,'working_bytes':value.working}
    finally:kernel.CloseHandle(handle)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--pilot',action='store_true')
    parser.add_argument('--minutes',type=int,choices=range(120,181),default=120)
    parser.add_argument('--audit-dir',type=Path,default=BASE)
    args=parser.parse_args()
    base=args.audit_dir.resolve()
    if not base.is_relative_to((ROOT/'build-single-instance-audit').resolve()):
        parser.error('--audit-dir must be inside build-single-instance-audit')
    out=base/('pilot' if args.pilot else 'soak')
    if out.exists():
        parser.error('Choose a fresh audit directory to preserve previous measurements')
    base.mkdir(parents=True,exist_ok=True)
    lock=base/'runner.lock'
    fd=os.open(lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY);os.write(fd,str(os.getpid()).encode());os.close(fd)
    state={'phase':'preparing','prepared_at':stamp(),'target_active_minutes':args.minutes,
           'pilot':args.pilot,'complete_acceptance':False,'active_seconds':0,'cases':0,'failures':0,'cycles':0}
    try:
        out.mkdir()
        runtime=base/'runtime';runtime.mkdir(exist_ok=True)
        manifest=json.loads((ROOT/'dist/runtime-manifest.json').read_text(encoding='utf-8'))
        for rel,info in manifest['files'].items():
            source=ROOT/'dist'/rel;target=runtime/rel
            if not target.exists() or target.stat().st_size!=info['size']:
                target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
            if sha(target)!=info['sha256']:raise RuntimeError('Runtime hash mismatch: '+rel)
        exe=runtime/'PurchasePaymentTests.exe'
        shutil.copy2(ROOT/'build-single-instance-audit/tests/PurchasePaymentTests.exe',exe)
        env={k:v for k,v in os.environ.items() if not k.startswith('CS_')}
        profile=out/'profile';profile.mkdir(exist_ok=True)
        temp=profile/'temp';temp.mkdir(exist_ok=True)
        cwd=out/'cwd';(cwd/'build-single-instance-audit').mkdir(parents=True,exist_ok=True)
        env.update(CS_FINANCIAL_AUDIT='1',CS_ELIT_DISABLE_STARTUP='1',CS_TEST_DATA_ROOT=str(profile),
                   TEMP=str(temp),TMP=str(temp),QT_QPA_PLATFORM='windows',
                   QT_PLUGIN_PATH=str(runtime/'plugins'),QTEST_FUNCTION_TIMEOUT='45000')
        env['PATH']=str(runtime)+';'+env['PATH']
        # windeployqt commonly stores platform plugins beside the exe.
        if not (runtime/'plugins/platforms').exists():env['QT_PLUGIN_PATH']=str(runtime)
        write(out/'matrix.json',[dict(case=c,module=m,scope=s) for c,m,s in CASES])
        write(out/'environment.json',{'runtime':str(runtime),'profile':str(profile),'temp':str(temp),
            'settings':'QSettings IniFormat under temporary fixture directory',
            'network':'Qt application HTTP proxy 127.0.0.1:9; no live opt-ins',
            'test_exe_sha256':sha(exe),'production_exe_sha256':sha(ROOT/'dist/CarServicePointG.exe')})
        state.update(phase='running',started_at=stamp());write(out/'status.json',state)
        rounds=1 if args.pilot else 1000000
        for cycle in range(rounds):
            for case,module,scope in CASES:
                ordinal=state['cases']+1;log=out/f'{ordinal:06d}-{case}.txt'
                begin=time.monotonic();peak=0
                with (out/f'{ordinal:06d}-console.txt').open('wb') as stream:
                    child=subprocess.Popen([str(exe),case,'-o',str(log)+',txt'],cwd=cwd,env=env,stdout=stream,stderr=stream)
                    while child.poll() is None and time.monotonic()-begin<60:
                        sample=memory(child.pid)
                        if sample:peak=max(peak,sample['private_bytes'])
                        time.sleep(.25)
                    timeout=child.poll() is None
                    if timeout:child.kill() # owned test child only; never the desktop application
                    code=child.wait()
                elapsed=time.monotonic()-begin;text=log.read_bytes().decode('utf-8',errors='replace') if log.exists() else ''
                isolated='ISOLATION_VERIFIED' in text
                record=dict(index=ordinal,cycle=cycle+1,case=case,module=module,scope=scope,at=stamp(),
                    seconds=elapsed,exit=code,timeout=timeout,isolated=isolated,peak_private_bytes=peak)
                with (out/'operations.jsonl').open('a',encoding='utf-8') as stream:stream.write(json.dumps(record)+'\n')
                valid=code==0 and 'Totals:' in text and not diagnostic_error(text)
                state['cases']+=1;state['failures']+=int(not valid)
                if not timeout and isolated and 0 <= code <= 255:state['active_seconds']+=elapsed
                state.update(last_case=case,last_exit=code,updated_at=stamp(),peak_last_private_bytes=peak)
                write(out/'status.json',state);print(json.dumps(record),flush=True)
                if not isolated:raise RuntimeError('Isolation preflight failed; no further tests allowed')
                if timeout or code<0 or code>255:
                    raise RuntimeError('Test hang/crash; preserve evidence and repair before sustained run')
                if not valid:
                    raise RuntimeError(f'{case} failed or produced incomplete diagnostics; see {log}')
                if state['active_seconds']>=args.minutes*60:break
            state['cycles']=cycle+1
            if state['active_seconds']>=args.minutes*60:break
        state.update(phase='pilot_complete' if args.pilot else 'duration_complete',finished_at=stamp())
        write(out/'status.json',state)
    except BaseException as exc:
        state.update(phase='interrupted',finished_at=stamp(),error=str(exc));write(out/'status.json',state)
        raise
    finally:lock.unlink(missing_ok=True)
if __name__=='__main__':main()
