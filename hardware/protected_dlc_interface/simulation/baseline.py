#!/usr/bin/env python3
"""Export exact revision A from Git, reproduce its model report and stuck-TX gap.

No checkout/reset. Output is a build artifact; historical A results stay A.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

BASE='b2cf4ea12a783d4eb81736383f51790b361d9b04'
REPO=Path(__file__).resolve().parents[3]

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--ngspice',default='ngspice')
    ap.add_argument('--out',type=Path,default=REPO/'build/electrical-baseline')
    ap.add_argument('--sources-only',action='store_true'); args=ap.parse_args()
    out=args.out.resolve(); source=out/'source'; source.mkdir(parents=True,exist_ok=True)
    names=subprocess.check_output(['git','ls-tree','-r','--name-only',BASE,
        'hardware/protected_dlc_interface','firmware/shared/reference_fixtures.hpp'],cwd=REPO,text=True).splitlines()
    assert names,'Baseline commit unavailable; electrical checkout must fetch history'
    hashes={}
    for name in names:
        target=(source/name).resolve()
        assert target.is_relative_to(source)
        data=subprocess.check_output(['git','show',f'{BASE}:{name}'],cwd=REPO)
        target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
        hashes[name]=hashlib.sha256(data).hexdigest()
    manifest={'revision':'A','source_sha':BASE,'files_sha256':hashes,'physical_status':'NOT VERIFIED'}
    (out/'baseline-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    if args.sources_only:return 0
    script=source/'hardware/protected_dlc_interface/simulation/run.py'
    subprocess.run([sys.executable,str(script),'--ngspice',args.ngspice,'--out',str(out/'model-A')],check=True)
    report_path=out/'model-A/summary.json'; report=json.loads(report_path.read_text())
    report['execution_checkout_sha']=report['git_sha']; report['git_sha']=BASE; report['revision']='A'
    report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    # Run frozen A for20ms at both healthy rails and a continuous HIGH request.
    sys.path.insert(0,str(script.parent.parent))
    spec=importlib.util.spec_from_file_location('revision_A_runner',script)
    runner=importlib.util.module_from_spec(spec); spec.loader.exec_module(runner)
    case=runner.default_case('continuous_high_A',kind='fault',fault_v=0,rs=1e12,
                             rpu=1000,vpu=5.25,tx=1,usb=5,battery=9,field=5)
    folder=out/'continuous-high-A'; folder.mkdir(exist_ok=True)
    _,stop=runner.netlist(case,folder)
    circuit=folder/'scenario.cir'
    circuit.write_text(circuit.read_text().replace(f'tran .25u {stop:.12g} 0 .25u','tran .25u .02 0 .25u'))
    result=subprocess.run([args.ngspice,'-b','scenario.cir'],cwd=folder,capture_output=True,text=True,timeout=180)
    (folder/'engine.log').write_text(result.stdout+result.stderr)
    if result.returncode or any(t.lower() in (result.stdout+result.stderr).lower()
                               for t in ['timestep too small','doAnalyses:','fatal error']):
        raise RuntimeError('Baseline simulator error is not evidence of a failsafe failure')
    trace=runner.read_trace(folder/'trace.dat'); assert trace[0][-1]>=.02-1e-9
    gate=runner.at(trace,.019,5); bus=runner.at(trace,.019,1)
    checks=[dict(check='own_gate_released_after_5ms',value=gate,limit=1.3,
                 margin=1.3-gate,status='FAIL' if gate>1.3 else 'PASS'),
            dict(check='fixture_bus_released_after_5ms',value=bus,limit=3.3,
                 margin=bus-3.3,status='FAIL' if bus<3.3 else 'PASS')]
    assert all(c['status']=='FAIL' for c in checks),'Frozen A no longer reproduces reported stuck-TX gap'
    gap=dict(revision='A',source_sha=BASE,status='EXPECTED_BASELINE_FAILSAFE_FAILURE',
             scenario=case,simulated_duration_s=.02,checks=checks,
             interpretation='Valid numerical transient: healthy powered A holds its own sink indefinitely under HIGH request.',
             physical_status='NOT VERIFIED')
    (out/'stuck-tx-baseline.json').write_text(json.dumps(gap,indent=2)+'\n',encoding='utf-8')
    (folder/'trace.dat').unlink()
    print('revision A model reproduced; independent stuck-TX failsafe criterion fails as expected')
    return 0

if __name__=='__main__':sys.exit(main())
