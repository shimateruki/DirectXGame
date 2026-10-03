"""設定画面の日本語ラベルを透過PNGとして再生成する。装飾ではなく文字だけを出力する。"""
from pathlib import Path
import re
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Resources/sprite/ui/settings/text'
FONT = ImageFont.truetype('C:/Windows/Fonts/meiryob.ttc', 40)
LABELS = {
    'invert_x': '左右の反転', 'invert_y': '上下の反転', 'on': '反転する', 'off': '通常',
    'title': 'オプション', 'sound': 'サウンド', 'controls': '操作設定', 'camera': 'カメラ', 'display': '画面',
    'dirty': '未適用の変更あり', 'clean': '変更なし', 'bgm': 'BGM 音量', 'se': 'SE 音量',
    'sensitivity': 'カメラ感度', 'device': '設定する入力機器', 'keyboard': 'キーボード / マウス', 'pad': 'ゲームパッド',
    'window_mode': '表示モード', 'resolution': 'ウィンドウ解像度', 'windowed': 'ウィンドウ', 'borderless': 'ボーダーレス',
    'desktop': 'モニターに合わせる', 'left': '‹', 'right': '›', 'times': '×', 'change': '変更', 'scroll': 'ホイールでスクロール',
    'back': '戻る', 'reset': 'このカテゴリを初期化', 'apply': '変更を適用',
    'help_sound': '音量はその場で試聴できます。保存するには「変更を適用」を選んでください。',
    'help_controls': '項目を選んで決定 → キーやボタンを押して登録。重複時は入れ替えを確認します。',
    'help_camera': 'カメラの回転速度を調整します。適用後にゲームへ反映されます。',
    'help_display': 'アプリの表示設定です。変更後15秒以内に確認しない場合は元に戻ります。',
    'hints': 'Tab / LB・RB：カテゴリ　 ↑↓：選択　 ←→：調整　 Enter / A：決定　 Esc / B：戻る　 マウス操作対応',
    'confirm_discard': '未適用の変更を破棄して戻りますか？', 'confirm_reset': 'このカテゴリを初期値に戻しますか？',
    'capture': '割り当てるキー・ボタンを押してください', 'capture_help': '決定を一度離して入力。Esc / パッドBackでキャンセルできます。',
    'conflict': '使用中の操作と割り当てを入れ替えますか？', 'confirm_display': 'この表示設定を維持しますか？',
    'timeout': '確認しないと元の表示へ戻ります。残り秒数：', 'yes': '確定する', 'cancel': 'キャンセル',
    'reserved': 'この入力はメニュー操作用のため登録できません。', 'saved': '変更を保存しました。',
    'display_error': 'この解像度を使用できません。小さい解像度を選んでください。',
    'display_restored': '表示設定を元に戻しました。他の変更はまだ適用されていません。',
    'save_error': '設定を保存できませんでした。保存先の空き容量・権限を確認してください。',
}
for i, name in enumerate(('前に移動','後ろに移動','左に移動','右に移動','ジャンプ','攻撃 / 投げる','引っ張り / コピー防御',
                          'コピー特殊技','コピー解除','ロックオン','ロックオン切替','通常攻撃（補助入力）')):
    LABELS[f'action_{i}'] = name
source = (ROOT/'engine/system/input/KeyConfig.cpp').read_text(encoding='utf-8-sig')
key_names = {int(code,16): text for code,text in re.findall(r'case (0x[0-9A-Fa-f]+): return "([^"\\]*)";',source)}
for key in range(256): LABELS[f'key_{key}'] = '未設定' if key==0 else key_names.get(key, f'Key {key}')
for key,name in {0:'未設定',1:'十字 上',2:'十字 下',4:'十字 左',8:'十字 右',16:'Start',32:'Back',64:'LS 押し込み',128:'RS 押し込み',256:'LB',512:'RB',4096:'A',8192:'B',16384:'X',32768:'Y'}.items():
    LABELS[f'pad_{key}'] = name
for i,name in enumerate(('マウス 左','マウス 右','マウス 中','マウス X1')): LABELS[f'mouse_{i}']=name
for i in range(10): LABELS[f'digit_{i}']=str(i)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    for name,text in LABELS.items():
        box=FONT.getbbox(text);w=box[2]-box[0]+8;h=52
        image=Image.new('RGBA',(w,h),(0,0,0,0))
        ImageDraw.Draw(image).text((4-box[0],(h-(box[3]-box[1]))/2-box[1]),text,font=FONT,fill=(255,255,255,255))
        image.save(OUT/(name+'.png'))
    print(f'Generated {len(LABELS)} transparent text labels.')

if __name__=='__main__': main()
