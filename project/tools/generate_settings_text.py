"""設定画面の日本語ラベルを透過PNGとして再生成する。装飾ではなく文字だけを出力する。"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Resources/sprite/ui/settings/text'
FONT = ImageFont.truetype('C:/Windows/Fonts/meiryob.ttc', 40)
LABELS = {
    'reset_all': '初期値に戻す',
    'confirm_reset_all': '音量とカメラ感度を初期値に戻しますか？',
    'key_hint': '↑↓ 選択    ←→ 調整    SPACE 決定    TAB 戻る',
    'pad_hint': '十字キー 選択・調整    A 決定    B 戻る',
    'invert_x': '左右の反転',
    'invert_y': '上下の反転',
    'on': '反転する',
    'off': '通常',
    'title': 'オプション',
    'sound': 'サウンド',
    'camera': 'カメラ',
    'display': '画面',
    'dirty': '未適用の変更あり',
    'clean': '変更なし',
    'bgm': 'BGM 音量',
    'se': 'SE 音量',
    'sensitivity': 'カメラ感度',
    'window_mode': '表示モード',
    'resolution': 'ウィンドウ解像度',
    'windowed': 'ウィンドウ',
    'borderless': 'ボーダーレス',
    'desktop': 'モニターに合わせる',
    'left': '‹',
    'right': '›',
    'times': '×',
    'back': '戻る',
    'reset': 'このカテゴリを初期化',
    'apply': '変更を適用',
    'help_sound': '音量はその場で試聴できます。保存するには「変更を適用」を選んでください。',
    'help_camera': 'カメラの回転速度を調整します。適用後にゲームへ反映されます。',
    'help_display': 'アプリの表示設定です。変更後15秒以内に確認しない場合は元に戻ります。',
    'hints': 'Tab / LB・RB：カテゴリ\u3000 ↑↓：選択\u3000 ←→：調整\u3000 Enter / A：決定\u3000 Esc / B：戻る\u3000 マウス操作対応',
    'confirm_discard': '未適用の変更を破棄して戻りますか？',
    'confirm_reset': 'このカテゴリを初期値に戻しますか？',
    'confirm_display': 'この表示設定を維持しますか？',
    'timeout': '確認しないと元の表示へ戻ります。残り秒数：',
    'yes': '確定する',
    'cancel': 'キャンセル',
    'saved': '変更を保存しました。',
    'display_error': 'この解像度を使用できません。小さい解像度を選んでください。',
    'display_restored': '表示設定を元に戻しました。他の変更はまだ適用されていません。',
    'save_error': '設定を保存できませんでした。保存先の空き容量・権限を確認してください。',
}
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
