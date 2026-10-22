"""移行前の計算と保存済みキーを比較する。描画・ゲーム進行の実機検証ではない。"""
import bisect
import importlib.util
import json
import math
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('legacy_bake',ROOT/'tools/cinematic/bake_crown_defeat.py')
legacy=importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy)
asset=json.loads((ROOT/'Resources/json/scenario/crown_defeat.json').read_text(encoding='utf-8'))


def sample(rows,time,discrete=()):
    i=bisect.bisect_right([k[0] for k in rows],time)
    if i==0:return rows[0]
    if i==len(rows):return rows[-1]
    a,b=rows[i-1:i+1]
    t=(time-a[0])/(b[0]-a[0])
    return [time]+[a[j] if j in discrete else a[j]+(b[j]-a[j])*t for j in range(1,len(a))]


limits=[.001]*3+[.003]*3+[.001]*3+[.003]*4+[.008,.003,.003]
worst=0
for clip in asset['presentation']['objects']:
    assert len({k[0] for k in clip['keys']})==len(clip['keys']),clip['name']
    if clip['model']:
        assert (ROOT/'Resources/3DModel'/clip['model']).is_file(),clip['model']
    reference=legacy.original_samples[clip['name']]
    for time in legacy.times:
        expected=reference(time)
        actual=sample(clip['keys'],time,(17,18))
        assert actual[17]==expected[17],(clip['name'],time,'visibility boundary')
        if expected[17] or not clip['model']:
            assert actual[18]==expected[18],(clip['name'],time,'clip boundary')
            for j,tolerance in enumerate(limits,1):
                error=abs(expected[j]-actual[j])
                assert math.isfinite(actual[j]) and error<=tolerance+1e-7,(clip['name'],time,j,error)
                worst=max(worst,error/tolerance)
for time in legacy.times:
    expected=legacy.view(time)
    actual=sample(asset['presentation']['view'],time)
    for j,tolerance in enumerate([.0005]+[.002]*6+[.003]*6+[.001,.001],1):
        assert abs(expected[j]-actual[j])<=tolerance+1e-7,('camera',time,j)
names=[c['name'] for c in asset['presentation']['objects']]
assert len(set(names))==len(names)
assert {p['target'] for p in asset['presentation']['particles']}<=set(names)
print(f'{len(names)} clips and camera match the legacy formulas within authored compression tolerances ({worst:.3f} of limit).')
