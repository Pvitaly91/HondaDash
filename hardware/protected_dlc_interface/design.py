"""M3b.1 revision B pin-level source. Build tooling only; not application code.

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
    "TIMER": ("Analog Devices", "https://www.analog.com/media/en/technical-documentation/data-sheets/LTC6994-1-6994-2.pdf", "Rev C; 2019-11; pp2-5,13-16"),
    "LATCH": ("Texas Instruments", "https://www.ti.com/lit/ds/symlink/sn74lvc1g74.pdf", "SCES794G; 2021-09; sections5,6.3,6.5-6.8,8.4"),
    "NOR": ("Texas Instruments", "https://www.ti.com/lit/ds/symlink/sn74lvc2g02.pdf", "SCES194N; 2019-05; sections5,6.3,6.5,6.6"),
    "AND": ("Texas Instruments", "https://www.ti.com/lit/ds/symlink/sn74lvc2g08.pdf", "SCES198N; 2015-12; sections5,6.3,6.5,6.6"),
    "INV": ("Texas Instruments", "https://www.ti.com/lit/ds/symlink/sn74lvc1g04.pdf", "SCES214AF; 2025-10; sections4,5.3,5.5-5.7"),
    "PG": ("Texas Instruments", "https://www.ti.com/lit/ds/symlink/tps3808.pdf", "SBVS050N; 2026-08; table5-1,sections6.5-6.6,7.3-7.4"),
    "HOLD": ("Texas Instruments", "https://www.ti.com/lit/ds/symlink/lm66100.pdf", "SLVSEZ8A;2019-06;sections5,6.3,6.5-6.6,8.3.2; CE compares held rail with direct raw VIN"),
    "CHOLD": ("KEMET", "https://content.kemet.com/datasheets/KEM_C1050_GOLDMAX_X7R.pdf", "C1050_GOLDMAX_X7R;2025-08-05;ordering p1,Table1E p11:C340/50V/475; effective combined capacitance measured>=4.7uF"),
    "OR": ("Texas Instruments", "https://www.ti.com/lit/ds/symlink/sn74lvc1g32.pdf", "SCES219W;2026-08; sections4,5.3,5.5-5.7"),
    "OPTO": ("Vishay", "https://www.vishay.com/docs/83430/vo617a.pdf", "83430; Rev2.8;2025-01-22;pp1-4,wideDIPoption6mechanicaldrawing"),
    "SW": ("OMRON", "https://omronfs.omron.com/en_US/ecb/products/pdf/en-b3f.pdf", "B3F catalog; CatNoA070-E1-08; ratings p3,pin numbering p4; retrieved2026-10-06"),
    "PROJECT": ("HondaDash", "SCHEMATIC.md", "M3b.1 rev B; functional pads and test points; no vehicle connector pinout"),
}

COMPONENTS = []
def part(ref, mpn, value, source, pins, domain, kind, model=None):
    COMPONENTS.append(dict(ref=ref, mpn=mpn, value=value, source=source,
                           pins={str(k): v for k, v in pins.items()}, domain=domain,
                           kind=kind, model=model))

part("U1", "ISO7721FDWR", "DW-16; default LOW; two opposite channels", "ISO",
     {1:"GND_L",2:"NC_U1_2",3:"V5_L",4:"RX_L_ISO",5:"TX_L_ISO",6:"NC_U1_6",7:"GND_L",8:"NC_U1_8",
      9:"GND_F",10:"NC_U1_10",11:"NC_U1_11",12:"TX_F",13:"RX_F",14:"V5_F",15:"NC_U1_15",16:"GND_F"}, "barrier", "X", "ISO7721F_PROJECT")
part("U2", "SN74LVC1G17DBVR", "DBV-5; noninverting TX gate buffer", "BUF",
     {1:"NC_U2_1",2:"TX_ALLOWED",3:"GND_F",4:"GATE_DRIVE",5:"V5_F"}, "field", "X", "LVC17_PROJECT")
part("U3", "LM393BIDR", "SOIC-8; RX Schmitt comparator; spare held LOW", "CMP",
     {1:"RX_F",2:"VREF",3:"SENSE",4:"GND_F",5:"GND_F",6:"VREF",7:"NC_U3_7",8:"V5_F"}, "field", "X", "LM393B_PROJECT")
part("U4", "TPS70950DBVR", "DBV-5; 5V LDO; EN left open (internal enable)", "LDO",
     {1:"LDO_IN",2:"GND_F",3:"NC_U4_EN",4:"NC_U4_4",5:"V5_F"}, "field", "X", "TPS709_PROJECT")
part("Q1", "PMV88ENEA,215", "60V NMOS SOT23: 1 gate / 2 source / 3 drain", "MOS",
     {1:"GATE",2:"SINK_RET",3:"DRAIN"}, "field", "X", "PMV88_PROJECT")
part("Q2", "PMV88ENEA,215", "Series PG disconnect; G1/S2/D3; does not interrupt D1 negative path", "MOS",
     {1:"PG_GATE",2:"GND_F",3:"SINK_RET"}, "field", "X", "PMV88_PG_PROJECT")
part("U5", "LTC6994IS6-1#TRPBF", "TSOT23-6; rising delay; RSET200k; DIVCODE3/N512", "TIMER",
     {1:"TX_F",2:"GND_F",3:"TIMER_SET",4:"TIMER_DIV",5:"V5_F",6:"TIMEOUT"}, "field", "X", "LTC6994_1_PROJECT")
part("U6", "LTC6994IS6-2#TRPBF", "TSOT23-6; both-edge debounce; RSET249k; DIVCODE4/N4096", "TIMER",
     {1:"ARM_RAW",2:"GND_F",3:"ARM_SET",4:"ARM_DIV",5:"V5_F",6:"ARM_CLK"}, "field", "X", "LTC6994_2_PROJECT")
part("U7", "SN74LVC1G74DCUR", "VSSOP8; Q=ARMED; asynchronous clear has priority", "LATCH",
     {1:"ARM_CLK",2:"ARM_DATA",3:"LOCKED",4:"GND_F",5:"ARMED",6:"CLR_N",7:"V5_F",8:"V5_F"}, "field", "X", "LVC74_PROJECT")
part("U8", "SN74LVC2G02DCUR", "VSSOP8; NOR1=idle qualification; NOR2=clear", "NOR",
     {1:"TX_F",2:"TIMEOUT",3:"CLR_N",4:"GND_F",5:"TIMEOUT",6:"POWER_BAD",7:"IDLE_OK",8:"V5_F"}, "field", "X", "LVC2NOR_PROJECT")
part("U9", "SN74LVC2G08DCUR", "VSSOP8; AND1=TX gating; AND2=arm qualification", "AND",
     {1:"TX_F",2:"ARMED",3:"ARM_SAFE",4:"GND_F",5:"IDLE_OK",6:"PG_CLEAN",7:"TX_ALLOWED",8:"V5_F"}, "field", "X", "LVC2AND_PROJECT")
part("U10", "SN74LVC1G04DBVR", "DBV5; PG inversion", "INV",
     {1:"NC_U10_1",2:"PG_CLEAN",3:"GND_F",4:"NOT_PG",5:"V5_F"}, "field", "X", "LVCINV_PROJECT")
part("U11", "TPS3808G01DBVR", "DBV6; adjustable PG; 100k/10.2k; CT open 12-28ms", "PG",
     {1:"PG_F",2:"GND_F",3:"V5_F",4:"NC_U11_CT",5:"PG_SENSE_F",6:"V5_F"}, "field", "X", "TPS3808_PROJECT")
part("U12", "SN74LVC1G17DBVR", "DBV5; cleans slow PG edge for logic; Q2 driven directly by supervisor", "BUF",
     {1:"NC_U12_1",2:"PG_F",3:"GND_F",4:"PG_CLEAN",5:"V5_F"}, "field", "X", "LVC17_PROJECT")
part("U13", "TPS3808G01DBVR", "DBV6; USB supervisor; 100k/10.2k; drives opto LED switch Q3", "PG",
     {1:"PG_L",2:"GND_L",3:"V5_L",4:"NC_U13_CT",5:"PG_SENSE_L",6:"V5_L"}, "logic", "X", "TPS3808_PROJECT")
part("Q3", "PMV88ENEA,215", "USB-PG LED sink; no field-ground connection", "MOS",
     {1:"PG_LED_GATE",2:"GND_L",3:"LED_K"}, "logic", "X", "PMV88_LED_PROJECT")
part("U14", "VO617A-4X016", "Wide DIP4 option6; 1LED_A 2LED_K 3emitter 4collector; USB health only", "OPTO",
     {1:"LED_A",2:"LED_K",3:"GND_F",4:"USB_BAD"}, "barrier", "X", "VO617_PROJECT")
part("U15", "SN74LVC1G32DBVR", "DBV5; either power loss forces fault clear", "OR",
     {1:"NOT_PG",2:"USB_BAD_CLEAN",3:"GND_F",4:"POWER_BAD",5:"V5_F"}, "field", "X", "LVCOR_PROJECT")
part("U16", "SN74LVC1G17DBVR", "DBV5; Schmitt cleanup of phototransistor edge", "BUF",
     {1:"NC_U16_1",2:"USB_BAD",3:"GND_F",4:"USB_BAD_CLEAN",5:"V5_F"}, "field", "X", "LVC17_PROJECT")
part("U17", "LTC6994IS6-1#TRPBF", "TSOT23-6; post-PG released-button/inactive-TX qualification20.398ms", "TIMER",
     {1:"READY_REQ",2:"GND_F",3:"READY_SET",4:"READY_DIV",5:"V5_F",6:"READY_QUAL"}, "field", "X", "LTC6994_READY_PROJECT")
part("U18", "SN74LVC1G74DCUR", "VSSOP8; readiness memory; clears on power or TX-duration fault", "LATCH",
     {1:"READY_QUAL",2:"V5_F",3:"NC_U18_3",4:"GND_F",5:"REARM_READY",6:"CLR_N",7:"V5_F",8:"V5_F"}, "field", "X", "LVC74_PROJECT")
part("U19", "SN74LVC2G02DCUR", "VSSOP8; physical released-button + healthy power qualification; spare heldLOW", "NOR",
     {1:"ARM_RAW_CLEAN",2:"POWER_BAD",3:"NC_U19_3",4:"GND_F",5:"V5_F",6:"V5_F",7:"RELEASE_OK",8:"V5_F"}, "field", "X", "LVC2NOR_PROJECT")
part("U20", "SN74LVC2G08DCUR", "VSSOP8; release/inactive qualifier and readiness-qualified arm data", "AND",
     {1:"RELEASE_OK",2:"IDLE_OK",3:"ARM_DATA",4:"GND_F",5:"ARM_SAFE",6:"REARM_READY",7:"READY_REQ",8:"V5_F"}, "field", "X", "LVC2AND_PROJECT")
part("U21", "SN74LVC1G17DBVR", "DBV5; cleans physical switch level for readiness qualifier", "BUF",
     {1:"NC_U21_1",2:"ARM_RAW",3:"GND_F",4:"ARM_RAW_CLEAN",5:"V5_F"}, "field", "X", "LVC17_PROJECT")
part("U22", "LM66100DCKR", "DCK6; VIN raw USB; CE held V5_L; resistor is AFTER OUT, not before VIN", "HOLD",
     {1:"USB5V",2:"GND_L",3:"V5_L",4:"NC_U22_4",5:"GND_L",6:"ISO_FEED"}, "logic", "X", "LM66100_PROJECT")
part("U23", "SN74LVC1G17DBVR", "DBV5; D3 input buffer powered held rail; overvoltage-tolerant input", "BUF",
     {1:"NC_U23_1",2:"D3",3:"GND_L",4:"TX_L_ISO",5:"V5_L"}, "logic", "X", "LVC17_PROJECT")
part("U24", "SN74LVC1G17DBVR", "DBV5; RX output powered raw USB; Ioff protects unpowered Nano D8", "BUF",
     {1:"NC_U24_1",2:"RX_L_ISO",3:"GND_L",4:"D8",5:"USB5V"}, "logic", "X", "LVC17_PROJECT")
part("D1", "SS16-M3/IA", "60V 1A SMA; band=K; drain negative clamp", "SS", {"A":"GND_F","K":"DRAIN"}, "field", "X", "SS16_DRAIN_PROJECT")
part("D2", "BAT54,215", "SOT23: 1=A / 2=NC / 3=K; negative RX clamp", "BAT", {1:"GND_F",2:"NC_D2_2",3:"SENSE"}, "field", "D", "BAT54_PROJECT")
part("D3P", "SS16-M3/IA", "Battery reverse protection; band=K", "SS", {"A":"BAT_LIMITED","K":"LDO_IN"}, "field", "D", "SS16_PROJECT")
part("D4", "BAT54,215", "Source-to-field-rail clamp; 1=A / 2=NC / 3=K", "BAT",
     {1:"SINK_RET",2:"NC_D4_2",3:"V5_F"}, "field", "X", "BAT54_CLAMP_PROJECT")
part("D5", "BAT54,215", "Positive gate-to-field-rail clamp; 1=A / 2=NC / 3=K", "BAT",
     {1:"GATE",2:"NC_D5_2",3:"V5_F"}, "field", "X", "BAT54_CLAMP_PROJECT")
part("D6", "BAT54,215", "Negative gate clamp; 1=A / 2=NC / 3=K", "BAT",
     {1:"GND_F",2:"NC_D6_2",3:"GATE"}, "field", "X", "BAT54_CLAMP_PROJECT")

def resistor(ref, ohms, code, a, b, domain="field", power=False):
    part(ref, code, ohms, "POWER_R" if power else "R", {1:a,2:b}, domain, "R")
resistor("R1",220000,"MRS25000C2203FCT00","ECU_DATA","SENSE")
resistor("R2",68000,"MRS25000C6802FCT00","SENSE","GND_F")
resistor("R3",1000000,"MRS25000C1004FCT00","RX_F","SENSE")
resistor("R4",78700,"MRS25000C7872FCT00","V5_F","VREF")
resistor("R5",10000,"MRS25000C1002FCT00","VREF","GND_F")
resistor("R6",2200,"MRS25000C2201FCT00","V5_F","RX_F")
resistor("R7",220,"MRS25000C2200FCT00","GATE_DRIVE","GATE")
resistor("R8",68000,"MRS25000C6802FCT00","GATE","GND_F")
resistor("R9",47000,"MRS25000C4702FCT00","D3","GND_L","logic")
resistor("R10",10000,"MRS25000C1002FCT00","D8","GND_L","logic")
resistor("R11",680,"MRS25000C6800FCT00","V5_F","GND_F")
resistor("R12",22,"AC10000002209JAB00","ECU_DATA","TX_MID",power=True)
resistor("R13",22,"AC10000002209JAB00","TX_MID","DRAIN",power=True)
resistor("R14",47,"AC03000004709JAC00","BAT_PLUS","BAT_LIMITED",power=True)
resistor("R15",200000,"MRS25000C2003FCT00","TIMER_SET","GND_F")
resistor("R16",357000,"MRS25000C3573FCT00","V5_F","TIMER_DIV")
resistor("R17",100000,"MRS25000C1003FCT00","TIMER_DIV","GND_F")
resistor("R18",249000,"MRS25000C2493FCT00","ARM_SET","GND_F")
resistor("R19",255000,"MRS25000C2553FCT00","V5_F","ARM_DIV")
resistor("R20",100000,"MRS25000C1003FCT00","ARM_DIV","GND_F")
resistor("R21",150000,"MRS25000C1503FCT00","V5_F","PG_F")
resistor("R22",10000,"MRS25000C1002FCT00","ARM_RAW","GND_F")
resistor("R23",1000,"MRS25000C1001FCT00","ARM_BUTTON","ARM_RAW")
resistor("R24",2200,"MRS25000C2201FCT00","PG_F","PG_GATE")
resistor("R25",10000000,"MRS25000C1005FCT00","PG_GATE","GND_F")
resistor("R26",150000,"MRS25000C1503FCT00","V5_L","PG_L","logic")
resistor("R27",390,"MRS25000C3900FCT00","USB5V","LED_A","logic")
resistor("R28",10000,"MRS25000C1002FCT00","V5_F","USB_BAD")
resistor("R29",100000,"MRS25000C1003FCT00","V5_F","PG_SENSE_F")
resistor("R30",10200,"MRS25000C1022FCT00","PG_SENSE_F","GND_F")
resistor("R31",100000,"MRS25000C1003FCT00","USB5V","PG_SENSE_L","logic")
resistor("R32",10200,"MRS25000C1022FCT00","PG_SENSE_L","GND_L","logic")
resistor("R33",249000,"MRS25000C2493FCT00","READY_SET","GND_F")
resistor("R34",255000,"MRS25000C2553FCT00","V5_F","READY_DIV")
resistor("R35",100000,"MRS25000C1003FCT00","READY_DIV","GND_F")
resistor("R36",2200,"MRS25000C2201FCT00","PG_L","PG_LED_GATE","logic")
resistor("R37",22,"AC03000002209JAC00","ISO_FEED","V5_L","logic",power=True)
resistor("R38",4700,"MRS25000C4701FCT00","USB5V","GND_L","logic")
resistor("R39",10000,"MRS25000C1002FCT00","TX_L_ISO","GND_L","logic")
resistor("R40",10000,"MRS25000C1002FCT00","RX_L_ISO","GND_L","logic")

def cap(ref, value, mpn, source, a, b, domain="field"):
    part(ref,mpn,value,source,{1:a,2:b},domain,"C")
for ref,a,b,domain in [("C1","V5_L","GND_L","logic"),("C2","V5_F","GND_F","field"),
                        ("C3","V5_F","GND_F","field"),("C4","V5_F","GND_F","field")]:
    cap(ref,1e-7,"C315C104K5R5TA","C100",a,b,domain)
cap("C5",1e-8,"C315C103K5R5TA","C10","VREF","GND_F")
cap("C6",2.2e-6,"C340C225K5R5TA","C2U","LDO_IN","GND_F")
for ref in ["C7","C8","C9"]:
    cap(ref,2.2e-6,"C340C225K5R5TA","C2U","V5_F","GND_F")
cap("C10",2.2e-9,"C315C222J5G5TA","CG","GATE","GND_F")
cap("C11",2.2e-9,"C315C222J5G5TA","CG","PG_GATE","GND_F")
for ref in ["C12","C13","C14","C15","C16","C17","C18","C19"]:
    cap(ref,1e-7,"C315C104K5R5TA","C100","V5_F","GND_F")
cap("C20",1e-7,"C315C104K5R5TA","C100","V5_L","GND_L","logic")
cap("C21",1e-7,"C315C104K5R5TA","C100","V5_F","GND_F")
cap("C22",1e-7,"C315C104K5R5TA","C100","V5_F","GND_F")
for ref in ["C23","C24","C25","C26","C27"]:
    cap(ref,1e-7,"C315C104K5R5TA","C100","V5_F","GND_F")
for ref in ["C28","C29"]:
    cap(ref,4.7e-6,"C340C475K5R5TA","CHOLD","V5_L","GND_L","logic")
cap("C30",1e-7,"C315C104K5R5TA","C100","V5_L","GND_L","logic")
cap("C31",1e-7,"C315C104K5R5TA","C100","USB5V","GND_L","logic")
part("SW1", "B3F-1002-G", "Field-side SPST-NO; terminals1/2 and3/4 pairs; verify bottom-view numbering", "SW",
     {1:"V5_F",2:"V5_F",3:"ARM_BUTTON",4:"ARM_BUTTON"},"field","BUTTON")
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
                       ("TP10","LDO_IN","field"),("TP11","GND_L","logic"),("TP12","GND_F","field"),
                       ("TP13","TX_F","field"),("TP14","TIMEOUT","field"),("TP15","ARMED","field"),
                       ("TP16","PG_F","field"),("TP17","ARM_RAW","field"),("TP18","PG_GATE","field"),
                       ("TP19","LOCKED","field"),("TP20","REARM_READY","field"),
                       ("TP21","READY_QUAL","field"),("TP22","READY_REQ","field"),
                       ("TP23","USB_BAD","field"),("TP24","PG_L","logic"),
                       ("TP25","V5_L","logic"),("TP26","ISO_FEED","logic")]:
    part(ref,"project-test-pad",net,"PROJECT",{1:net},domain,"PAD")

def spice():
    rows = ["* M3b.1 rev B; generated pin interconnect. Models are project behavioral approximations.",
            "* ARM_BUTTON models the field-side dry-contact stimulus: open/0 when released, V5_F when pressed.",
            ".subckt frontend USB5V GND_L D3 D8 ECU_DATA GND_F BAT_PLUS ARM_BUTTON"]
    for p in COMPONENTS:
        k,ref,pins=p["kind"],p["ref"],p["pins"]
        if k in ("R","C"):
            value='{HOLD_CAP/2}' if ref in ('C28','C29') else f"{p['value']:.12g}"
            rows.append(f"{ref} {pins['1']} {pins['2']} {value}")
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
    field_columns=[[],[],[]]; field_heights=[0,0,0]
    for p in field:
        slot=min(range(3),key=lambda i:field_heights[i])
        field_columns[slot].append(p)
        field_heights[slot]+=76+23*len(p['pins'])
    columns += field_columns
    heights=[sum(76+len(p['pins'])*23 for p in col)+720 for col in columns]
    h=max(heights); w=2400
    out=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',
         '<rect width="100%" height="100%" fill="#f8fafc"/>',
         '<style>text{font:15px monospace;fill:#152238}.title{font:bold 23px sans-serif}.ref{font:bold 16px monospace} .pin{stroke:#344d65;stroke-width:2}</style>',
         '<text x="24" y="35" class="title">HondaDash M3b.1 revision B — independent TX cutoff/latch — NOT VERIFIED</text>',
         '<text x="24" y="65">NET-LABEL SCHEMATIC: identical labels connect. All IC pin numbers are top-view. NC_* pads stay open.</text>',
         '<text x="24" y="90">No vehicle connector assignment. B1 is an independent battery. No USB5V pullup on ECU_DATA. See SCHEMATIC.md.</text>']
    out.append("""
