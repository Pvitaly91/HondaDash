#!/usr/bin/env python3
"""Numerical reference-grid check; never a physical timing measurement."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import run as electrical

def edges(t,usb):
    high=t[2][0]>.6*usb; out=[]
    for i in range(1,len(t[0])):
        a,b=t[2][i-1],t[2][i]; threshold=(.3 if high else .6)*usb
        changed=(a>threshold>=b) if high else (a<threshold<=b)
        if changed:
            time=t[0][i-1]+(threshold-a)*(t[0][i]-t[0][i-1])/(b-a)
            high=not high;out.append((time,high))
    return out

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--ngspice',default='ngspice')
    ap.add_argument('--out',type=Path,default=Path('build/electrical/convergence'));args=ap.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    groups=[];checks=[]
    case=electrical.default_case('reference_R2200_C500p',rpu=2200,cbus=500e-12,vpu=4.75,field=5.25,usb=5.25)
    for step in [.25e-6,.125e-6]:
        folder=out/str(step);folder.mkdir(exist_ok=True)
        frames,stop=electrical.netlist(case,folder)
        p=folder/'scenario.cir';text=p.read_text(encoding='utf-8')
        text=re.sub(r'(?m)^tran .*$',f'tran {step} {stop} .095 {step} uic',text)
        p.write_text(text,encoding='utf-8');electrical.run_spice(folder,args.ngspice)
        t=electrical.read_trace(folder/'trace.dat');assert t[0][-1]>=stop-1e-9
        check=electrical.assess(case,t,frames)
        if check['status']!='PASS':raise RuntimeError('Reference scenario fails its physical/model criteria')
        groups.append(edges(t,case['usb']));checks.append(check)
        (folder/'trace.dat').unlink()
    count_ok=len(groups[0])==len(groups[1]) and all(a[1]==b[1] for a,b in zip(*groups))
    delta=max((abs(a[0]-b[0]) for a,b in zip(*groups)),default=1)
    report={'status':'PASS' if count_ok and delta<=.4e-6 else 'FAIL','complete':True,
        'model_sha256':hashlib.sha256((electrical.ROOT/'simulation/models.cir').read_bytes()).hexdigest(),
        'scenario':case,'steps_s':[.25e-6,.125e-6],'edge_counts':[len(x) for x in groups],
        'edge_polarities_match':count_ok,'max_edge_delta_s':delta,'criterion_s':.4e-6,
        'margin_s':.4e-6-delta,'physical_status':'NOT VERIFIED',
        'scope':'Selected reference-corner numerical convergence, not off-grid or physical proof'}
    (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(report['status'],'max D8 edge delta',delta,'s',flush=True)
    return 0 if report['status']=='PASS' else 1

if __name__=='__main__':sys.exit(main())
