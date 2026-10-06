"""Reproducible bounded engineering arithmetic; all SI, Python standard library."""
import itertools
import math

def calculate():
    rows=[]
    def add(name,value,limit,relation,unit,basis):
        margin=limit-value if relation=='<=' else value-limit
        rows.append(dict(check=name,value=value,limit=limit,relation=relation,unit=unit,
                         margin=margin,status='PASS' if margin>=0 else 'FAIL',basis=basis))
    # Enumerate independent resistor/supply/offset/output/leakage endpoints.
    rising=[]; falling=[]; hysteresis=[]
    for vf,rt,rb,rf,r4,r5,offset,bias,leak in itertools.product(
            [4.75,5.25],[.9895,1.0105],[.9895,1.0105],[.9895,1.0105],
            [.9895,1.0105],[.9895,1.0105],[-.004,.004],[-50e-9,50e-9],[0,2e-6]):
        a,b,f=220e3*rt,68e3*rb,1e6*rf
        vr=vf*(10e3*r5)/(78.7e3*r4+10e3*r5)+offset
        # Comparator output high includes pullup loading; conservative loss .04V.
        high=vf-.04
        for low in [0,.55]:
            up=vr*(1+a/b+a/f)-low*a/f+(bias+leak)*a
            down=vr*(1+a/b+a/f)-high*a/f+(bias+leak)*a
            rising.append(up); falling.append(down); hysteresis.append(up-down)
    add('rising_threshold_max',max(rising),3.3,'<=','V','corner calculation; 2uA BAT54 leakage allocation; <=4mV offset assumption below 5V')
    add('falling_threshold_min',min(falling),.7,'>=','V','same independent corners')
    add('hysteresis_min',min(hysteresis),.8,'>=','V','same device corner; correlated high/low supply')
    rtmax=44*1.05; ron=.32+.5
    low=5.25*(rtmax+ron)/(1000+rtmax+ron)
    add('bus_low_max',low,.6,'<=','V','Rpull=1k; 2x22R +5%; Rds=.32R design allocation')
    idle=4.75-2200*250e-6
    add('idle_high_min',idle,max(rising),'>=','V','total leakage/load <=250uA is a measured acceptance requirement')
    add('idle_load_limit',250e-6,300e-6,'<=','A','includes <=200uA D1 + Q1 + RX network; 15-35C must be measured')
    rmin=44*.95
    fault_i=16/rmin
    add('fault_sink_current',fault_i,.4,'<=','A','16V ideal source; internal resistance only; source >=0R, limit 0.5A')
    # One resistor high, the other low maximizes individual dissipation near equality.
    p_each=max(16**2*a/(a+b)**2 for a,b in itertools.product([22*.95,22*1.05],repeat=2))
    add('each_tx_resistor_fault_power',p_each,4.2,'<=','W','50% of AC10 P70=8.4W; room ambient 15-35C; thermal measurement required')
    q_power=fault_i**2*.5
    add('mosfet_dc_fault_power',q_power,.15,'<=','W','Rds=.32R allocation; Q1/Q2 conservative individual max.5R allocation; standard-pad RthetaJA<=244K/W')
    add('mosfet_estimated_Tj',35+q_power*244,100,'<=','degC','temperature estimate, not thermal measurement')
    add('negative_D1_power',fault_i*.75,.4,'<=','W','conservative IF*VF; actual DC/temperature needs measurement')
    add('negative_input_current',16/(220e3*.99),100e-6,'<=','A','BAT54 240mV maximum is specified at 25C with short pulses only')
    add('rx_resistor_fault_power',16**2/(220e3*.99),.3,'<=','W','MRS25 derated project ceiling')
    add('field_regulator_input_min',8-.75-.02*47*1.05,6,'>=','V','battery >=8V measured under load; SS16 <=.75V allocation')
    add('battery_resistor_short_power',9.6**2/(47*.95),2.5,'<=','W','AC03 P70=2.5W; reverse diode + battery R ignored conservatively')
    add('ldo_power_max',(9.6-5)*.02,.15,'<=','W','20mA budget; no field power from USB or DLC')
    add('gate_high_min',4.75-.1-220*1.0105*(5.25/(68000*.9895)+1e-6),4.5,'>=','V','LVC VOH >=Vcc-.1 at <=100uA; static gate load')
    miller=24*50e-12/(2.2e-9*.95+200e-12+50e-12)
    off=10e-6*68000*1.01 + miller
    add('off_gate_with_24V_step',off,1.3,'<=','V','10uA buffer Ioff, Cgd<=50pF & Cgs>=200pF are model allocations; measure')
    release=68000*1.01*(2.2e-9*1.05+200e-12+50e-12)*math.log(5.25/1.3)
    add('power_off_release',release,1e-3,'<=','s','RC estimate excludes slow intermediate-rail behavior; scope power ramps')
    add('full_echo_allocation',8e-6,10.24e-6,'<=','s','52us -9.5*2.08us clock drift -20us ISR -2us edge jitter')
    # Timer IC full-temperature accuracy already includes supply/temperature
    # drift. Add external resistor tolerance/TCR and PCB SET leakage separately.
    timer_nom=512*(200e3/50e3)*1e-6
    resistor_error=.01+50e-6*10
    # Leakage changes SET current reciprocally; VSET minimum is .97V.
    timer_min=timer_nom*(1-resistor_error)*.97/(1+10e-9*200e3*(1-resistor_error)/.97)
    timer_max=timer_nom*(1+resistor_error)*1.03/(1-10e-9*200e3*(1+resistor_error)/.97)
    legal_low_compiled=9*104e-6/.98+20e-6/.98+131/(16e6*.98)+2/(16e6*.98)+.1e-6
    legal_low_sensitivity=9*104e-6/.98+20e-6/.98+16e-6/.98+2/(16e6*.98)+.1e-6
    legal_low=1.025e-3
    add('legal_low_conservative_envelope',max(legal_low_compiled,legal_low_sensitivity),legal_low,'<=','s','slow9bits+40tickISRlate+max131cyclecompiledSTOP/16usgenericPORTDtail+2cyclestamp+.1uswidth; no physical ISR proof')
    add('timer_min_legal_low',timer_min,legal_low,'>=','s','LTC6994NDIV512 fulltemp+/-3%;R+/-1%+TCR.05%;SETleak10nA allocation; fullproductionLOW1.025msenvelope')
    add('timer_max_own_sink_release',timer_max+1e-6,5e-3,'<=','s','1us NOR/clear/AND/buffer/gate allocation, not guaranteed timer tPD maximum')
    add('timer_reset_vs_stop',1e-6,104e-6/1.02,'<=','s','1us project measured-acceptance allocation; normal stop bit fastest+2%clock')
    debounce_nom=4096*(249e3/50e3)*1e-6
    debounce_min=debounce_nom*(1-resistor_error)*.97/(1+10e-9*249e3*(1-resistor_error)/.97)
    debounce_max=debounce_nom*(1+resistor_error)*1.03/(1-10e-9*249e3*(1+resistor_error)/.97)
    add('debounce_min_vs_bounce',debounce_min,5e-3,'>=','s','LTC6994-2 qualification; Omron5msmaxbounce; remove+press again for freshedge')
    # Actual PG adjustable comparator corners, including SENSE25nA and R TCR.
    pg_fall=[]; pg_rise=[]
    for top,bottom,vit,ib in itertools.product([.9895,1.0105],[.9895,1.0105],[.405*.98,.405*1.02],[-25e-9,25e-9]):
        rtop=100e3*top; rbottom=10.2e3*bottom
        falling_pg=vit*(1+rtop/rbottom)+ib*rtop
        pg_fall.append(falling_pg)
        pg_rise.append(vit*1.03*(1+rtop/rbottom)+ib*rtop)
    add('PG_rise_max_normal_min',max(pg_rise),4.75,'<=','V','TPS3808G01±2%;maxhyst3%;R±1%+.05%TCR;Ib±25nA')
    add('PG_fall_min_valid_logic',min(pg_fall),2.25,'>=','V','ISO/TIMERlowerrecommendedsupply; PG doesnotcertifynormal4.75Vrail')
    add('PG_small_supply_sink_budget',1.3/(150e3*.9895)+5e-6+1e-6+1.3/10e6,15e-6,'<=','A','TPS3808VPOR0.8V guarantee at15uA; raw PG fanout oneinput5uA+gate1uA')
    pg_static=4.75-150e3*1.0105*(5e-6+1e-6+.3e-6+4.75/10e6)
    add('PG_logic_high_min',pg_static,.7*4.75,'>=','V','Rpull150k; input5uA,gate1uA,RESETleak.3uA,pulldown10M; Q2Rds.5R remainsmeasuredallocation')
    add('gate_peak_current',5.25/(220*.9895),.032,'<=','A','bufferinternalresistance ignored; C10/Cgsstep; LVCrecommended32mA@4.5V')
    add('PG_reset_peak_current',5.25/(2200*.9895),.005,'<=','A','R24 and R36 limit supervisor capacitive discharge; TPSRESETabsolute5mA ceiling')
    gate_release=(220*1.0105+20)*(2.2e-9*1.05+200e-12+50e-12)*math.log(5.25/1.3)
    add('cutoff_gate_RC_release',gate_release+50e-9,1e-6,'<=','s','20Rbuffer+50nslogic allocation; capacitances allocated, scopeactualwaveform')
    pg_gate_release=(2200*1.0105+100)*(2.2e-9*1.05+200e-12+50e-12)*math.log(5.25/1.3)
    add('PG_gate_RC_release',pg_gate_release+50e-6,100e-6,'<=','s','100Rsupervisor dynamic allocation+50usassertion allocation; not DSguaranteedmax')
    added_field=.000525+.000006+10*.00001+5.25/10000+5.25/(357000+100000)+2*5.25/(255000+100000)+5.25/(100000+10200)+5.25/11000+5.25/150000
    original_field=.0125
    add('field_static_budget',original_field+added_field,.02,'<=','A','ISO7721maxDC3.4mA at5V,LM393B1.25mA allocation,bleed5.25mA,RXpullup2.42mA,VREF/gate/misc.18mA plus timers,latch/gates,opto collector,debouncebutton/dividers; switching measured separately')
    usb_led_min=(4.75-1.65)/(390*1.0105+.32)
    usb_led_max=(5.25-1)/(390*.9895)
    add('USB_health_LED_IF_min',usb_led_min,5e-3,'>=','A','VF<=1.65V allocation15–35C; exact IF/VF table at25C; Q3.32Rallocation')
    add('USB_health_LED_IF_max',usb_led_max,.02,'<=','A','VF>=1V sensitivityallocation; no claim acrossfulltemp')
    add('USB_health_CTR_sink',usb_led_min*.8,5.25/(10000*.9895),'>=','A','80%CTRmeasuredallocation15–35C; datasheet160%minimum only25C/5mA')
    # Logic hold-up: reverse charge before actual switch blocking is explicit.
    hold_current=.010
    hold_cap_min=4.7e-6
    hold_r_min=22*.95
    hold_r_max=22*1.05
    hold_ron=.14
    hold_start=4.75-hold_current*(hold_r_max+hold_ron)
    hold_reverse_peak=5.25/hold_r_min
    hold_block=15e-6
    hold_health=250e-6
    hold_v_min=hold_start-(hold_reverse_peak*hold_block+hold_current*hold_health)/hold_cap_min
    add('logic_island_normal_min',hold_start,4.5,'>=','V','steady10mA;22R5%+LM.14Rallocation; Cinitialstate/transienttreatedseparately')
    add('hold_feed_peak_current',hold_reverse_peak,1.5,'<=','A','R37 limits startup/reversecharging;LMcontinuouscurrentrating1.5A;peaknotcurrentlimiterinsideIC')
    add('hold_feed_peak_power',5.25**2/hold_r_min,2.5,'<=','W','VishayAC03 P70=2.5W; no need infer pulse overload capability of MRS25')
    add('island_valid_until_USB_inhibit',hold_v_min,2.25,'>=','V','Ceff>=4.7uF measured;15usblocking+250ushealth allocations; includesIpk*tOFFreversecharge and10mAload')
    add('raw_off_rail_with_bleed',25e-6*4700*1.0105,.2,'<=','V','25uA aggregateinjectionallocation;LMreverse2.7uA+bufferleaks;R38rawbleed4.7k; wholeoffstateOPEN')
    add('Nano_D8_off_with_pulldown',10e-6*10000*1.0105,.2,'<=','V','rawpoweredU24Ioff<=10uA plusR10; inputtorailinjectioncountedseparately')
    add('logic_bleed_draw',5.25/(4700*.9895),.002,'<=','A','R38 onlyRAWUSB/GND_L; field/islandcurrent budgets not combined')
    add('ISO_INB_guard_off_pulldown',20e-6*10000*1.0105,.3,'<=','V','R39 aggregateU23Ioff10uA+U1INBinput10uA allocation; inputoffstatewholepathmeasured')
    add('RX_ISO_guard_off_pulldown',20e-6*10000*1.0105,.3,'<=','V','R40 aggregate20uA ISOoutput/U24input allocation; ISOoffoutputnotmanufacturerIoffguarantee')
    add('D3_guard_high_to_ISO',3.8,.7*5.25,'>=','V','U23VOH>=3.8Vat4.5V/32mAcondition;10kpulldownload<.54mA; do notuse100uA VOHtable')
    add('RX_guard_input_high',4.5-.3,.7*5.25,'>=','V','U1VOH>=Vcc-.3at<=4mA;R40steadyload<.54mA;U24VIH<=.7Vcc')
    add('healthy_field_decay_vs_PG',2.2e-6*(4.2-2.25)/.020,100e-6,'>=','s','Ceff>=2.2uF field/max20mA gives>=214.5us; healthyload/batteryremoval only, hardcollapseOPEN')
    return {'rows':rows,'failsafe':{'timer_nominal_s':timer_nom,'timer_min_s':timer_min,'timer_max_s':timer_max,'own_sink_release_max_s':timer_max+1e-6,'legal_low_max_s':legal_low,'reset_max_allocation_s':1e-6,'debounce_min_s':debounce_min,'debounce_max_s':debounce_max,'ready_min_s':debounce_min,'ready_max_s':debounce_max,'PG_fall_min_V':min(pg_fall),'PG_fall_max_V':max(pg_fall),'PG_rise_max_V':max(pg_rise),'PG_delay_min_s':.012,'PG_delay_max_s':.028,'field_static_A':original_field+added_field,'USB_LED_IF_min_A':usb_led_min,'USB_LED_IF_max_A':usb_led_max,'hold_current_max_A':hold_current,'hold_cap_effective_min_F':hold_cap_min,'hold_normal_min_V':hold_start,'hold_reverse_peak_A':hold_reverse_peak,'hold_block_allocation_s':hold_block,'USB_health_allocation_s':hold_health,'hold_min_after_health_V':hold_v_min},'thresholds':{'rising_min':min(rising),'rising_max':max(rising),
             'falling_min':min(falling),'falling_max':max(falling),'hysteresis_min':min(hysteresis)},
            'physical_status':'NOT VERIFIED'}