<rect x="25" y="118" width="670" height="510" fill="#e9edfa"/>
<rect x="715" y="118" width="1660" height="510" fill="#e6f3ef"/>
<path d="M705 120V625" stroke="#bc7b18" stroke-width="4" stroke-dasharray="10 8"/>
<text x="45" y="155" class="title">USB / Nano / GND_L</text>
<text x="735" y="155" class="title">Independent battery / field / GND_F</text>
<text x="50" y="208">D3 → U23(held) → U1.INB5 ║ OUTB12 → TX_F</text>
<text x="740" y="208">TX_F → U5 LTC6994-1 (2.048ms) → TIMEOUT → U8.CLR_N</text>
<text x="740" y="250">SW1 → R23/R22 → U6 debounce → ARM_CLK → U7 DFF</text>
<text x="740" y="290">U17/U18 READY latch: released button + idle + healthy power for20ms first</text>
<text x="740" y="332">TX_F AND U7.ARMED → U9 → U2 → R7/C10 → Q1.G</text>
<text x="740" y="374">ECU_DATA → R12 22R → R13 22R → Q1.D / Q1.S → Q2.D / Q2.S → GND_F</text>
<text x="740" y="416">U11 PG_F → R24/C11 → Q2.G; U12 cleans PG for logic; D1 negative path remains</text>
<text x="50" y="270">USB5V → U22 → R37 → V5_L; U22.CE=V5_L</text>
<text x="50" y="310">U13: sense rawUSB / powered heldV5_L → Q3</text>
<text x="740" y="455">U14 collector USB_BAD + NOT_PG → U15.POWER_BAD</text>
<text x="50" y="350">USB5V → R27 → U14 LED → Q3; optical barrier</text>
<text x="50" y="420">D8 ← U24(raw) ← U1.OUTA4 ║ INA13 ← RX_F</text>
<text x="740" y="495">ARM_DATA=IDLE_OK AND PG_CLEAN AND REARM_READY; RX: R1/R2/R3/D2/U3→RX_F</text>
<text x="50" y="548">No field-ground button or test pad</text>
<text x="50" y="580">may connect to logic ground.</text>
<text x="740" y="548">B1 → R14/D3P → U4 → V5_F. No line pullup in adapter.</text>
<text x="740" y="580">Field button and all failsafe test points remain in GND_F domain.</text>
<text x="740" y="610">No vehicle connector pinout. Domain PCB spacing ≥8mm; NOT VERIFIED.</text>
<text x="24" y="669" class="title">Complete pin map — equal net labels connect; NC labels stay open</text>
""")
    for i,col in enumerate(columns):
        x=24+i*476; y=710
        for p in col:
            ph=64+23*len(p['pins']); fill='#e6f3ef' if p['domain']=='field' else '#e9edfa'
            if p['domain']=='barrier': fill='#fff1d7'
            out.append(f'<g id="{p["ref"]}" data-mpn="{escape(p["mpn"])}"><rect x="{x}" y="{y}" width="445" height="{ph}" rx="5" fill="{fill}" stroke="#8a9bad"/>')
            out.append(f'<text x="{x+10}" y="{y+22}" class="ref">{p["ref"]} {escape(p["mpn"])}</text>')
            val=str(p['value']); val=(val[:46]+'…') if len(val)>47 else val
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
            'netlist.json':json.dumps({'revision':'M3b.1-B','components':COMPONENTS,'sources':SOURCES},indent=2)+'\n'}

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
