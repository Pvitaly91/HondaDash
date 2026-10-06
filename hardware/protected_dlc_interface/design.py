"""M3b rev A pin-level source. Build tooling only; not application code.

Net names are electrical connections. NC_* names are intentionally isolated pads.
The SVG, BOM and SPICE interconnect are rendered from this data; --check detects drift.
"""
from pathlib import Path
import csv
import hashlib
import io
import json
from html import escape

ROOT = Path(__file__).resolve().parent
SOURCES = {
    "ISO": ("Texas Instruments", "https://www.ti.com/lit/ds/symlink/iso7721.pdf", "SLLSEP3G; 2024-05; table 5-1, sections 6.3, 6.10, 6.15, 8.4"),
    "BUF": ("Texas Instruments", "https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf", "SCES351Y; 2025-10; sections 4, 5.5-5.8"),
    "CMP": ("Texas Instruments", "https://www.ti.com/lit/ds/symlink/lm393.pdf", "SLCS005AH; 2025-04; sections 4, 5.1, 5.5, 5.7, 7.2.2.1"),
    "LDO": ("Texas Instruments", "https://www.ti.com/lit/ds/symlink/tps709.pdf", "SBVS186H; 2021-07; table 5-1, sections 6.5, 7.3, 8.2"),
    "MOS": ("Nexperia", "https://assets.nexperia.com/documents/data-sheet/PMV88ENEA.pdf", "2026-08-03; tables 2, 5-7, section 10, figures 3, 13"),
    "BAT": ("Nexperia", "https://assets.nexperia.com/documents/data-sheet/BAT54.pdf", "2022-07-01; tables 2, 5-7"),
    "SS": ("Vishay", "https://www.vishay.com/docs/98653/ss16hm3_bia_ss16-m3ia.pdf", "98653; 2025-03-12; pages 1-3, electrical and thermal tables"),
    "R": ("Vishay", "https://www.vishay.com/docs/28724/mrs16m25.pdf", "28724; 2016-03-07; electrical and ordering tables"),
    "POWER_R": ("Vishay", "https://www.vishay.com/docs/28730/ac_ac-at_ac-ni.pdf", "28730; 2024-12-05; pages 1-4, derating and ordering"),
    "C100": ("KEMET", "https://search.kemet.com/component-documentation/download/specsheet/C315C104K5R5TA", "generated per-part specification; retrieved 2026-10-06; specifications"),
    "C10": ("KEMET", "https://search.kemet.com/component-documentation/download/specsheet/C315C103K5R5TA", "generated 2026-10-06; specifications"),
    "CG": ("KEMET", "https://search.kemet.com/component-documentation/download/specsheet/C315C222J5G5TA", "generated 2026-10-06; specifications"),
    "C2U": ("KEMET", "https://search.kemet.com/component-documentation/download/specsheet/C340C225K5R5TA", "generated per-part specification; retrieved 2026-10-06; specifications"),
    "CELL": ("Energizer", "https://data.energizer.com/pdfs/522_ap.pdf", "Form EBC-1108L; undated; nominal voltage and temperature table"),
    "PROJECT": ("HondaDash", "SCHEMATIC.md", "M3b rev A; functional pads and test points; no vehicle connector pinout"),
}

COMPONENTS = []
def part(ref, mpn, value, source, pins, domain, kind, model=None):
    COMPONENTS.append(dict(ref=ref, mpn=mpn, value=value, source=source,
                           pins={str(k): v for k, v in pins.items()}, domain=domain,
                           kind=kind, model=model))

part("U1", "ISO7721FDWR", "DW-16; default LOW; two opposite channels", "ISO",
     {1:"GND_L",2:"NC_U1_2",3:"USB5V",4:"D8",5:"D3",6:"NC_U1_6",7:"GND_L",8:"NC_U1_8",
      9:"GND_F",10:"NC_U1_10",11:"NC_U1_11",12:"TX_F",13:"RX_F",14:"V5_F",15:"NC_U1_15",16:"GND_F"}, "barrier", "X", "ISO7721F_PROJECT")
part("U2", "SN74LVC1G17DBVR", "DBV-5; noninverting TX gate buffer", "BUF",
     {1:"NC_U2_1",2:"TX_F",3:"GND_F",4:"GATE_DRIVE",5:"V5_F"}, "field", "X", "LVC17_PROJECT")
part("U3", "LM393BIDR", "SOIC-8; RX Schmitt comparator; spare held LOW", "CMP",
     {1:"RX_F",2:"VREF",3:"SENSE",4:"GND_F",5:"GND_F",6:"VREF",7:"NC_U3_7",8:"V5_F"}, "field", "X", "LM393B_PROJECT")
