"""Thirty-day schedule model, cross-checked against the native C++ experiment.

This is mathematical evidence, not a simulated world or human playtest. It assumes
needs are replenished, a usable nearby lock and repeated access to a 48-hour book.
"""
import csv,hashlib,json,math,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Saved/Analysis/CompatibilityClosure'
header=ROOT/'Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Progression/ProjectPracticeSettings.h'
math_header=header.with_name('ProjectPracticeMath.h')
config=(ROOT/'Config/DefaultGame.ini').read_text(encoding='utf-8-sig')
section=config.split('[/Script/EFProjectSystemsGameplay.ProjectPracticeSettings]',1)[1].split('\n[',1)[0]
assert not re.search(r'^[+!\-]?Activities=',section,re.M),'Model must be adapted for overridden activities'
source=header.read_text()
def setting(name):
    override=re.search(r'^'+name+r'\s*=\s*([\d.]+)',section,re.M)
    return float(override.group(1)) if override else float(re.search(r'\b'+name+r'\s*=\s*([\d.]+)',source).group(1))
half,rate,scale=(setting(n) for n in ['ExposureHalfLifeHours','RecoveryPerCalendarHour','RecoveryDebtScale'])
unit,cost,growth,maximum=(setting(n) for n in ['PracticePerHour','PracticeForFirstRank','RankCostGrowth','MaximumAttributeRank'])
def gain(exposure,hours):
    k=math.log(2)/half
    return unit*(.0001*hours+.9999*2**(-exposure/half)*-math.expm1(-k*hours)/k)
def recover(exposure,hours):
    remaining=max(0.,exposure+exposure*exposure/(2*scale)-rate*hours)
    return 2*remaining/(1+math.sqrt(1+2*remaining/scale))
def simulate(initial,schedule,quantum=1/60,checkpoint=False):
    state={n:{'rank':initial,'progress':0.,'exposure':0.,'practice':0.,'hours':0.} for n in ['Strength','Intelligence','Cunning']}
    for hour in range(720):
        daily=hour%24
        selected=(['Strength','Intelligence','Cunning'][daily//2] if daily<6 else None) if schedule=='mixed' else ('Intelligence' if daily<6 else None)
        remaining=1.
        while remaining>1.e-10:
            step=min(quantum,remaining);remaining-=step
            for name,value in state.items():
                if name!=selected:value['exposure']=recover(value['exposure'],step);continue
                learned=gain(value['exposure'],step)
                value['exposure']+=step;value['hours']+=step;value['practice']+=learned;value['progress']+=learned
                while value['rank']<maximum and value['progress']>=cost*(1+growth*value['rank']):
                    value['progress']-=cost*(1+growth*value['rank']);value['rank']+=1
                if value['rank']>=maximum:value['progress']=0.
        if checkpoint and hour%24==23:state=json.loads(json.dumps(state))
    return state
native=list(csv.DictReader((OUT/'thirty_day_balance.csv').read_text().splitlines()[1:]))
results=[]
for initial in [0,25,75]:
    control=simulate(initial,'reading')['Intelligence']
    reference=next(r for r in native if int(r['InitialRank'])==initial and r['Schedule']=='1')
    assert abs(control['practice']-float(reference['Practice']))<1.e-6 and control['rank']==int(reference['FinalRank'])
    mixed=simulate(initial,'mixed')
    accelerated=simulate(initial,'mixed',4/60)
    assert mixed==simulate(initial,'mixed',checkpoint=True),'Model checkpoint changed learning'
    assert all(abs(mixed[n]['practice']-accelerated[n]['practice'])<1.e-6 and mixed[n]['rank']==accelerated[n]['rank'] for n in mixed)
    intelligence=mixed['Intelligence']['rank'];cunning=mixed['Cunning']['rank']
    lock_hours=(.25+2.75*.5)/(1+.01*(cunning+min(5,math.floor(.05*intelligence))))
    results.append({'initial_attribute_rank':initial,'schedule':'2h exercise + 2h reading + 2h lock practice daily, 18h other/rest','attributes':mixed,'completed_48h_readings':int((mixed['Intelligence']['hours']+1.e-6)//48),'hours_in_next_reading':mixed['Intelligence']['hours']%48,'difficulty_50_lock_session_hours_at_end':lock_hours})
report={'scope':'Mathematical schedule model; native C++ daily-reading result cross-check passed; x1/x4 partitions and JSON model checkpoints agree.','limitations':['No human-feel certification','No integrated needs, health or sweat simulation; needs replenishment and access to book/lock are assumptions','Native level curve and world travel are not simulated; separate PIE gates cover persistence','Exercise blocks are cancelled at 2h; cancellation grants no separate reward'], 'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [header,math_header,ROOT/'Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Training/ProjectPracticeComponent.cpp']},'results':results}
(OUT/'mixed_thirty_day_balance.json').write_text(json.dumps(report,indent=2),encoding='utf8')
for r in results:print(r['initial_attribute_rank'],{n:v['rank'] for n,v in r['attributes'].items()},'books',r['completed_48h_readings'],'lock hours',round(r['difficulty_50_lock_session_hours_at_end'],3))
