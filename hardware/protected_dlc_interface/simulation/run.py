#!/usr/bin/env python3
"""Separate M3b electrical checks, ngspice 42 + Python stdlib. Never opens serial.

Exit 0: all positive criteria passed and negative controls rejected.
Exit 1: infrastructure or unexpected check failure. Exit 2: --bad-only rejected.
Generated numerical reports/netlists stay under build/, never under hardware/.
"""
import argparse
from bisect import bisect_left
import csv
import hashlib
import importlib.util
import itertools
import json
import math
import platform
from datetime import datetime, timezone
from pathlib import Path
import re
import subprocess
import sys
import os
import shutil
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import design
spec=importlib.util.spec_from_file_location('arithmetic',ROOT/'calculations/check.py')
arithmetic=importlib.util.module_from_spec(spec); spec.loader.exec_module(arithmetic)

def topology_checks():
    digest=design.render(check=True)
    parts={p['ref']:p for p in design.COMPONENTS}
    # Independent critical pin assertions catch a mutually generated wrong document.
    assert parts['Q1']['pins']=={'1':'GATE','2':'SINK_RET','3':'DRAIN'}
    assert parts['D2']['pins']=={'1':'GND_F','2':'NC_D2_2','3':'SENSE'}
    assert parts['D1']['pins']=={'A':'GND_F','K':'DRAIN'}
    assert parts['U1']['mpn']=='ISO7721FDWR'
    assert {k:parts['U1']['pins'][k] for k in ['3','4','5','12','13','14']}=={
        '3':'V5_L','4':'RX_L_ISO','5':'TX_L_ISO','12':'TX_F','13':'RX_F','14':'V5_F'}
    assert parts['U22']['mpn']=='LM66100DCKR'
    assert parts['U22']['pins']=={'1':'USB5V','2':'GND_L','3':'V5_L','4':'NC_U22_4','5':'GND_L','6':'ISO_FEED'}
    assert parts['R37']['pins']=={'1':'ISO_FEED','2':'V5_L'} and parts['R37']['value']==22
    assert parts['U23']['pins']['2']=='D3' and parts['U23']['pins']['4']=='TX_L_ISO'
    assert parts['U23']['pins']['5']=='V5_L'
    assert parts['U24']['pins']['2']=='RX_L_ISO' and parts['U24']['pins']['4']=='D8'
    assert parts['U24']['pins']['5']=='USB5V'
    assert parts['U13']['pins']['3']=='V5_L' and parts['U13']['pins']['6']=='V5_L'
    assert parts['R39']['pins']=={'1':'TX_L_ISO','2':'GND_L'} and parts['R39']['value']==10000
    assert parts['R40']['pins']=={'1':'RX_L_ISO','2':'GND_L'} and parts['R40']['value']==10000
    assert parts['U3']['pins']=={'1':'RX_F','2':'VREF','3':'SENSE','4':'GND_F',
        '5':'GND_F','6':'VREF','7':'NC_U3_7','8':'V5_F'}
    assert parts['U4']['pins']=={'1':'LDO_IN','2':'GND_F','3':'NC_U4_EN','4':'NC_U4_4','5':'V5_F'}
    assert parts['R12']['value']==parts['R13']['value']==22
    assert parts['R1']['value']==220000 and parts['R3']['value']==1000000
    for p in parts.values():
        nets=set(p['pins'].values())
        if p['ref'] not in ('U1','U14'):
            assert not ('GND_L' in nets and 'GND_F' in nets)
        if p['kind']=='R' and 'ECU_DATA' in nets:
            assert p['ref'] in ('R1','R12'), 'Unapproved DATA pullup or load'
    # Check that visible SVG pins (not just metadata) correspond to all BOM rows.
    xml=ET.parse(ROOT/'schematic.svg').getroot(); ns={'s':'http://www.w3.org/2000/svg'}
    for group in xml.findall('s:g',ns):
        pintext={n.attrib['data-pin']:n.attrib['data-net'] for n in group.findall('s:text',ns) if 'data-pin' in n.attrib}
        assert pintext==parts[group.attrib['id']]['pins']
    with (ROOT/'BOM.csv').open(encoding='utf-8',newline='') as f:
        rows=list(csv.DictReader(f))
    assert len(rows)==len(parts)
    for row in rows:
        assert json.loads(row['pins'])==parts[row['ref']]['pins']
        assert row['part_number']==parts[row['ref']]['mpn']
    # Pin the physical byte stimulus to the same production A/B/invalid fixtures.
    header=(ROOT.parents[1]/'firmware/shared/reference_fixtures.hpp').read_text()
    expected={'WakeBytes':[0x68,0x6a,0xf5,0xaf,0xbf,0xb3,0xb2,0xc1,0xdb,0xb3,0xe9],
              'Rpm':[0,5,9,0xc3,0x2f,0,5,4,0xe1,0x16,0,5,255,255,0xfd],
              'Ect':[0,4,0x40,0xbc,0,4,0x20,0xdc,0,4,255,0xfd],
              'Tps':[0,4,0x58,0xa4,0,4,0xae,0x4e,0,4,0x18,0xe4]}
    for symbol,values in expected.items():
        match=re.search(r'\b'+symbol+r'(?:\[\d*\])+\s*=\s*\{(.*?)\};',header,re.S)
        assert match, f'Missing production fixture {symbol}'
        actual=[int(n,0) for n in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b',match[1])]
        assert actual==values, f'Production {symbol} changed; review electrical stimuli'
    return {'status':'PASS','components':len(parts),'design_sha256':digest,
            'scope':'pin-level source/BOM/SVG/SPICE agreement; not PCB inspection'}

def default_case(name,**kw):
    d=dict(name=name,kind='bytes',rpu=2200.,cbus=500e-12,vpu=4.75,field=4.75,usb=4.75,
           delay=3e-6,offset=.004,rx_leak=2.05e-6,tx_leak=200e-6,
           rt=1.01,rb=.99,rf=.99,refscale=1.01,period=104e-6,full=False,
           ground=0,battery=8.,ambient=25,iso_delay=17e-9,expected='PASS',
           timer_scale=1.,timer_bypass=0,arm_unsafe=0,gate_reversed=0,pg_bypass=0,
           pg_delay=.028,pg_threshold=.405,low_power_tx=1)
    d.update(kw); return d

def byte_stream(full,period,all_values=False,block=0):
    # Current production fixtures are checked independently below. No new commands.
    groups=[('tx',[0,255,85,170]),('rx',[0,255,85,170])]
    if all_values:
        values=list(range(block*32,(block+1)*32))
        groups=[('tx',values),('rx',values),('tx',[0]*16)]
    if full:
        groups += [('tx',[0x68,0x6a,0xf5,0xaf,0xbf,0xb3,0xb2,0xc1,0xdb,0xb3,0xe9])]
        for responses in [[[0,5,9,0xc3,0x2f],[0,4,0x40,0xbc],[0,4,0x58,0xa4]],
                          [[0,5,4,0xe1,0x16],[0,4,0x20,0xdc],[0,4,0xae,0x4e]],
                          [[0,5,255,255,0xfd],[0,4,255,0xfd],[0,4,0x18,0xe4]]]:
            for request,response in zip([[0x20,5,0,2,0xd9],[0x20,5,0x10,1,0xca],[0x20,5,0x14,1,0xc6]],responses):
                groups += [('tx',request),('rx',response)]
    frames=[]; edges={'tx':[(0.,0.)],'rx':[(0.,0.)]}; t=.100
    for direction,data in groups:
        t+=2*period
        for value in data:
            p=104e-6 if direction=='tx' else period
            bits=[0]+[(value>>k)&1 for k in range(8)]+[1]
            frames.append(dict(direction=direction,t=t,value=value,bits=bits,period=p))
            for k,bit in enumerate(bits):
                edges[direction].append((t+k*p,1-bit))
            # Electrical stimulus mirrors production's additional8.5us finalstop.
            # This is not the driver integration: that separately runs Driver.
            t+=10*p+(8.5e-6 if direction=='tx' else 0)
        edges[direction].append((t,0))
    return frames,edges,t+.0004

def pwl(events,high=5):
    out=['0 0']; last=0
    for t,value in events:
        if t<=0 or value==last: continue
        out += [f'{t:.12g} {last*high:.12g}',f'{t+30e-9:.12g} {value*high:.12g}']
        last=value
    return 'PWL('+ ' '.join(out)+')'

def netlist(case,folder):
    d=case; frames=[]; stop=.107
    cir=(ROOT/'simulation/frontend.cir').read_text()
    for ref,scale in [('R1',d['rt']),('R2',d['rb']),('R3',d['rf']),('R5',d['refscale'])]:
        cir=re.sub(rf'(?m)^({ref} \S+ \S+ )([\deE.+-]+)$',lambda m:m[1]+str(float(m[2])*scale),cir)
    (folder/'frontend.cir').write_text(cir)
    if d['kind']=='bytes':
        frames,edges,stop=byte_stream(d['full'],d['period'],d.get('all_values',False),d.get('byte_block',0))
        tx=pwl(edges['tx'],d['usb']); peer=pwl(edges['rx'])
    else:
        tx=pwl([(0.,0.),(.100,float(d.get('tx',0)))],d['usb']); peer='0'
    battery=str(d['battery']); usb=str(d['usb'])
    if d['kind']=='sequence':
        stop=.200
        # USB precedes field; field loss during LOW, then USB loss; both recover.
        usb='PWL(0 0 1m 0 1.01m 5 108m 5 108.01m 0 123m 0 123.01m 5)'
        battery='PWL(0 0 2m 0 2.1m 9 105m 9 105.1m 0 120m 0 120.1m 9)'
        tx='PWL(0 0 100m 0 100.001m 5)'
    if d['kind']=='loss':
        stop=.140
        # Keep TX asserted after rail loss; do not hide it with a release command.
        tx='PWL(0 0 100m 0 100.001m 5)'
        if d['loss']=='field':battery='PWL(0 9 100.2m 9 100.3m 0)'
        else:usb='PWL(0 5 100.2m 5 100.3m 0)'
    base=[f'M3b model {d["name"]}',f'.include "{(ROOT/"simulation/models.cir").as_posix()}"',
          '.include "frontend.cir"',f'.param CMP_DELAY={d["delay"]} CMP_OFFSET={d["offset"]} RX_LEAK={d["rx_leak"]} TX_LEAK={d["tx_leak"]} FIELD_SET={d["field"]}',
          f'.param ISO_DELAY={d["iso_delay"]}',f'.temp {d["ambient"]}',
          f'.param TIMER_SCALE={d["timer_scale"]} TIMER_BYPASS={d["timer_bypass"]} ARM_UNSAFE={d["arm_unsafe"]} GATE_REVERSED={d["gate_reversed"]} PG_BYPASS={d["pg_bypass"]} PG_DELAY={d["pg_delay"]} PG_THRESHOLD={d["pg_threshold"]} LOW_POWER_TX={d["low_power_tx"]}',
          f'Vg gl 0 {d["ground"]}',f'Vusb usb gl {usb}',f'Vbat bat 0 {battery}',
          f'Vcommand command gl {tx}',
          'Btx tx gl V=min(max(V(command,gl),0),max(V(usb,gl),0))',f'Vpeer peer 0 {peer}',
          'Vbutton button 0 PWL(0 0 65m 0 65.001m 5.25 90m 5.25 90.001m 0)',
          'Barm arm 0 V=min(max(V(button),0),max(V(xfe.v5_f),0))',
          'Xfe usb gl tx rx bus 0 bat arm frontend',
          f'Vpu pu 0 {d["vpu"]}',f'Rpu pu bus {d["rpu"]}',
          # Lumped total line capacitance includes Q1 Cgd=50p. All other
          # line capacitances/fixtures/cable/probes must fit remaining budget.
          f'Cline bus 0 {max(d["cbus"]-50e-12,1e-12)}',
          '.model PEER_SW SW(Ron=5 Roff=1e12 Vt=2.5 Vh=0)',
          'Speak bus 0 peer 0 PEER_SW']
    if d['kind'] in ('fault','ramp'):
        voltage=d['fault_v']
        if d['kind']=='ramp':
            stop=.107
            voltage='PWL(0 0 100m 0 102m 5.25 103m 5.25 105m 0 107m 0)'
        if d.get('pulse'):
            voltage=f'PULSE(0 {voltage} 100m 1u 1u 1m 10m)'
        base += [f'Vfault fault 0 {voltage}',f'Rfault fault bus {d["rs"]}']
    if d.get('clamp_removed'):
        cir=re.sub(r'(?m)^D2 .*$', '* negative-control: D2 omitted', cir)
        (folder/'frontend.cir').write_text(cir)
    vectors=['v(bus)','v(rx,gl)','v(tx,gl)','v(xfe.sense)','v(xfe.gate)',
             'v(xfe.drain)','v(xfe.v5_f)','v(xfe.rx_f)','v(xfe.vref)','v(peer)','v(usb,gl)']
    vectors += ['v(xfe.pg_f)','v(xfe.armed)','v(xfe.timeout)','v(xfe.arm_clk)',
                'v(xfe.tx_f)','v(xfe.pg_gate)','v(xfe.usb_bad)','v(xfe.clr_n)','v(xfe.rearm_ready)',
                'v(xfe.v5_l,gl)','v(xfe.iso_feed,gl)',
                'v(xfe.tx_l_iso,gl)','v(xfe.rx_l_iso,gl)']
    save_start=0 if d['kind'] in ('loss','sequence') else .095
    base += ['.options reltol=1e-4 abstol=1e-10 vntol=1e-6', '.save '+ ' '.join(vectors),
             '.control','set wr_singlescale','set wr_vecnames','set numdgt=12',
             f'tran .25u {stop:.12g} {save_start:.12g} .25u uic', 'wrdata trace.dat '+ ' '.join(vectors),
             'quit','.endc','.end']
    (folder/'scenario.cir').write_text('\n'.join(base)+'\n')
    return frames,stop

def read_trace(path):
    columns=None
    with path.open() as f:
        header=next(f).split(); columns=[[] for _ in header]
        for line in f:
            vals=[float(x) for x in line.split()]
            if len(vals)!=len(columns) or not all(math.isfinite(v) for v in vals):
                raise ValueError('Missing or non-finite SPICE samples')
            for col,v in zip(columns,vals): col.append(v)
    assert len(columns[0])>20
    return columns

def run_spice(folder,engine,timeout=240):
    log_path=folder/'engine.log'
    binary=Path(shutil.which(engine) or engine).resolve()
    module=binary.parent.parent/'lib/x86_64-linux-gnu/ngspice/analog.cm'
    if not module.is_file():
        raise RuntimeError(f'Pinned ngspice42 analog code module missing: {module}')
    init=folder/'spice-init';init.mkdir(exist_ok=True)
    # codemodel retains quote characters in ngspice42. A relative copied test
    # module also permits a workspace path containing spaces, without quoting.
    shutil.copyfile(module,init/'analog.cm')
    (init/'spinit').write_text('codemodel ./spice-init/analog.cm\n',encoding='utf-8')
    spice_env=os.environ.copy();spice_env['SPICE_SCRIPTS']=str(init)
    (folder/'code-module.json').write_text(json.dumps({'engine':'ngspice42',
        'module':str(module),'sha256':hashlib.sha256(module.read_bytes()).hexdigest(),
        'buffer_size':8192},indent=2)+'\n',encoding='utf-8')
    with log_path.open('w',encoding='utf-8') as output:
        try:
            result=subprocess.run([engine,'-b','scenario.cir'],cwd=folder,
                                  stdout=output,stderr=subprocess.STDOUT,timeout=timeout,env=spice_env)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(f'Simulator infrastructure timeout; never expected rejection: {log_path}') from error
    log=log_path.read_text(encoding='utf-8',errors='replace')
    if result.returncode!=0 or re.search(r'timestep too small|fatal error|doAnalyses:|Error on line',log,re.I):
        raise RuntimeError(f'Simulator infrastructure failure; never expected rejection: {log_path}')
    return log

def at(trace,t,col):
    ts=trace[0]; i=bisect_left(ts,t)
    if i==0:return trace[col][0]
    if i>=len(ts):return trace[col][-1]
    f=(t-ts[i-1])/(ts[i]-ts[i-1])
    return trace[col][i-1]+f*(trace[col][i]-trace[col][i-1])

def crossing(trace,start,end,col,threshold,rising):
    a=bisect_left(trace[0],start); b=bisect_left(trace[0],end)
    for i in range(max(a,1),min(b+1,len(trace[0]))):
        before,after=trace[col][i-1],trace[col][i]
        if (before<threshold<=after) if rising else (before>threshold>=after):
            return trace[0][i-1]+(threshold-before)*(trace[0][i]-trace[0][i-1])/(after-before)
    return None

def assess(d,trace,frames):
    checks=[]
    def ck(name,value,limit,relation,unit=''):
        margin=limit-value if relation=='<=' else value-limit
        checks.append(dict(check=name,value=value,limit=limit,relation=relation,unit=unit,
                           margin=margin,status='PASS' if margin>=-1e-12 else 'FAIL'))
    names=['time','bus','rx','tx','sense','gate','drain','field_rail','comparator','vref','peer','usb_rail',
           'field_pg','armed','timeout','arm_clk','tx_isolated','pg_gate','usb_bad','clear_n','rearm_ready',
           'held_logic_rail','hold_feed','ISO_logic_TX_input','ISO_logic_RX_output']
    extrema={n:{'min':min(v),'max':max(v)} for n,v in zip(names,trace) if n!='time'}
    ck('isolator_logic_input_above_held_rail',max(t-u for t,u in zip(trace[23],trace[21])),.3,'<=','V')
    ck('raw_D3_guard_input_abs_max',max(trace[3]),5.5,'<=','V')
    ck('raw_RX_guard_input_abs_max',max(trace[24]),5.5,'<=','V')
    if d['kind']=='bytes':
        errors=0; missing_edges=0; delays=[]; bus_low=[]; bus_high=[]; rise_times=[]; fall_times=[]
        for frame in frames:
            start=frame['t']; p=frame['period']; bits=frame['bits']
            capture=crossing(trace,start,start+30e-6,2,d['usb']*.5,False)
            if capture is None: missing_edges+=1; capture=start
            # Match existing capture-relative RX and TX echo centres. Receiver
            # time is 104us; sweep 0/20us lateness AND +/-2us edge jitter.
            for late,jitter in itertools.product([0.,20e-6],[-2e-6,2e-6]):
                for k,bit in enumerate(bits):
                    t=(start if frame['direction']=='tx' else capture)+(k+.5)*104e-6+late+jitter
                    value=at(trace,t,2)
                    if (value>=.6*d['usb']) if bit else (value<=.3*d['usb']): pass
                    else: errors+=1
            last=1
            for k,bit in enumerate(bits):
                t=start+k*p
                bus=at(trace,t+.7*p,1)
                (bus_high if bit else bus_low).append(bus)
                if bit!=last:
                    found=crossing(trace,t,t+40e-6,2,.5*d['usb'],bool(bit))
                    if found is None:missing_edges+=1
                    else:delays.append(found-t)
                    low_cross=crossing(trace,t,t+50e-6,1,.1*d['vpu'],bool(bit))
                    high_cross=crossing(trace,t,t+50e-6,1,.9*d['vpu'],bool(bit))
                    # Some loaded idle levels are below .9*Vpu; report that
                    # explicitly instead of claiming a fabricated rise time.
                    if low_cross is not None and high_cross is not None:
                        (rise_times if bit else fall_times).append(abs(high_cross-low_cross))
                last=bit
        ck('sample_errors_all_centres',errors,0,'<=','count')
        ck('missing_logic_edges',missing_edges,0,'<=','count')
        ck('echo_or_receive_delay_max',max(delays,default=1),8e-6,'<=','s')
        ck('bus_low_max',max(bus_low),.6,'<=','V')
        ck('bus_high_min',min(bus_high),3.3,'>=','V')
        extrema['edge_delay']={'min':min(delays,default=0),'max':max(delays,default=0)}
        extrema['rise_10_90']={'max':max(rise_times,default=None),'count':len(rise_times),'reference':'unloaded Vpu; absent crossing is not zero time'}
        extrema['fall_90_10']={'max':max(fall_times,default=None),'count':len(fall_times)}
        ck('rx_input_abs_min',min(trace[4]),-.3,'>=','V')
        ck('rx_input_abs_max',max(trace[4]),38,'<=','V')
    elif d['kind']=='ramp':
        up=crossing(trace,.100,.102,2,.5*d['usb'],True)
        down=crossing(trace,.103,.105,2,.5*d['usb'],False)
        ck('both_threshold_crossings',int(up is not None)+int(down is not None),2,'>=','count')
        if up is not None and down is not None:
            vu=at(trace,up,1); vd=at(trace,down,1)
            ck('rising_threshold_max',vu,3.3,'<=','V'); ck('falling_threshold_min',vd,.7,'>=','V')
            ck('hysteresis_min',vu-vd,.8,'>=','V')
            extrema['thresholds']={'rising':vu,'falling':vd}
        # Actual DC line load from the voltage source, including all modeled paths.
        load=abs((5.25-at(trace,.1029,1))/d['rs'])
        ck('idle_load_max',load,250e-6,'<=','A')
    elif d['kind']=='fault':
        ck('rx_input_abs_min',min(trace[4]),-.3,'>=','V')
        ck('rx_input_abs_max',max(trace[4]),38,'<=','V')
        ck('mosfet_Vds_max',max(trace[6]),48,'<=','V')
        ck('mosfet_Vds_min',min(trace[6]),-1.2,'>=','V')
        current=[abs((b-dr)/44) for b,dr in zip(trace[1],trace[6])]
        ck('tx_leg_peak_current',max(current),.4,'<=','A')
        ck('each_R22_peak_power',max(i*i*22 for i in current),4.2,'<=','W')
        if d['battery']==0:
            ck('unpowered_field_rail',max(trace[7]),.2,'<=','V')
            ck('unpowered_gate_peak',max(trace[5]),1.3,'<=','V')
        if d['usb']==0:
            ck('usb_off_TX_release',max(trace[5]),1.3,'<=','V')
        ck('USB_RX_abs_min',min(trace[2]),-.3,'>=','V')
        ck('USB_RX_abs_max',max(v-u for v,u in zip(trace[2],trace[11])),.3,'<=','V above local rail')
        if d.get('target_off'):
            ck('injection_into_target_off',abs(trace[1][-1]/d['rs']),25e-6,'<=','A')
        # These are simulated finite fault sources. Energy integral includes
        # only modeled 3ms/1ms, NOT the physical 10s DC validation duration.
        energy=sum(current[i]**2*44*(trace[0][i]-trace[0][i-1]) for i in range(1,len(current)))
        extrema['tx_resistors_energy_J']={'integral':energy}
    elif d['kind']=='loss':
        if d['loss']=='field':
            collapsed=crossing(trace,.1003,.139,7,.8,False)
            ck('field_collapsed',int(collapsed is not None),1,'>=','count')
            if collapsed is not None:
                ck('gate_released_after_field_collapse',at(trace,collapsed+.001,5),1.3,'<=','V')
        else:
            ck('gate_released_after_USB_loss',at(trace,.1013,5),1.3,'<=','V')
        ck('gate_remains_released',max(trace[5][bisect_left(trace[0],.139):]),1.3,'<=','V')
    else:
        ck('field_restored',at(trace,.199,7),4.5,'>=','V')
        ck('field_loss_gate_released',at(trace,.111,5),1.3,'<=','V')
        ck('USB_loss_gate_released',at(trace,.114,5),1.3,'<=','V')
        ck('restored_RX_idle',at(trace,.199,2),3,'>=','V')
        ck('gate_abs_max',max(trace[5]),6,'<=','V')
    failed=[c['check'] for c in checks if c['status']=='FAIL']
    status='PASS' if not failed else 'FAIL'
    if d['expected']=='REJECT':status='EXPECTED_REJECTION' if failed else 'UNEXPECTED_PASS'
    protection_open=(d['kind'] in ('loss','sequence') or d['battery']==0 or d['usb']==0 or
                     (d['kind']=='fault' and d.get('fault_v',0)<0))
    return {'scenario':d,'status':status,'model_status':status,
            'protection_status':'OPEN' if protection_open else 'NOT VERIFIED',
            'checks':checks,'failed':failed,'extrema':extrema,
            'frames':len(frames),'physical_status':'NOT VERIFIED'}

def export_driver_trace(folder,d,trace,frames):
    """Extract ADC-independent digital bounds and causally split edge timestamps.

    The actual production Driver consumes these transitions in its separate C++
    harness. This exporter never decodes UART bytes.
    """
    name=d['name']; folder.mkdir(parents=True,exist_ok=True)
    events=[]; level=trace[2][0]>=.6*trace[11][0]
    valid=trace[11][0]>=2.25 and trace[7][0]>=2.25
    events.append((trace[0][0],int(level),int(valid)))
    for i in range(1,len(trace[0])):
        before,after=trace[2][i-1],trace[2][i]
        newvalid=trace[11][i]>=2.25 and trace[7][i]>=2.25
        boundary=(.3 if level else .6)*trace[11][i]
        change=(after<=boundary<before) if level else (before<boundary<=after)
        if change:
            t=trace[0][i-1]+(boundary-before)*(trace[0][i]-trace[0][i-1])/(after-before)
            level=not level; events.append((t,int(level),int(newvalid)))
        elif valid!=newvalid:
            events.append((trace[0][i],int(level),int(newvalid)))
        valid=newvalid
    with (folder/f'{name}-transitions.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.writer(f); w.writerow(['time_s','level','valid']); w.writerows(events)
    with (folder/f'{name}-frames.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.writer(f); w.writerow(['direction','start_s','period_s','value'])
        w.writerows((x['direction'],x['t'],x['period'],x['value']) for x in frames)
    manifest=folder/'manifest.csv'
    with manifest.open('a',newline='',encoding='utf-8') as f:
        w=csv.writer(f)
        if f.tell()==0:w.writerow(['name','transitions','frames','expected'])
        w.writerow([name,f'{name}-transitions.csv',f'{name}-frames.csv','PASS'])
    pairs={False:[],True:[]}
    edge_rows=[]
    for frame in frames:
        if frame['direction']!='tx':continue
        last=1
        for k,bit in enumerate(frame['bits']):
            if bit!=last:
                start=frame['t']+k*frame['period']; rising=bool(bit)
                # Same3.3V threshold for both DATA polarities; stop width is not
                # inflated by mixing HIGH and LOW acceptance thresholds.
                data=crossing(trace,start,start+40e-6,1,3.3,rising)
                rx=crossing(trace,start,start+40e-6,2,(.6 if rising else .3)*d['usb'],rising)
                if data is None or rx is None:raise ValueError('Missing analog edge in a passing case')
                tx_delay=data-start; rx_delay=rx-data; echo=rx-start
                if min(tx_delay,rx_delay)<0:raise ValueError('Noncausal split; inspect threshold choices')
                pairs[rising].append((tx_delay,rx_delay,echo))
                edge_rows.append([name,'rise' if rising else 'fall',start,data,rx,tx_delay,rx_delay,echo])
            last=bit
    if all(pairs.values()):
        # Select each largest echo's own TX/RX pair, never sum independent maxima.
        fall=max(pairs[False],key=lambda x:x[2]); rise=max(pairs[True],key=lambda x:x[2])
        with (folder/'corners.csv').open('a',newline='',encoding='utf-8') as f:
            w=csv.writer(f)
            if f.tell()==0:w.writerow(['name','tx_fall_us','tx_rise_us','rx_fall_us','rx_rise_us','echo_fall_us','echo_rise_us'])
            w.writerow([name,fall[0]*1e6,rise[0]*1e6,fall[1]*1e6,rise[1]*1e6,fall[2]*1e6,rise[2]*1e6])
    with (folder/'edge-paths.csv').open('a',newline='',encoding='utf-8') as f:
        w=csv.writer(f)
        if f.tell()==0:w.writerow(['case','polarity','d3_s','data_s','d8_s','d3_data_s','data_d8_s','echo_s'])
        w.writerows(edge_rows)

def cases():
    out=[]
    for r,c,v,corner in itertools.product([1000,2200],[200e-12,500e-12],[4.75,5.25],[0,1]):
        out.append(default_case(f'line_{r}_{c:g}_{v}_{corner}',rpu=r,cbus=c,vpu=v,
            field=4.75 if corner==0 else 5.25,usb=4.75 if corner==0 else 5.25,
            delay=.3e-6 if corner==0 else 3e-6,offset=-.004 if corner==0 else .004,
            rx_leak=-50e-9 if corner==0 else 2.05e-6,tx_leak=0 if corner==0 else 200e-6,
            rt=.99 if corner==0 else 1.01,rb=1.01 if corner==0 else .99,
            rf=1.01 if corner==0 else .99,refscale=.99 if corner==0 else 1.01))
    for period in [102e-6,106e-6]:
        for ground in [-1,1]:
            out.append(default_case(f'full_{period:g}_{ground:+}',full=True,period=period,ground=ground))
    for block in range(8):
        out.append(default_case(f'all_256_block_{block}',all_values=True,byte_block=block))
    out.append(default_case('bad_short_cutoff_00',timer_scale=.25,expected='REJECT'))
    out.append(default_case('bad_slow_comparator',delay=60e-6,expected='REJECT'))
    out.append(default_case('bad_removed_negative_clamp',kind='fault',fault_v=-16,rs=.1,
                            rpu=1e12,vpu=0,clamp_removed=True,expected='REJECT'))
    for temp,corner in itertools.product([15,35],[0,1]):
        out.append(default_case(f'thermal_supply_{temp}_{corner}',ambient=temp,
            field=4.75 if corner==0 else 5.25,usb=5.25 if corner==0 else 4.75,
            iso_delay=6e-9 if corner==0 else 17e-9))
        out.append(default_case(f'threshold_ramp_{temp}_{corner}',kind='ramp',ambient=temp,
            fault_v=0,rs=.1,rpu=1e12,vpu=0,field=4.75 if corner==0 else 5.25,
            offset=-.004 if corner==0 else .004,rx_leak=-50e-9 if corner==0 else 2.05e-6))
    for v,tx in itertools.product([-16,0,12,16],[0,1]):
        out.append(default_case(f'fault_{v}_{tx}',kind='fault',fault_v=v,tx=tx,rs=.1,rpu=1e12,vpu=0))
    for usb,battery in [(0,9),(5,0),(0,0)]:
        for v in [-16,16]:
            out.append(default_case(f'off_{usb}_{battery}_{v}',kind='fault',fault_v=v,tx=1,rs=.1,
                usb=usb,battery=battery,rpu=1e12,vpu=0))
    for v in [-24,24]:
        out.append(default_case(f'pulse_{v}',kind='fault',fault_v=v,tx=0,rs=1000,pulse=True,rpu=1e12,vpu=0))
        out.append(default_case(f'pulse_off_{v}',kind='fault',fault_v=v,tx=1,rs=1000,pulse=True,
            rpu=1e12,vpu=0,battery=0))
    out.append(default_case('target_off',kind='fault',fault_v=0,rs=1000,rpu=1e12,vpu=0,target_off=True))
    out.append(default_case('open_data',kind='fault',fault_v=0,rs=1e12,rpu=1e12,vpu=0))
    out.append(default_case('stuck_sink',kind='fault',fault_v=0,rs=1e12,rpu=1000,vpu=5.25,tx=1))
    # Unsupported direct 48V active sink must violate passive resistor/current limits.
    out.append(default_case('bad_hard48_active',kind='fault',fault_v=48,tx=1,rs=.1,rpu=1e12,vpu=0,expected='REJECT'))
    out.append(default_case('bad_strong_pullup',rpu=100,expected='REJECT'))
    out.append(default_case('bad_slow_line',rpu=47000,cbus=10e-9,expected='REJECT'))
    out.append(default_case('power_sequence',kind='sequence'))
    for domain in ['field','usb']:
        out.append(default_case(f'power_{domain}_loss',kind='loss',loss=domain))
    return out

def write_reports(out,report):
    (out/'summary.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    with (out/'checks.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.writer(f); w.writerow(['scenario','expected','check','value','relation','limit','unit','margin','status'])
        for r in report['simulations']:
            for c in r['checks']:
                w.writerow([r['scenario']['name'],r['scenario']['expected'],c['check'],c['value'],c['relation'],c['limit'],c['unit'],c['margin'],c['status']])
    with (out/'calculations.csv').open('w',newline='',encoding='utf-8') as f:
        rows=report['calculations']['rows']; w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    lines=['# M3b electrical report','',f"Result: **{report['status']}**. Physical hardware: **NOT VERIFIED**.",'',
           f"Engine: {report['engine']}. Topology: {report['topology']['status']}.",'',
           f"Calculation checks: {len(report['calculations']['rows'])}. Scenarios: {len(report['simulations'])}.",'',
           '| Scenario | Status | Failed numerical criteria |','|---|---|---|']
    lines += [f"| {r['scenario']['name']} | {r['status']} | {', '.join(r['failed']) or 'none'} |" for r in report['simulations']]
    lines += ['', 'Behavioral parameter allocations remain physical acceptance gates; see MODEL_LIMITS.md.',
              'No physical USB, GPIO, thermal, isolation, EMC, ESD or ECU evidence is produced by this run.','']
    (out/'SUMMARY.md').write_text('\n'.join(lines))

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--ngspice',default='ngspice'); ap.add_argument('--out',type=Path,default=Path('build/electrical'))
    ap.add_argument('--calculations-only',action='store_true'); ap.add_argument('--bad-only',action='store_true')
    ap.add_argument('--scenario'); ap.add_argument('--keep-traces',action='store_true'); args=ap.parse_args()
    out=args.out.resolve(); out.mkdir(parents=True,exist_ok=True)
    driver_traces=out/'driver-traces'
    driver_traces.mkdir(exist_ok=True)
    for file in ['manifest.csv','corners.csv','edge-paths.csv']:
        (driver_traces/file).write_text('')
    (out/'summary.json').write_text(json.dumps({'status':'RUNNING','complete':False})+'\n')
    (out/'SUMMARY.md').write_text('# Incomplete electrical run\n\nRUNNING; no completed result yet.\n')
    git=subprocess.run(['git','rev-parse','HEAD'],cwd=ROOT,capture_output=True,text=True)
    report={'topology':topology_checks(),'calculations':arithmetic.calculate(),'simulations':[],
            'engine':'not run','status':'RUNNING','complete':False,'physical_status':'NOT VERIFIED',
            'created_utc':datetime.now(timezone.utc).isoformat(),'git_sha':git.stdout.strip(),
            'platform':platform.platform(),'python':platform.python_version(),
            'model_sha256':hashlib.sha256((ROOT/'simulation/models.cir').read_bytes()).hexdigest()}
    if any(c['status']=='FAIL' for c in report['calculations']['rows']): report['status']='FAIL'
    if not args.calculations_only:
        ver=subprocess.run([args.ngspice,'--version'],capture_output=True,text=True,check=True)
        assert re.search(r'ngspice-42\s',ver.stdout), 'Exactly ngspice 42 required'
        report['engine']='ngspice 42'; (out/'engine.txt').write_text(ver.stdout+ver.stderr)
        all_cases=cases()
        if args.bad_only:all_cases=[c for c in all_cases if c['expected']=='REJECT']
        if args.scenario:all_cases=[c for c in all_cases if c['name']==args.scenario]
        assert all_cases,'No scenarios selected'
        for d in all_cases:
            folder=out/d['name']; folder.mkdir(exist_ok=True)
            frames,stop=netlist(d,folder)
            d['simulated_duration_s']=stop
            d['fixture_current_limit_a']=(None if d['expected']=='REJECT' else
                .025 if d.get('pulse') else .5 if d['kind']=='fault' else .01)
            d['source_model']='ideal voltage through stated resistance; fixture limit is not a simulated clamp'
            run_spice(folder,args.ngspice)
            if not (folder/'trace.dat').exists():raise RuntimeError('Missing trace is infrastructure, not expected rejection')
            trace=read_trace(folder/'trace.dat')
            if trace[0][-1]<stop-1e-9:raise RuntimeError(f"{d['name']}: truncated transient")
            r=assess(d,trace,frames)
            report['simulations'].append(r)
            if r['status'] in ('FAIL','UNEXPECTED_PASS'): report['status']='FAIL'
            if d['kind']=='bytes' and r['status']=='PASS':export_driver_trace(driver_traces,d,trace,frames)
            write_reports(out,report)
            print(d['name'],r['status'],','.join(r['failed']),flush=True)
            if not args.keep_traces:(folder/'trace.dat').unlink()
    if report['status']!='FAIL':report['status']='PASS'
    report['complete']=True
    report['revision']='B'
    report['protection_status']='OPEN'
    report['protection_reason']='Negative DC, off-state and production hardware survival unresolved; model PASS does not qualify protection.'
    write_reports(out,report)
    print(report['status'],out/'summary.json',flush=True)
    if report['status']!='PASS':return 1
    return 2 if args.bad_only else 0

if __name__=='__main__':
    sys.exit(main())