part("U4", "TPS70950DBVR", "DBV-5; 5V LDO; EN left open (internal enable)", "LDO",
     {1:"LDO_IN",2:"GND_F",3:"NC_U4_EN",4:"NC_U4_4",5:"V5_F"}, "field", "X", "TPS709_PROJECT")
part("Q1", "PMV88ENEA,215", "60V NMOS SOT23: 1 gate / 2 source / 3 drain", "MOS",
     {1:"GATE",2:"GND_F",3:"DRAIN"}, "field", "X", "PMV88_PROJECT")
part("D1", "SS16-M3/IA", "60V 1A SMA; band=K; drain negative clamp", "SS", {"A":"GND_F","K":"DRAIN"}, "field", "D", "SS16_PROJECT")
part("D2", "BAT54,215", "SOT23: 1=A / 2=NC / 3=K; negative RX clamp", "BAT", {1:"GND_F",2:"NC_D2_2",3:"SENSE"}, "field", "D", "BAT54_PROJECT")
part("D3P", "SS16-M3/IA", "Battery reverse protection; band=K", "SS", {"A":"BAT_LIMITED","K":"LDO_IN"}, "field", "D", "SS16_PROJECT")

def resistor(ref, ohms, code, a, b, domain="field", power=False):
    part(ref, code, ohms, "POWER_R" if power else "R", {1:a,2:b}, domain, "R")
resistor("R1",220000,"MRS25000C2203FCT00","ECU_DATA","SENSE")
resistor("R2",68000,"MRS25000C6802FCT00","SENSE","GND_F")
resistor("R3",1000000,"MRS25000C1004FCT00","RX_F","SENSE")
resistor("R4",78700,"MRS25000C7872FCT00","V5_F","VREF")
resistor("R5",10000,"MRS25000C1002FCT00","VREF","GND_F")
resistor("R6",2200,"MRS25000C2201FCT00","V5_F","RX_F")
resistor("R7",100,"MRS25000C1000FCT00","GATE_DRIVE","GATE")
resistor("R8",68000,"MRS25000C6802FCT00","GATE","GND_F")
resistor("R9",47000,"MRS25000C4702FCT00","D3","GND_L","logic")
resistor("R10",10000,"MRS25000C1002FCT00","D8","GND_L","logic")
resistor("R11",1000,"MRS25000C1001FCT00","V5_F","GND_F")
resistor("R12",22,"AC10000002209JAB00","ECU_DATA","TX_MID",power=True)
resistor("R13",22,"AC10000002209JAB00","TX_MID","DRAIN",power=True)
resistor("R14",47,"AC03000004709JAC00","BAT_PLUS","BAT_LIMITED",power=True)

def cap(ref, value, mpn, source, a, b, domain="field"):
    part(ref,mpn,value,source,{1:a,2:b},domain,"C")
for ref,a,b,domain in [("C1","USB5V","GND_L","logic"),("C2","V5_F","GND_F","field"),
                        ("C3","V5_F","GND_F","field"),("C4","V5_F","GND_F","field")]:
    cap(ref,1e-7,"C315C104K5R5TA","C100",a,b,domain)
cap("C5",1e-8,"C315C103K5R5TA","C10","VREF","GND_F")
cap("C6",2.2e-6,"C340C225K5R5TA","C2U","LDO_IN","GND_F")
for ref in ["C7","C8","C9"]:
    cap(ref,2.2e-6,"C340C225K5R5TA","C2U","V5_F","GND_F")
cap("C10",2.2e-9,"C315C222J5G5TA","CG","GATE","GND_F")
part("J1","project-logic-pads","Nano: USB5V/GND/D3/D8; no VIN", "PROJECT",
     {1:"USB5V",2:"GND_L",3:"D3",4:"D8"},"logic","PAD")
part("J2","project-field-pads","Functional labels only; third pad not routed", "PROJECT",
     {1:"ECU_DATA",2:"GND_F",3:"VEHICLE_POWER_NC"},"field","PAD")
part("J3","project-battery-pads","Battery disconnected to switch OFF", "PROJECT",
     {1:"BAT_PLUS",2:"GND_F"},"field","PAD")
part("B1","522","External removable 9V alkaline; accept measured 8.0-9.6V", "CELL",
     {"+":"BAT_PLUS","-":"GND_F"},"field","FIXTURE")
for ref,net,domain in [("TP1","D3","logic"),("TP2","D8","logic"),("TP3","ECU_DATA","field"),
                       ("TP4","SENSE","field"),("TP5","VREF","field"),("TP6","RX_F","field"),
                       ("TP7","GATE","field"),("TP8","DRAIN","field"),("TP9","V5_F","field"),
                       ("TP10","LDO_IN","field"),("TP11","GND_L","logic"),("TP12","GND_F","field")]:
    part(ref,"project-test-pad",net,"PROJECT",{1:net},domain,"PAD")

