#!/usr/bin/env python3
"""Revision-B finite electrical/state checks. Never accepts simulator errors.

Expected rejections require failed numeric/logical criteria, never case names.
Analog waveforms and sources stay in build output. No hardware measurement.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import run as electrical
SHIFT=.035

def shifted(d):
    d=dict(d)
    for key in ['tx','button','usb','field']:
        text=d[key]
        if text.startswith('PWL('):
            tokens=text[4:-1].split()
            values=[(float(tokens[i])+(SHIFT if float(tokens[i])>0 else 0),float(tokens[i+1]))
                    for i in range(0,len(tokens),2)]
            d[key]=profile(values)
    d['stop']+=SHIFT;d['time_origin_shift_s']=SHIFT
    d['DATA_source_V']=5.25;d['DATA_source_R_ohm']=1000;d['DATA_fixture_limit_A']=.010
    d['DATA_capacitance_F']=500e-12
    d['power_source_model']='Prescribed ideal local rails; regulator/inrush/current-limiter dynamics not validated'
    return d

def profile(values):
    return 'PWL('+ ' '.join(f'{t:.12g} {v:.12g}' for t,v in values)+')'

def default(name,rule='cutoff',**kw):
    d=dict(name=name,rule=rule,expected='PASS',stop=.080,scale=1,ambient=25,
           tx=profile([(0,0),(.065,0),(.065001,5)]),
           button=profile([(0,0),(.030,0),(.030001,5),(.055,5),(.055001,0)]),
           usb='5',field='5',pg_threshold=.405,pg_delay=.028,opto_ctr=.8,opto_delay=50e-6,
           timer_bypass=0,arm_unsafe=0,gate_reversed=0,pg_bypass=0,low_power_rx=1,
           independent_tx=False,hold_cap=4.7e-6,hold_block_delay=15e-6,hold_bypass=0)
    d.update(kw);return d

def cases():
    out=[]
    timing=electrical.arithmetic.calculate()['failsafe']
    for scale in [timing['timer_min_s']/timing['timer_nominal_s'],1,
                  timing['timer_max_s']/timing['timer_nominal_s']]:
        for temp in [15,25,35]:
            out.append(default(f'held_high_{scale}_{temp}',scale=scale,ambient=temp))
    out += [
        default('latched_low_glitch',rule='locked',stop=.083,
                tx=profile([(0,0),(.065,0),(.065001,5),(.072,5),(.072001,0),(.072003,0),(.072004,5)])),
        default('explicit_rearm',rule='rearm',stop=.162,
                tx=profile([(0,0),(.065,0),(.065001,5),(.085,5),(.085001,0),(.151,0),(.151001,5),(.1515,5),(.151501,0)]),
                button=profile([(0,0),(.030,0),(.030001,5),(.055,5),(.055001,0),(.071,0),(.071001,5),(.0713,5),(.071301,0),(.073,0),(.073001,5),(.0733,5),(.073301,0),(.115,0),(.115001,5),(.145,5),(.145001,0)])),
        default('arm_while_active',rule='active_arm',tx=profile([(0,0),(.060,0),(.060001,5)]),
                button=profile([(0,0),(.040,0),(.040001,5),(.075,5),(.075001,0)])),
        default('held_button_no_rearm',rule='held_button',stop=.135,
                button=profile([(0,0),(.030,0),(.030001,5)]),
                tx=profile([(0,0),(.065,0),(.065001,5),(.080,5),(.080001,0),(.085,0),(.085001,5)])),
        default('power_on_high',rule='unarmed',tx='5',button='0'),
        default('power_on_low',rule='unarmed',tx='0',button='0'),
        default('button_held_at_startup',rule='unarmed',tx='0',button='5'),
        default('external_low',rule='external_low',peer_low=True),
        default('shorted_Q1',rule='shorted_q',q1_short=True),
        default('normal_00',rule='legal_low',tx=profile([(0,0),(.065,0),(.065001,5),(.0659562,5),(.0659563,0)])),
        default('longest_legal_production_low',rule='longest_legal',
                tx=profile([(0,0),(.065,0),(.065001,5),(.066025,5),(.066026,0)])),
        default('normal_back_to_back_00',rule='legal_repeat',stop=.073,
                tx=profile([(0,0),(.065,0),(.065001,5),(.065936,5),(.065937,0),(.066049,0),(.06605,5),(.066985,5),(.066986,0)])),
    ]
    for domain in ['field','usb']:
        for slope in [.0001,.001,.010]:
            dip=profile([(0,5),(.060,5),(.060+slope,1.9),(.080,1.9),(.080+slope,5)])
            out.append(default(f'{domain}_brownout_{slope}',rule='power',stop=.140,
                tx=profile([(0,0),(.072,0),(.072001,5)]),**{domain:dip}))
        slow=profile([(0,0),(.001,0),(.101,5)])
        out.append(default(f'{domain}_slow_rise_high',rule='unarmed',stop=.140,tx='5',button='0',**{domain:slow}))
        repeated=profile([(0,5),(.060,5),(.0601,1.9),(.080,1.9),(.0801,5),(.110,5),(.1101,1.9),(.130,1.9),(.1301,5)])
        out.append(default(f'{domain}_repeated_brownout',rule='power',stop=.190,tx=profile([(0,0),(.075,0),(.075001,5)]),**{domain:repeated}))
    for threshold,delay,ctr,storage in [(.405*.98,.012,.8,5e-6),(.405*1.02,.028,.8,200e-6),(.405,.020,.4,200e-6)]:
        out.append(default(f'health_sensitivity_{threshold}_{delay}_{ctr}_{storage}',
                           pg_threshold=threshold,pg_delay=delay,opto_ctr=ctr,opto_delay=storage))
    fast_dip=profile([(0,5),(.060,5),(.0601,1.9),(.080,1.9),(.0801,5)])
    for level in [0,5]:
        out.append(default(f'USB_loss_independent_D3_{level}_slow_health',rule='power_high' if level else 'power',
            stop=.140,tx=profile([(0,0),(.0599,0),(.059901,level)]),usb=fast_dip,
            independent_tx=True,opto_delay=200e-6,hold_cap=4.7e-6))
    out.append(default('field_loss_during_active_TX',rule='field_high',stop=.140,
        tx=profile([(0,0),(.0599,0),(.059901,5)]),field=fast_dip,independent_tx=True))
    for domain in ['field','usb']:
        # A held button spanning loss/restoration must not create a new arm edge.
        # Release for READY qualification, then a fresh debounced press is explicit.
        out.append(default(f'{domain}_held_button_power_cycle',rule='held_cycle',stop=.200,
            tx=profile([(0,0),(.107,0),(.107001,5),(.1075,5),(.107501,0),(.190,0),(.190001,5),(.1905,5),(.190501,0)]),
            button=profile([(0,0),(.030,0),(.030001,5),(.120,5),(.120001,0),(.155,0),(.155001,5),(.185,5),(.185001,0)]),
            independent_tx=True,opto_delay=200e-6,pg_delay=.012,**{domain:fast_dip}))
    out.append(default('bad_hold_island_bypassed',rule='power',stop=.140,tx='0',usb=fast_dip,
                       hold_bypass=1,opto_delay=200e-6,expected='REJECT'))
    fast_zero=profile([(0,5),(.060,5),(.0600001,0),(.080,0),(.0800001,5)])
    out.append(default('fast_USB_zero_minimum_hold',rule='power',stop=.140,tx='0',usb=fast_zero,
                       opto_delay=200e-6))
    out.append(default('bad_hold_blocking_100us',rule='power',stop=.140,tx='0',usb=fast_zero,
                       opto_delay=200e-6,hold_block_delay=100e-6,expected='REJECT'))
    out.append(default('bad_hold_effective_cap_470n',rule='power',stop=.140,tx='0',usb=fast_zero,
                       opto_delay=200e-6,hold_cap=470e-9,expected='REJECT'))
    held_field=next(x for x in out if x['name']=='field_held_button_power_cycle')
    out.append(dict(held_field,name='bad_held_button_arm_data_bypassed',arm_unsafe=1,expected='REJECT'))
    # Each mutation has a direct physical/logical oracle; no name-based rejection.
    out += [default('bad_timer_disabled',timer_bypass=1,expected='REJECT'),
            default('bad_missing_RSET',remove_rset=True,expected='REJECT'),
            default('bad_timer_too_short',rule='legal_low',scale=.25,expected='REJECT',
                tx=profile([(0,0),(.065,0),(.065001,5),(.0659562,5),(.0659563,0)])),
            default('bad_active_rearm',rule='active_arm',arm_unsafe=1,expected='REJECT',
                tx=profile([(0,0),(.060,0),(.060001,5)]),
                button=profile([(0,0),(.040,0),(.040001,5),(.075,5),(.075001,0)])),
            default('bad_gate_polarity',rule='unarmed',tx='0',button='0',gate_reversed=1,expected='REJECT'),
            default('bad_no_power_good',rule='invalid_gate',field=profile([(0,0),(.001,0),(.101,5)]),
                    tx='5',button='0',pg_bypass=1,stop=.140,expected='REJECT')]
    # Both possible sampled levels of the undefined ISO supply region are model
    # choices, never guaranteed correct pin logic. Own-channel inhibition must
    # hold for each; firmware has no validity feedback wire.
    out += [dict(x,name=x['name']+'_undefined_RX_LOW',low_power_rx=0)
            for x in list(out) if x['rule'] in ('power','power_high','field_high','held_cycle') and x['expected']=='PASS']
    return out

def prepare(d,folder):
    case=electrical.default_case(d['name'],kind='fault',fault_v=0,rs=1e12,rpu=1000,vpu=5.25,
        battery=9,field=5,usb=5,ambient=d['ambient'],timer_scale=d['scale'],timer_bypass=d['timer_bypass'],
        arm_unsafe=d['arm_unsafe'],gate_reversed=d['gate_reversed'],pg_bypass=d['pg_bypass'],
        pg_delay=d['pg_delay'],pg_threshold=d['pg_threshold'])
    electrical.netlist(case,folder)
    cir=folder/'frontend.cir'; source=cir.read_text(encoding='utf-8')
    # Independent regulated-field fixture tests supervisor logic and ramps. This
    # explicitly bypasses the illustrative regulator loop, not a safety component.
    source=re.sub(r'(?m)^XU4 .*$',f'Vfield_fixture V5_F GND_F {d["field"]}',source)
    if d.get('q1_short'):
        source=source.replace('.ends frontend','Rshort DRAIN SINK_RET .01\n.ends frontend')
    if d.get('remove_rset'):source=re.sub(r'(?m)^R15 .*$', '* deliberate RSET-open control',source)
    cir.write_text(source,encoding='utf-8')
    path=folder/'scenario.cir'; deck=path.read_text(encoding='utf-8')
    for element,value in [('Vcommand',d['tx']),('Vbutton',d['button']),('Vusb',d['usb'])]:
        deck=re.sub(rf'(?m)^({element} \S+ \S+) .*$',lambda m:m[1]+' '+value,deck)
    if d.get('peer_low'):deck=deck.replace('Vpeer peer 0 0','Vpeer peer 0 5')
    if d['independent_tx']:
        deck=deck.replace('Btx tx gl V=min(max(V(command,gl),0),max(V(usb,gl),0))','Btx tx gl V=V(command,gl)')
    deck=deck.replace('.control',f'.param OPTO_CTR={d["opto_ctr"]} OPTO_DELAY={d["opto_delay"]} LOW_POWER_RX={d["low_power_rx"]} HOLD_CAP={d["hold_cap"]} HOLD_BLOCK_DELAY={d["hold_block_delay"]} HOLD_BYPASS={d["hold_bypass"]}\n.control')
    # Every circuit still integrates from t=0. Store the complete relevant phase;
    # startup/slow-ramp oracles retain t=0, other events begin no earlier than90ms.
    save_start=0 if d['rule'] in ('unarmed','invalid_gate') else .090
    d['trace_save_start_s']=save_start
    deck=re.sub(r'(?m)^tran .*$',f'tran .5u {d["stop"]} {save_start} .5u uic',deck)
    path.write_text(deck,encoding='utf-8')

def assess(d,t):
    rows=[]
    def ck(name,value,limit,relation,unit='V'):
        margin=limit-value if relation=='<=' else value-limit
        rows.append(dict(check=name,value=value,limit=limit,relation=relation,margin=margin,unit=unit,
                         status='PASS' if margin>=-1e-9 else 'FAIL'))
    def sample(time,col):return electrical.at(t,time+SHIFT,col)
    def maximum(col,a,b):return max(v for time,v in zip(t[0],t[col]) if a+SHIFT<=time<=b+SHIFT)
    def minimum(col,a,b):return min(v for time,v in zip(t[0],t[col]) if a+SHIFT<=time<=b+SHIFT)
    end=d['stop']-SHIFT-.001
    rule=d['rule']
    if rule in ['cutoff','locked','held_button','external_low','shorted_q','rearm']:
        ck('arm_before_request',sample(.062,13),3,'>=')
        if rule!='shorted_q':ck('sink_enabled_at_legal_start',sample(.0652,5),3,'>=')
        released=electrical.crossing(t,.066+SHIFT,.071+SHIFT,5,1.3,False)
        ck('own_gate_release_edge',int(released is not None),1,'>=','count')
        if released is not None:
            ck('continuous_high_release_time',released-(.065001+SHIFT),.005,'<=','s')
            ck('release_within_selected_timer_plus_gate_allocation',released-(.065001+SHIFT),
               .002048*d['scale']+1e-6,'<=','s')
        ck('fault_latch_cleared',sample(.070,13),1.3,'<=')
        if rule=='rearm':
            ck('bounce_does_not_rearm',maximum(13,.072,.084),1.3,'<=')
            ck('new_inactive_press_rearms',sample(.148,13),3,'>=')
            ck('new_normal_pulse_enabled',sample(.1512,5),3,'>=')
        else:
            ck('no_automatic_rearm',maximum(13,.070,end),1.3,'<=')
            ck('gate_remains_off',maximum(5,.070,end),1.3,'<=')
        if rule=='external_low':
            ck('external_low_stays_low',sample(.075,1),.6,'<=')
        elif rule=='shorted_q':
            ck('shorted_Q_not_claimed_released',sample(.075,1),.6,'<=')
        else:ck('fixture_released',sample(.0705,1),3.3,'>=')
    elif rule in ['legal_low','legal_repeat','longest_legal']:
        end=.066020 if rule=='longest_legal' else .06593 if rule=='legal_low' else .06697
        ck('legal_00_low_survives',sample(end,5),3,'>=')
        ck('legal_00_does_not_latch',minimum(13,.064,.070),3,'>=')
        ck('timer_reset_after_stop',sample(.0675,14),1.3,'<=')
    elif rule=='active_arm':
        ck('active_request_cannot_arm',maximum(13,.060,.079),1.3,'<=')
        ck('active_request_cannot_drive',maximum(5,.060,.079),1.3,'<=')
    elif rule in ['power','power_high','field_high']:
        ck('unsafe_power_locks_enable',maximum(13,.110,end),1.3,'<=')
        ck('restore_HIGH_does_not_sink',maximum(5,.110,end),1.3,'<=')
        if rule=='power_high':
            # Check from actual sense-threshold crossing, throughout power loss,
            # not merely long after restoration. Worst modeled health:50+200us.
            lost=electrical.crossing(t,.059+SHIFT,.061+SHIFT,11,4.2,False)
            ck('raw_USB_loss_observed',int(lost is not None),1,'>=','count')
            if lost is not None:
                off=electrical.crossing(t,lost,lost+.00035,5,1.3,False)
                ck('own_gate_released_within_health_bound',int(off is not None),1,'>=','count')
                active_hold=[held for time,held,gate in zip(t[0],t[21],t[5]) if lost<=time<=lost+.00035 and gate>1.3]
                ck('held_ISO_supply_valid_until_inhibition',min(active_hold,default=5),2.25,'>=')
        elif rule=='field_high':
            ck('own_channel_active_before_field_loss',sample(.05995,5),3,'>=')
            lost=electrical.crossing(t,.059+SHIFT,.061+SHIFT,7,4.2,False)
            ck('field_loss_observed',int(lost is not None),1,'>=','count')
            if lost is not None:
                # Q2 may inhibit while Q1's gate retains charge. Own current,
                # not a forced DATA HIGH or Q1-gate-only predicate, is decisive.
                current=[max((bus-drain)/44,0) for time,bus,drain in zip(t[0],t[1],t[6])
                         if lost+.000100<=time<=d['stop']-.001]
                ck('own_positive_path_released_after_field_health_window',max(current),.0003,'<=','A')
    elif rule=='held_cycle':
        ck('armed_before_power_loss',sample(.055,13),3,'>=')
        ck('held_button_restore_does_not_rearm',maximum(13,.090,.145),1.3,'<=')
        ck('held_button_restore_gate_off',maximum(5,.090,.145),1.3,'<=')
        ck('qualified_new_press_rearms',sample(.188,13),3,'>=')
        ck('post_restore_legal_pulse_passes',sample(.1902,5),3,'>=')
    elif rule=='unarmed':
        armed_valid=[q for f,u,q,time in zip(t[7],t[11],t[13],t[0]) if f>=4.75 and u>=4.75 and time>=.030+SHIFT]
        gate_valid=[q for f,u,q,time in zip(t[7],t[11],t[5],t[0]) if f>=4.75 and u>=4.75 and time>=.030+SHIFT]
        ck('startup_no_press_remains_locked',max(armed_valid,default=0),1.3,'<=')
        ck('startup_no_press_gate_off',max(gate_valid,default=0),1.3,'<=')
    # Strongest unspecified low-power logic outputs are modeled HIGH. Independent
    # raw power gate must block before rails enter the undefined logical region.
    unsafe=sum(1 for f,gate,pg in zip(t[7],t[5],t[17]) if .8<=f<2.25 and gate>1.3 and pg>1.3)
    ck('invalid_power_cannot_enable_both_gates',unsafe,0,'<=','samples')
    # This whole-transient oracle caught the pre-hold32us USB brownout hole.
    # A pin settling interval is allowed after an actual LOW transition; steady
    # LOW through USB loss must never create a new own-sink pulse. External
    # peer/short current is not mistaken for our transistor's commanded channel.
    low_since=None;unrequested=[]
    for time,raw,field,request,gate,pg,bus,drain in zip(t[0],t[11],t[7],t[3],t[5],t[17],t[1],t[6]):
        if request<=.1:
            if low_since is None:low_since=time
        else:low_since=None
        if (low_since is not None and time-low_since>=2e-6 and gate>1.3 and pg>1.3):
            current=max((bus-drain)/44,0)
            if current>.0003:unrequested.append(current)
    ck('unrequested_own_sink_at_settled_D3_LOW',len(unrequested),0,'<=','samples')
    ck('actual_ISO_INB_above_held_supply',max(inp-rail for inp,rail in zip(t[23],t[21])),.3,'<=')
    ck('raw_D3_guard_input_abs_max',max(t[3]),5.5,'<=')
    ck('raw_RX_guard_input_abs_max',max(t[24]),5.5,'<=')
    ck('Q1_VGS_abs_min',min(g-s for g,s in zip(t[5],t[25])),-20,'>=')
    ck('Q1_VGS_abs_max',max(g-s for g,s in zip(t[5],t[25])),20,'<=')
    ck('U2_output_abs_min',min(t[26]),-.5,'>=')
    ck('U2_output_above_field_rail',max(pin-rail for pin,rail in zip(t[26],t[7])),.5,'<=')
    failed=[x['check'] for x in rows if x['status']=='FAIL']
    status='PASS' if not failed else 'FAIL'
    if d['expected']=='REJECT':status='EXPECTED_REJECTION' if failed else 'UNEXPECTED_PASS'
    return dict(scenario=d,model_status=status,status=status,checks=rows,failed=failed,
                physical_status='NOT VERIFIED',protection_status='OPEN')

def structural_controls():
    # Required references are a physical contract, independently enumerated here.
    required={'U5','U6','U7','U8','U9','U10','U11','U12','U13','U14','U15','U16',
              'U17','U18','U19','U20','U21','U22','U23','U24','R37','R38','R39','R40','C28','C29',
              'R15','R18','R21','R24','R25','R26','R28','R29','R30','R31','R32','Q2','SW1',
              'D4','D5','D6'}
    actual={p['ref'] for p in electrical.design.COMPONENTS}
    good=required<=actual
    omitted=actual-{'R15'}; missing=sorted(required-omitted)
    return [dict(scenario='actual_component_contract',status='PASS' if good else 'FAIL',
                 check='required_refs_present',value=len(required-actual),limit=0,margin=-len(required-actual)),
            dict(scenario='deliberately_omitted_RSET',status='EXPECTED_REJECTION' if missing else 'UNEXPECTED_PASS',
                 check='required_refs_present',value=len(missing),limit=0,margin=-len(missing),missing=missing)]

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--ngspice',default='ngspice'); ap.add_argument('--out',type=Path,default=Path('build/electrical/failsafe'))
    ap.add_argument('--scenario');ap.add_argument('--keep-traces',action='store_true')
    ap.add_argument('--shard-index',type=int,default=0);ap.add_argument('--shard-count',type=int,default=1)
    args=ap.parse_args()
    if not 0<=args.shard_index<args.shard_count:ap.error('invalid shard index/count')
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    electrical.topology_checks()
    report=dict(status='RUNNING',complete=False,revision='B',simulations=[],structural=structural_controls(),
                physical_status='NOT VERIFIED',protection_status='OPEN',
                git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=electrical.ROOT,text=True).strip(),
                model_sha256=hashlib.sha256((electrical.ROOT/'simulation/models.cir').read_bytes()).hexdigest())
    (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    selected=[x for x in cases() if not args.scenario or x['name']==args.scenario]; assert selected
    selected=selected[args.shard_index::args.shard_count];assert selected
    for original in selected:
        d=shifted(original)
        folder=out/d['name'];folder.mkdir(exist_ok=True);prepare(d,folder)
        electrical.run_spice(folder,args.ngspice)
        trace=electrical.read_trace(folder/'trace.dat')
        if trace[0][-1]<d['stop']-1e-9:raise RuntimeError('Incomplete transient, not expected rejection')
        r=assess(d,trace);report['simulations'].append(r)
        print(d['name'],r['status'],','.join(r['failed']),flush=True)
        (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        if not args.keep_traces:(folder/'trace.dat').unlink()
    report['complete']=True
    report['shard']={'index':args.shard_index,'count':args.shard_count}
    report['status']='PASS' if all(x['status'] in ('PASS','EXPECTED_REJECTION') for x in report['simulations']+report['structural']) else 'FAIL'
    (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    with (out/'checks.csv').open('w',encoding='utf-8',newline='') as f:
        writer=csv.writer(f);writer.writerow(['scenario','expected','check','value','relation','limit','margin','unit','status'])
        for r in report['simulations']:
            for c in r['checks']:writer.writerow([r['scenario']['name'],r['scenario']['expected'],c['check'],c['value'],c['relation'],c['limit'],c['margin'],c['unit'],c['status']])
    print(report['status'],out/'summary.json',flush=True)
    return 0 if report['status']=='PASS' else 1

if __name__=='__main__':sys.exit(main())
