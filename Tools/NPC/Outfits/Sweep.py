"""Sequential, bounded warmed crowd pairs; never overlap GPU measurements.

The editor must already be open in the SaveGuard-protected target session.
Runtime failures stop the sweep. Budget failures remain FAIL and do not hide later
requested group measurements. Warmup receipts are preparation, not cold-load tests.
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

root=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser()
parser.add_argument('--counts',nargs='+',type=int,choices=[1,10,25,50],default=[1,10,25,50])
parser.add_argument('--prefix',required=True)
parser.add_argument('--body',default='Male.01')
parser.add_argument('--archetype',default='SkillGuy')
parser.add_argument('--seed',type=int,default=731)
parser.add_argument('--independent-npc-lod',type=int,choices=[0,1],default=0)
args=parser.parse_args()
assert args.prefix.replace('_','').replace('-','').isalnum()
for count in args.counts:
    paths=[]
    for dressed in [0,1]:
        name=f'{args.prefix}_{count}_'+('dressed' if dressed else 'bare')
        out=root/'Saved/NPCOutfits/Native'/name
        assert not out.exists(), f'Use a fresh prefix: {out}'
        command=[sys.executable,str(root/'Tools/NPC/Outfits/Run.py'),'RunNativePIE',
                 '--count',str(count),'--dressed',str(dressed),'--name',name,'--prewarm',
                 '--body',args.body,'--archetype',args.archetype,'--seed',str(args.seed),
                 '--independent-npc-lod',str(args.independent_npc_lod)]
        start=time.monotonic()
        subprocess.run(command,cwd=root,timeout=150,check=True)
        while not (out/'host.json').exists() and time.monotonic()-start<155: time.sleep(1)
        assert (out/'host.json').exists(), 'Host ceiling exceeded; inspect PIE before continuing'
        host=json.loads((out/'host.json').read_text(encoding='utf-8-sig'))
        assert host['status']=='PASS', host
        paths.append(str(out/'runtime.json'))
    output=root/'Saved/NPCOutfits/Comparisons'/f'{args.prefix}_{count}.json'
    subprocess.run([sys.executable,str(root/'Tools/NPC/Outfits/Measure.py'),*paths,'--output',str(output)],
                   cwd=root,timeout=15,check=False)
    assert output.exists(), 'Comparison failed without a receipt'
