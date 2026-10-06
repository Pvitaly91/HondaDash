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
            [4.75,5.25],[.99,1.01],[.99,1.01],[.99,1.01],
            [.99,1.01],[.99,1.01],[-.004,.004],[-50e-9,50e-9],[0,2e-6]):
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
    rtmax=44*1.05; ron=.32
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
    q_power=fault_i**2*ron
    add('mosfet_dc_fault_power',q_power,.15,'<=','W','Rds=.32R allocation; Q1 standard-pad RthetaJA<=244K/W')
    add('mosfet_estimated_Tj',35+q_power*244,100,'<=','degC','temperature estimate, not thermal measurement')
    add('negative_D1_power',fault_i*.75,.4,'<=','W','conservative IF*VF; actual DC/temperature needs measurement')
    add('negative_input_current',16/(220e3*.99),100e-6,'<=','A','BAT54 240mV maximum is specified at 25C with short pulses only')
    add('rx_resistor_fault_power',16**2/(220e3*.99),.3,'<=','W','MRS25 derated project ceiling')
    add('field_regulator_input_min',8-.75-.02*47*1.05,6,'>=','V','battery >=8V measured under load; SS16 <=.75V allocation')
    add('battery_resistor_short_power',9.6**2/(47*.95),2.5,'<=','W','AC03 P70=2.5W; reverse diode + battery R ignored conservatively')
    add('ldo_power_max',(9.6-5)*.02,.15,'<=','W','20mA budget; no field power from USB or DLC')
    add('gate_high_min',4.75-.1-100*(5.25/(68000*.99)+1e-6),4.5,'>=','V','LVC VOH >=Vcc-.1 at <=100uA; static gate load')
    miller=24*50e-12/(2.2e-9*.95+200e-12+50e-12)
    off=10e-6*68000*1.01 + miller
    add('off_gate_with_24V_step',off,1.3,'<=','V','10uA buffer Ioff, Cgd<=50pF & Cgs>=200pF are model allocations; measure')
    release=68000*1.01*(2.2e-9*1.05+200e-12+50e-12)*math.log(5.25/1.3)
    add('power_off_release',release,1e-3,'<=','s','RC estimate excludes slow intermediate-rail behavior; scope power ramps')
    add('full_echo_allocation',8e-6,10.24e-6,'<=','s','52us -9.5*2.08us clock drift -20us ISR -2us edge jitter')
    return {'rows':rows,'thresholds':{'rising_min':min(rising),'rising_max':max(rising),
             'falling_min':min(falling),'falling_max':max(falling),'hysteresis_min':min(hysteresis)},
            'physical_status':'NOT VERIFIED'}