def spice():
    rows = ["* M3b rev A; generated pin interconnect. Models are project behavioral approximations.",
            ".subckt frontend USB5V GND_L D3 D8 ECU_DATA GND_F BAT_PLUS"]
    for p in COMPONENTS:
        k,ref,pins=p["kind"],p["ref"],p["pins"]
        if k in ("R","C"):
            rows.append(f"{ref} {pins['1']} {pins['2']} {p['value']:.12g}")
        elif k=="X":
            rows.append(f"X{ref} {' '.join(pins.values())} {p['model']}")
        elif k=="D":
            a,c=(pins['1'],pins['3']) if ref=="D2" else (pins['A'],pins['K'])
            rows.append(f"{ref} {a} {c} {p['model']}")
    return '\n'.join(rows+[".ends frontend", ""])

def svg():
    # Pin-labeled net schematic, not a placement/layout drawing. Every pin is visible.
    # Repeated net labels connect; NC pads do not connect to other NC pads.
    columns = [[p for p in COMPONENTS if p['domain']==d] for d in ('logic','barrier','field')]
    # Keep field symbols in three further columns to make the sheet practical to read.
    field=columns.pop()
    columns += [field[:11],field[11:24],field[24:]]
    heights=[sum(76+len(p['pins'])*23 for p in col)+720 for col in columns]
    h=max(heights); w=2400
    out=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',
         '<rect width="100%" height="100%" fill="#f8fafc"/>',
         '<style>text{font:15px monospace;fill:#152238}.title{font:bold 23px sans-serif}.ref{font:bold 16px monospace} .pin{stroke:#344d65;stroke-width:2}</style>',
         '<text x="24" y="35" class="title">HondaDash M3b rev A — isolated protected-interface candidate — NOT VERIFIED</text>',
         '<text x="24" y="65">NET-LABEL SCHEMATIC: identical labels connect. All IC pin numbers are top-view. NC_* pads stay open.</text>',
         '<text x="24" y="90">No vehicle connector assignment. B1 is an independent battery. No USB5V pullup on ECU_DATA. See SCHEMATIC.md.</text>']
    out.append('''
<rect x="25" y="118" width="670" height="515" rx="8" fill="#e9edfa"/>
<rect x="715" y="118" width="1660" height="515" rx="8" fill="#e6f3ef"/>
<path d="M705 120V630" stroke="#bc7b18" stroke-width="4" stroke-dasharray="10 8"/>
<text x="50" y="151" class="title">USB / Nano / GND_L</text>
<text x="760" y="151" class="title">Independent battery / field circuit / GND_F</text>
<rect x="580" y="185" width="250" height="320" fill="#fff1d7" stroke="#a66c14" stroke-width="2"/>
<text x="596" y="213" class="ref">U1 ISO7721FDWR</text>
<text x="600" y="240">INB5 → OUTB12</text>
<text x="600" y="470">OUTA4 ← INA13</text>
<text x="606" y="358">signal barrier</text>
<text x="600" y="381">F default LOW</text>
<path d="M110 265H580 M830 265H960 M1160 265H1280 M1380 265H1460" fill="none" stroke="#20578b" stroke-width="3"/>
<text x="110" y="250">TP1 D3 / sink enable HIGH</text>
<text x="850" y="250">TX_F</text>
<rect x="960" y="235" width="200" height="60" fill="white" stroke="#20578b"/>
<text x="975" y="260" class="ref">U2 LVC1G17</text><text x="975" y="282">2 A → 4 Y</text>
<rect x="1280" y="253" width="100" height="24" fill="white" stroke="#20578b"/>
<text x="1280" y="240">R7 100R</text>
<path d="M1460 235V295 M1475 235V295 M1475 240H1520V210H1650 M1475 290H1520V328" fill="none" stroke="#20578b" stroke-width="3"/>
<path d="M1500 328H1540 M1507 335H1533 M1514 342H1526" stroke="#20578b" stroke-width="2"/>
<text x="1420" y="370">Q1 PMV88ENEA</text><text x="1440" y="392">S2=GND_F</text>
<text x="1430" y="224">G1</text><text x="1530" y="201">pin3 / DRAIN</text>
<rect x="1650" y="198" width="130" height="24" fill="white" stroke="#20578b"/>
<rect x="1860" y="198" width="130" height="24" fill="white" stroke="#20578b"/>
<path d="M1780 210H1860 M1990 210H2220V420H2070" fill="none" stroke="#20578b" stroke-width="3"/>
<text x="1650" y="184">R13 22R/10W</text><text x="1860" y="184">R12 22R/10W</text>
<text x="2050" y="192" class="ref">TP3 ECU_DATA</text>
<text x="1660" y="262">D1: A=GND_F, K=DRAIN</text>
<text x="1660" y="287">R8/C10: GATE to GND_F</text>
<rect x="1920" y="408" width="150" height="24" fill="white" stroke="#20578b"/>
<text x="1930" y="395">R1 220k</text>
<path d="M1920 420H1770V405H1640" fill="none" stroke="#20578b" stroke-width="3"/>
<circle cx="1770" cy="420" r="5" fill="#20578b"/>
<text x="1730" y="455">SENSE</text>
<path d="M1640 370L1515 430L1640 490Z" fill="white" stroke="#20578b" stroke-width="2"/>
<text x="1605" y="413">+</text><text x="1605" y="464">−</text>
<path d="M1640 458H1690" stroke="#20578b" stroke-width="3"/>
<text x="1670" y="486">VREF</text>
<text x="1538" y="432">U3A</text><text x="1415" y="516">LM393B + hysteresis</text>
<path d="M1515 430H830 M580 430H110" fill="none" stroke="#20578b" stroke-width="3"/>
<text x="1120" y="418">RX_F / TP6</text><text x="110" y="416">TP2 D8 / noninverted RX</text>
<text x="910" y="476">R2: SENSE→GND_F; R3: RX_F→SENSE</text>
<text x="910" y="501">R4/R5: VREF divider; R6: RX_F pullup only</text>
<text x="1830" y="515">D2: A=GND_F, K=SENSE</text>
<text x="50" y="558">J1: USB5V, GND_L, D3, D8</text>
<text x="50" y="584">No VIN; R9/R10 hold D3/D8 LOW</text>
<text x="760" y="558">B1(+) → R14 47R/3W → D3P(A→K) → U4.IN1 → U4.OUT5 → V5_F</text>
<text x="760" y="584">B1(−)=GND_F; U4.EN3 open; R11 bleeds field rail; C1–C10 see pin map below</text>
<text x="760" y="610">J2.3 VEHICLE_POWER_NC: no trace. Keep ≥8mm domain clearance/creepage; unqualified PCB.</text>
<text x="24" y="669" class="title">Complete pin map and passives — equal labels connect; NC labels remain open</text>
''')
    for i,col in enumerate(columns):
        x=24+i*476; y=710
        for p in col:
            ph=64+23*len(p['pins']); fill='#e6f3ef' if p['domain']=='field' else '#e9edfa'
            if p['domain']=='barrier': fill='#fff1d7'
            out.append(f'<g id="{p["ref"]}" data-mpn="{escape(p["mpn"])}"><rect x="{x}" y="{y}" width="445" height="{ph}" rx="5" fill="{fill}" stroke="#8a9bad"/>')
            out.append(f'<text x="{x+10}" y="{y+22}" class="ref">{p["ref"]} {escape(p["mpn"])}</text>')
            val=str(p['value']); val=(val[:49]+'…') if len(val)>50 else val
            out.append(f'<text x="{x+10}" y="{y+43}">{escape(val)}</text>')
            for j,(pin,net) in enumerate(p['pins'].items()):
                py=y+65+j*23
                out.append(f'<line class="pin" x1="{x+10}" x2="{x+40}" y1="{py-5}" y2="{py-5}"/>')
                out.append(f'<text x="{x+47}" y="{py}" data-pin="{pin}" data-net="{net}">{pin:>2} — {escape(net)}</text>')
            out.append('</g>'); y+=ph+12
    out.append('</svg>\n')
    return '\n'.join(out)

