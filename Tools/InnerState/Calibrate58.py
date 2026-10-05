"""Read-only timing calculator from the live source defaults, no duplicate tuning table."""
import argparse
import json
import re
from pathlib import Path

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('--day-seconds',type=float,default=1800)
a=p.parse_args()
if a.day_seconds<=0: p.error('day length must be positive')
source=(root/'Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Survival/ProjectInnerStateSettings.h').read_text()
settings={k:float(v) for k,v in re.findall(r'double\s+(\w+)\s*=\s*([\d.]+)\s*;', source)}
poison_source=(root/'Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Survival/ProjectInnerStateMath.h').read_text()
poison={k:float(v) for k,v in re.findall(r'double\s+(\w+)\s*=\s*([\d.]+)\s*;', poison_source)}
hour=a.day_seconds/24
print(json.dumps({'day_seconds':a.day_seconds,'seconds_per_game_hour':hour,
 'bathroom_deadline_real_minutes':settings['BathroomDeadlineHours']*hour/60,
 'hygiene_100_to_20_real_minutes':80/settings['HygieneLossPerHour']*hour/60,
 'untreated_sickness_25_to_100_real_minutes':75/settings['SicknessGainPerHour']*hour/60,
 'treated_sickness_100_to_0_real_minutes':100/settings['TreatedSicknessRecoveryPerHour']*hour/60,
 'rest_0_to_100_real_minutes_at_x1':100/(settings['SleepingRecoveryPerHour']-2.625)*hour/60,
 'air_rest_seconds':settings['AirReserveSeconds'], 'air_exertion_seconds':settings['AirReserveExertionSeconds'],
 'awareness_100_to_0_after_air_empty_seconds':100/settings['AsphyxiaAwarenessPerSecond'],
 'poison_25_fast_seconds_including_delay':poison['FastDelay']+25/poison['FastRate'],
 'poison_100_fast_seconds_immediate_activation':100/poison['FastRate'],
 'poison_200_fast_seconds_immediate_activation':200/poison['FastRate'],
 'note':'Calendar estimates use source defaults; Config overrides and existing need/status modifiers must be read from the live editor.'},indent=2))
