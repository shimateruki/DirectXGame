"""継承の保存キーと旧計算の圧縮誤差、表示境界、実リソースを調べる。GPU検証とは別。"""
import bisect
import importlib.util
import json
import math
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/cinematic'))
spec=importlib.util.spec_from_file_location('finale_bake',ROOT/'tools/cinematic/bake_crown_finale.py')
legacy=importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy)
asset=json.loads((ROOT/'Resources/json/scenario/crown_finale.json').read_text(encoding='utf-8'))


def sample(rows, stamps, time, discrete=()):
    i=bisect.bisect_right(stamps,time)
    if i==0: return rows[0]
    if i==len(rows): return rows[-1]
    a,b=rows[i-1:i+1]; t=(time-a[0])/(b[0]-a[0])
    return [time]+[a[j] if j in discrete else a[j]+(b[j]-a[j])*t for j in range(1,len(a))]


for clip in asset['presentation']['objects']:
    path=ROOT/'Resources/3DModel'/clip['model']
    if clip['model']:
        candidates=(path,path.with_suffix('.obj'),path.with_suffix('.gltf'),path/(path.name+'.obj'))
        assert any(p.is_file() for p in candidates),clip['model']
    if clip['name'] not in legacy.original_samples: continue
    rows=clip['keys']; stamps=[k[0] for k in rows]
    assert len(set(stamps))==len(stamps),clip['name']
    for time in legacy.times:
        actual=sample(rows,stamps,time,(17,18)); expected=legacy.original_samples[clip['name']](time)
        assert actual[17]==expected[17],(clip['name'],time,'visibility')
        if not expected[17]: continue
        assert actual[18]==expected[18],(clip['name'],time,'clip')
        for j,tolerance in enumerate([.001]*3+[.003]*3+[.001]*3+[.003]*4+[.008,.003,.003],1):
            assert math.isfinite(actual[j]) and abs(actual[j]-expected[j])<=tolerance+1e-7,(clip['name'],time,j)
rows=asset['presentation']['view'];stamps=[k[0] for k in rows]
assert len(set(stamps))==len(stamps),'duplicate camera time'
for time in legacy.times:
    actual=sample(rows,stamps,time,(16,17,18));expected=legacy.view(time)
    assert actual[16:]==expected[16:],(time,'camera cut targets')
    for j,tolerance in enumerate([.00001]+[.001]*12+[.0001,.0001],1):
        assert abs(actual[j]-expected[j])<=tolerance+1e-7,(time,'camera',j)
print(f"{len(asset['presentation']['objects'])} inheritance clips and {len(rows)} camera keys: resource, sample and boundary checks passed")