def outputs():
    s=io.StringIO(newline=''); writer=csv.writer(s,lineterminator='\n')
    writer.writerow(['ref','quantity','manufacturer','part_number','value','domain','pins','source_url','source_revision'])
    for p in COMPONENTS:
        maker,url,rev=SOURCES[p['source']]
        writer.writerow([p['ref'],1,maker,p['mpn'],p['value'],p['domain'],json.dumps(p['pins'],sort_keys=True),url,rev])
    return {'BOM.csv':s.getvalue(),'schematic.svg':svg(),'simulation/frontend.cir':spice(),
            'netlist.json':json.dumps({'revision':'M3b-A','components':COMPONENTS,'sources':SOURCES},indent=2)+'\n'}

def render(check=False):
    for name,data in outputs().items():
        path=ROOT/name
        if check:
            if not path.exists() or path.read_text(encoding='utf-8')!=data:
                raise ValueError(f'{name} differs from design.py; render and review')
        else:
            path.parent.mkdir(parents=True,exist_ok=True); path.write_text(data,encoding='utf-8',newline='\n')
    return hashlib.sha256(json.dumps(COMPONENTS,sort_keys=True).encode()).hexdigest()

if __name__=='__main__':
    import argparse
    ap=argparse.ArgumentParser(); ap.add_argument('--check',action='store_true'); args=ap.parse_args()
    print(render(args.check))
