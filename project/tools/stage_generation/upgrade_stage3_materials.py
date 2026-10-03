"""ステージ3の材質・照明だけを更新する。配置やゲーム進行は変更しない。"""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import re
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCENE = Path('Resources/json/3Dobject/stage3_object.json')
LIGHT = Path('Resources/json/light/stage3_high_crown.json')
MTLS = [Path('Resources/3DModel/Stages/high_crown/high_crown.mtl'),
        Path('Resources/3DModel/Stages/high_crown_background/high_crown_background.mtl')]

# アルベドに既に色が入っているため、Kdの二重着色を避ける。
# 値は roughness / metallic。石材・布・草は非金属として扱う。
SURFACES = {
    'StoneDark': (.88, 0), 'Stone': (.82, 0), 'StoneLight': (.72, 0),
    'PavingA': (.70, 0), 'PavingB': (.78, 0),
    'Grass': (.96, 0), 'GrassLight': (.96, 0),
    'Gold': (.28, .86), 'GateBlack': (.38, .78),
    'GateGlow': (.24, 0), 'PurpleRune': (.24, 0),
    'Banner': (.92, 0), 'FlameGlow': (.30, 0),
    'BackdropStoneDark': (.88, 0), 'BackdropStone': (.82, 0),
    'BackdropStoneLight': (.72, 0), 'BackdropGold': (.28, .86),
    'BackdropRune': (.24, 0), 'BackdropBanner': (.92, 0),
}
VISUAL_FIELDS = {'roughness', 'metallic', 'normalMapPath', 'ormMapPath',
                 'envIntensity', 'enableEnvMap', 'enableNormalMap', 'enableLighting'}


def update_mtl(text: str) -> str:
    blocks = re.split(r'(?=^newmtl )', text, flags=re.M)
    for index, block in enumerate(blocks):
        match = re.match(r'newmtl (\S+)', block)
        if not match or match[1] not in SURFACES:
            continue
        name = match[1]
        roughness, metallic = SURFACES[name]
        for key, value in {'Kd': '1.000000 1.000000 1.000000',
                           'Pr': f'{roughness:.6f}', 'Pm': f'{metallic:.6f}',
                           'Ns': f'{(1-roughness)*128:.6f}'}.items():
            block = re.sub(rf'^{key} .*$', f'{key} {value}', block, flags=re.M)
        # 共通ORMのG=.72を掛けて石まで磨いた面になる経路を解除する。
        # 未指定ORMは描画側の白テクスチャになり、Pr/Pmをそのまま使える。
        block = re.sub(r'^map_Pm .*\n', '', block, flags=re.M)
        if metallic > .5:
            prefix = '../high_crown/' if name.startswith('Backdrop') else ''
            block = re.sub(r'^map_Bump .*$',
                           f'map_Bump {prefix}high_crown_gate_surface_normal.dds', block, flags=re.M)
        blocks[index] = block
    return ''.join(blocks)


def update_scene(scene: dict) -> int:
    count = 0
    for obj in scene['objects']:
        model = obj.get('modelName', '')
        if not model.startswith(('Stages/high_crown/', 'Stages/high_crown_background/')):
            continue
        if 'cloud' in model or obj.get('materialType', 0) != 0:
            continue
        # モデルの材質差分を活かし、オブジェクト全体の共通マップを解除する。
        obj.update(roughness=.5, metallic=0.0, normalMapPath='', ormMapPath='',
                   envIntensity=.32, enableEnvMap=True, enableNormalMap=True, enableLighting=True)
        count += 1
    return count


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true', help='検証後に更新（未指定は検査のみ）')
    args = parser.parse_args()
    original = json.loads((ROOT / SCENE).read_text(encoding='utf-8-sig'))
    scene = copy.deepcopy(original)
    count = update_scene(scene)
    assert count > 0
    # コライダー、GUID、座標、演出、パラメータを含む非描画項目を全件照合する。
    for before, after in zip(original['objects'], scene['objects'], strict=True):
        assert {k:v for k,v in before.items() if k not in VISUAL_FIELDS} == {
            k:v for k,v in after.items() if k not in VISUAL_FIELDS}
    second = copy.deepcopy(scene)
    update_scene(second)
    assert second == scene, '二重実行で変化しています'
    light = json.loads((ROOT / LIGHT).read_text(encoding='utf-8-sig'))
    light['shadow']['resolution'] = 4096
    # 影の範囲は維持。日光を増やし、環境光と白い散乱のベールを抑える。
    light['directionalLight'].update(
        ambientColor=[.27, .32, .41], color=[1.0, .95, .85, 1.0],
        direction=[-.48, -.76, .44], intensity=1.75,
        fogStart=190.0, fogEnd=540.0, volumetricIntensity=.012)
    updates = {SCENE: json.dumps(scene, ensure_ascii=False, indent=4)+'\n',
               LIGHT: json.dumps(light, ensure_ascii=False, indent=4)+'\n'}
    for path in MTLS:
        updated = update_mtl((ROOT / path).read_text(encoding='utf-8-sig'))
        assert update_mtl(updated) == updated
        for reference in re.findall(r'^map_\S+ (.+)$', updated, re.M):
            assert (ROOT / path.parent / reference.strip()).is_file(), reference
        updates[path] = updated
    print(f'Validated {count} visual objects; gameplay/transform/collision fields preserved.')
    if args.apply:
        backup = Path(tempfile.mkdtemp(prefix='cg2-stage3-materials-'))
        for path, data in updates.items():
            saved = backup / path
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / path, saved)
            (ROOT / path).write_text(data, encoding='utf-8', newline='\n')
        print(f'Backup: {backup}')
        print('Updated stage 3 material and lighting files only.')


if __name__ == '__main__':
    main()
