"""現行ステージのギミックを、配置座標と固有IDを除いてプリセット登録する。"""
import copy
import json
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    # シーンは読み取りだけにし、既存プリセットを退避してから追加する。
    scene = json.loads((ROOT / 'Resources/json/3Dobject/stage2_object.json').read_text(encoding='utf-8-sig'))
    objects = {o['name']: o for o in scene['objects']}
    path = ROOT / 'Resources/json/preset/presets.json'
    original = path.read_bytes()
    presets = json.loads(original.decode('utf-8-sig'))
    prefix = 'Stage2_Route_'
    added = []

    def node(name):
        # 子のローカル姿勢を保持し、配置するたび新規GUIDが生成される形式にする。
        source = objects[name]
        result = copy.deepcopy(source)
        for key in ('guid', 'parentGuid', 'parentName', 'prefab'):
            result.pop(key, None)
        result.update(myEventID=-1, targetID=-1, eventID=0, isLocked=False)
        children = [node(o['name']) for o in scene['objects'] if o.get('parentName') == name]
        if children:
            result['children'] = children
        return result

    def position(n, p):
        n['position'] = list(p)
        n['translate'] = list(p)

    def register(key, label, source, description, supports=()):
        # 支持部は動く部品の子ではなく、固定ルート直下の兄弟として登録する。
        part = node(source)
        origin = objects[source]['position']
        position(part, [0, 0, 0])
        if supports:
            root = dict(name=label, type='Gimmick', gimmickType='', modelName='', isVisible=False,
                        isStatic=False, collisionAttribute=0, collisionMask=0,
                        scale=[1, 1, 1], rotation=[0, 0, 0], rotate=[0, 0, 0],
                        quaternion=[0, 0, 0, 1], children=[part])
            position(root, [0, 0, 0])
            for support in supports:
                child = node(prefix + 'Mechanism_' + support)
                position(child, [a-b for a, b in zip(objects[prefix+'Mechanism_'+support]['position'], origin)])
                root['children'].append(child)
        else:
            root = part
        root.update(presetDisplayName=label, presetDescription=description,
                    presetCollection='Stage2', presetSection='現行コース・配置用ギミック',
                    presetSectionOrder=800, presetUsage='ギミック', presetSourceObject=source,
                    name=label)
        key = 'Stage2/RouteKit/' + key
        presets[key] = root
        added.append(key)

    register('SinkLong', '現行コース：沈む岩・長い直線', prefix+'02_LongSinkingRun', '乗ると沈降。速度・予兆時間・復帰はInspectorで調整。')
    register('SinkWide', '現行コース：沈む岩・幅広', prefix+'10_LongBasaltRun', '幅広の沈降区間。水面ではなくマグマ面に合わせて配置。')
    register('SinkTriangle', '現行コース：沈む岩・三角', prefix+'15_TriangleTurn', '三角形の形状判定付き。岩同士を組み合わせてコーナーに使用。')
    register('SinkFinal', '現行コース：沈む岩・長距離', prefix+'15_FinalLongSink', '終盤区間の長い沈降床。')
    register('Crumble', '現行コース：崩れる岩', prefix+'Star2_Crumble0', '踏むとひび割れて崩落。予兆と復帰時間をInspectorで調整。')
    register('Roller', '現行コース：回転円柱・支持部付き', prefix+'11_RollingBasalt_Mount', '乗っている間だけ回転。固定軸・描画・軽量な形状判定を分離。', ('RollerLeft', 'RollerRight'))
    for mode, label, supports in (
        ('Lift', '上下移動', ('LiftLeft', 'LiftRight')),
        ('Slide', '横往復', ('SlideLeft', 'SlideRight', 'SlideRail')),
        ('Spin', '水平回転', ('SpinAxle',)),
        ('Swing', '振り子', ('SwingAxle',)),
    ):
        register('Spike'+mode, '現行コース：棘バー・'+label, prefix+'Bar_'+mode,
                 '常時再生。GhostRecorderで動作と条件を調整。支持部は固定ルートの子。', supports)
    register('Mortar', '現行コース：火山大砲', prefix+'Mortar', '追尾弾道の大砲。射程・間隔・予兆をInspectorで調整。')
    geyser = next(o['name'] for o in scene['objects'] if o.get('gimmickType') == 'MagmaGeyser')
    register('Geyser', '現行コース：マグマ噴射', geyser, '噴射の周期・高さ・予兆をInspectorで調整。噴射口の高さをマグマ面へ合わせる。')

    def validate(n):
        assert 'guid' not in n and 'parentGuid' not in n and 'parentName' not in n
        recorder = n.get('components', {}).get('PathMover', {})
        record = n.get('recorder', {}).get('recordPathName') or recorder.get('path', '')
        if record:
            assert (ROOT / 'Resources/json/animation' / (record+'.json')).is_file(), record
        for child in n.get('children', []):
            validate(child)
    for key in added:
        validate(presets[key])
    backup = ROOT / 'backups/stage2' / datetime.now().strftime('%Y%m%d_%H%M%S_before_route_presets')
    backup.mkdir(parents=True)
    (backup / path.name).write_bytes(original)
    assert path.read_bytes() == original, 'プリセットが他の操作で変更されました'
    path.write_text(json.dumps(presets, ensure_ascii=False, indent=4)+'\n', encoding='utf-8')
    print('Registered:', len(added))
    print('Backup:', backup)


if __name__ == '__main__':
    main()
