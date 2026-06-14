from __future__ import annotations

import math
from pathlib import Path
from typing import Callable, Iterable, List, Sequence, Tuple

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "docs"
PAGE_DIR = OUT_DIR / "game_spec_pages"
PDF_PATH = OUT_DIR / "LE3B_13_シマ_テルキ_仕様書_方針.pdf"

PAGE_W = 1920
PAGE_H = 1080

FONT_REGULAR = Path(r"C:\Windows\Fonts\YuGothR.ttc")
FONT_MEDIUM = Path(r"C:\Windows\Fonts\YuGothM.ttc")
FONT_BOLD = Path(r"C:\Windows\Fonts\YuGothB.ttc")

BG = (31, 31, 31)
FG = (245, 247, 250)
MUTED = (150, 154, 160)
SUBTLE = (86, 91, 98)
ACCENT = (144, 218, 255)
SLIME = (37, 190, 222)
YELLOW = (255, 235, 82)
PINK = (255, 130, 178)
GREEN = (94, 226, 148)
RED = (255, 92, 86)
CARD_BG = (8, 8, 8)
CARD_EDGE = (40, 40, 40)


def font(path: Path, size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(path), size=size)


F_TITLE = font(FONT_BOLD, 84)
F_H1 = font(FONT_BOLD, 58)
F_H2 = font(FONT_BOLD, 34)
F_BODY = font(FONT_MEDIUM, 29)
F_SMALL = font(FONT_MEDIUM, 24)
F_CARD_TITLE = font(FONT_BOLD, 42)
F_CARD_BODY = font(FONT_BOLD, 30)
F_LABEL = font(FONT_BOLD, 22)


def text_size(draw: ImageDraw.ImageDraw, text: str, f: ImageFont.FreeTypeFont) -> Tuple[int, int]:
    box = draw.textbbox((0, 0), text, font=f)
    return box[2] - box[0], box[3] - box[1]


def wrap_text(draw: ImageDraw.ImageDraw, text: str, f: ImageFont.FreeTypeFont, width: int) -> List[str]:
    if not text:
        return [""]

    def keep_punctuation_with_previous(lines: List[str]) -> List[str]:
        no_line_start = "。、，．）」』】〕〉》!?！？"
        for i in range(1, len(lines)):
            while lines[i] and lines[i][0] in no_line_start:
                lines[i - 1] += lines[i][0]
                lines[i] = lines[i][1:]
        return [line for line in lines if line]

    words = text.split(" ")
    if len(words) > 1:
        lines: List[str] = []
        current = ""
        for word in words:
            candidate = word if not current else current + " " + word
            if text_size(draw, candidate, f)[0] <= width:
                current = candidate
            else:
                if current:
                    lines.append(current)
                current = word
        if current:
            lines.append(current)
        return keep_punctuation_with_previous(lines)

    lines = []
    current = ""
    for ch in text:
        candidate = current + ch
        if text_size(draw, candidate, f)[0] <= width:
            current = candidate
        else:
            if current:
                lines.append(current)
            current = ch
    if current:
        lines.append(current)
    return keep_punctuation_with_previous(lines)


def draw_text_block(
    draw: ImageDraw.ImageDraw,
    xy: Tuple[int, int],
    lines: Sequence[str],
    width: int,
    f: ImageFont.FreeTypeFont = F_BODY,
    fill: Tuple[int, int, int] = FG,
    line_gap: int = 10,
) -> int:
    x, y = xy
    for line in lines:
        for wrapped in wrap_text(draw, line, f, width):
            draw.text((x, y), wrapped, font=f, fill=fill)
            y += f.size + line_gap
    return y


def draw_bullets(
    draw: ImageDraw.ImageDraw,
    x: int,
    y: int,
    items: Sequence[str],
    width: int,
    bullet_color: Tuple[int, int, int] = (235, 245, 255),
    f: ImageFont.FreeTypeFont = F_BODY,
) -> int:
    for item in items:
        draw.rounded_rectangle((x, y + 12, x + 15, y + 27), radius=2, fill=bullet_color)
        tx = x + 36
        for i, wrapped in enumerate(wrap_text(draw, item, f, width - 36)):
            draw.text((tx, y), wrapped, font=f, fill=FG)
            y += f.size + 10
            if i == 0:
                pass
        y += 12
    return y


def draw_sub_bullets(
    draw: ImageDraw.ImageDraw,
    x: int,
    y: int,
    heading: str,
    items: Sequence[str],
    width: int,
) -> int:
    draw.text((x, y), heading, font=F_H2, fill=FG)
    y += 48
    return draw_bullets(draw, x + 8, y, items, width - 8, bullet_color=ACCENT, f=F_BODY)


def page_base(title: str, subtitle: str | None = None, page_no: int = 0) -> Tuple[Image.Image, ImageDraw.ImageDraw]:
    img = Image.new("RGB", (PAGE_W, PAGE_H), BG)
    draw = ImageDraw.Draw(img)
    draw.text((76, 76), title, font=F_TITLE, fill=FG)
    if subtitle:
        draw.text((86, 185), subtitle, font=F_H2, fill=ACCENT)
    draw.text((1510, 90), "日本工学院専門学校", font=F_H2, fill=(78, 78, 78))
    draw.text((1510, 135), "デザインカレッジ", font=F_H2, fill=(78, 78, 78))
    if page_no:
        draw.text((1740, 1000), f"{page_no:02}", font=F_SMALL, fill=(110, 110, 110))
    return img, draw


def draw_card(draw: ImageDraw.ImageDraw, box: Tuple[int, int, int, int], title: str) -> None:
    x0, y0, x1, y1 = box
    draw.rounded_rectangle(box, radius=4, fill=CARD_BG, outline=CARD_EDGE, width=2)
    draw.text((x0 + 58, y0 + 58), title, font=F_CARD_TITLE, fill=FG)


def arrow(draw: ImageDraw.ImageDraw, start: Tuple[int, int], end: Tuple[int, int], fill=FG, width: int = 5) -> None:
    draw.line((start, end), fill=fill, width=width)
    ang = math.atan2(end[1] - start[1], end[0] - start[0])
    size = 20
    p1 = (end[0] - math.cos(ang - 0.55) * size, end[1] - math.sin(ang - 0.55) * size)
    p2 = (end[0] - math.cos(ang + 0.55) * size, end[1] - math.sin(ang + 0.55) * size)
    draw.polygon([end, p1, p2], fill=fill)


def slime_icon(draw: ImageDraw.ImageDraw, cx: int, cy: int, scale: float = 1.0, fill=SLIME) -> None:
    w = int(92 * scale)
    h = int(72 * scale)
    draw.ellipse((cx - w, cy - h // 2, cx + w, cy + h), fill=fill, outline=(170, 245, 255), width=max(2, int(4 * scale)))
    top = [(cx - int(58 * scale), cy - int(12 * scale)), (cx, cy - int(92 * scale)), (cx + int(58 * scale), cy - int(12 * scale))]
    draw.polygon(top, fill=fill)
    draw.arc((cx - w, cy - h // 2, cx + w, cy + h), 190, 350, fill=(14, 95, 125), width=max(2, int(4 * scale)))
    eye_r = max(3, int(7 * scale))
    draw.ellipse((cx - int(32 * scale) - eye_r, cy - eye_r, cx - int(32 * scale) + eye_r, cy + eye_r), fill=(20, 45, 100))
    draw.ellipse((cx + int(32 * scale) - eye_r, cy - eye_r, cx + int(32 * scale) + eye_r, cy + eye_r), fill=(20, 45, 100))
    draw.ellipse((cx - int(42 * scale), cy - int(42 * scale), cx - int(8 * scale), cy - int(20 * scale)), fill=(150, 245, 255))


def enemy_icon(draw: ImageDraw.ImageDraw, cx: int, cy: int, scale: float = 1.0, color=(210, 220, 230)) -> None:
    r = int(48 * scale)
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline=color, width=max(2, int(4 * scale)))
    draw.line((cx - r, cy, cx + r, cy), fill=color, width=max(2, int(3 * scale)))
    draw.ellipse((cx - int(13 * scale), cy - int(13 * scale), cx + int(13 * scale), cy + int(13 * scale)), fill=color)


def star(draw: ImageDraw.ImageDraw, cx: int, cy: int, r_outer: int, r_inner: int, fill=YELLOW, outline=None) -> None:
    points = []
    for i in range(10):
        r = r_outer if i % 2 == 0 else r_inner
        a = -math.pi / 2 + i * math.pi / 5
        points.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    draw.polygon(points, fill=fill, outline=outline)


def wire_box(draw: ImageDraw.ImageDraw, cx: int, cy: int, w: int, h: int, color=FG) -> None:
    x0, y0 = cx - w // 2, cy - h // 2
    x1, y1 = cx + w // 2, cy + h // 2
    skew = 52
    draw.line((x0, y0, x1, y0, x1, y1, x0, y1, x0, y0), fill=color, width=3)
    draw.line((x0, y0, x0 + skew, y0 - skew, x1 + skew, y0 - skew, x1, y0), fill=color, width=3)
    draw.line((x1 + skew, y0 - skew, x1 + skew, y1 - skew, x1, y1), fill=color, width=3)
    draw.line((x0 + skew, y0 - skew, x1 + skew, y1 - skew), fill=color, width=2)
    draw.line((x1 + skew, y0 - skew, x0, y1), fill=color, width=2)


def draw_core_loop_card(draw: ImageDraw.ImageDraw, box: Tuple[int, int, int, int]) -> None:
    draw_card(draw, box, "CORE LOOP")
    x0, y0, x1, y1 = box
    steps = [("探索", (x0 + 165, y0 + 260), SLIME), ("捕まえる", (x0 + 395, y0 + 260), PINK), ("能力使用", (x0 + 635, y0 + 260), YELLOW), ("ゴール", (x0 + 395, y0 + 500), GREEN)]
    for label, pos, color in steps:
        draw.ellipse((pos[0] - 70, pos[1] - 70, pos[0] + 70, pos[1] + 70), outline=color, width=5)
        draw.text((pos[0] - text_size(draw, label, F_CARD_BODY)[0] // 2, pos[1] + 96), label, font=F_CARD_BODY, fill=FG)
    slime_icon(draw, *steps[0][1], scale=0.45)
    enemy_icon(draw, *steps[1][1], scale=0.8, color=PINK)
    star(draw, *steps[2][1], 62, 28, fill=YELLOW)
    draw.ellipse((steps[3][1][0] - 58, steps[3][1][1] - 58, steps[3][1][0] + 58, steps[3][1][1] + 58), outline=GREEN, width=8)
    arrow(draw, (x0 + 245, y0 + 260), (x0 + 315, y0 + 260), ACCENT)
    arrow(draw, (x0 + 475, y0 + 260), (x0 + 555, y0 + 260), ACCENT)
    arrow(draw, (x0 + 635, y0 + 350), (x0 + 475, y0 + 455), ACCENT)
    arrow(draw, (x0 + 315, y0 + 455), (x0 + 165, y0 + 350), ACCENT)


def draw_hook_card(draw: ImageDraw.ImageDraw, box: Tuple[int, int, int, int]) -> None:
    draw_card(draw, box, "SLIME STRETCH / CAPTURE")
    x0, y0, _, _ = box
    slime_icon(draw, x0 + 190, y0 + 405, 0.65)
    enemy_icon(draw, x0 + 650, y0 + 300, 1.0, color=FG)
    for i in range(6):
        t = i / 5
        x = x0 + 260 + int((x0 + 585 - (x0 + 260)) * t)
        y = y0 + 385 - int(math.sin(t * math.pi) * 90)
        draw.ellipse((x - 18, y - 18, x + 18, y + 18), fill=SLIME)
    draw.line((x0 + 260, y0 + 385, x0 + 585, y0 + 300), fill=SLIME, width=14)
    arrow(draw, (x0 + 590, y0 + 300), (x0 + 435, y0 + 345), PINK, width=6)
    draw.text((x0 + 115, y0 + 630), "伸ばす", font=F_CARD_BODY, fill=FG)
    draw.text((x0 + 360, y0 + 630), "拘束", font=F_CARD_BODY, fill=FG)
    draw.text((x0 + 595, y0 + 630), "引き寄せ", font=F_CARD_BODY, fill=FG)


def draw_enemy_state_card(draw: ImageDraw.ImageDraw, box: Tuple[int, int, int, int]) -> None:
    draw_card(draw, box, "ENEMY STATE")
    x0, y0, _, _ = box
    states = [
        ("索敵", x0 + 160, y0 + 315, ACCENT),
        ("拘束", x0 + 360, y0 + 315, PINK),
        ("能力", x0 + 560, y0 + 315, YELLOW),
        ("投げ", x0 + 360, y0 + 540, GREEN),
    ]
    for label, x, y, color in states:
        draw.rounded_rectangle((x - 90, y - 48, x + 90, y + 48), radius=24, outline=color, width=5)
        draw.text((x - text_size(draw, label, F_CARD_BODY)[0] // 2, y - 22), label, font=F_CARD_BODY, fill=FG)
    arrow(draw, (x0 + 250, y0 + 315), (x0 + 270, y0 + 315), FG)
    arrow(draw, (x0 + 450, y0 + 315), (x0 + 470, y0 + 315), FG)
    arrow(draw, (x0 + 560, y0 + 370), (x0 + 425, y0 + 500), FG)
    arrow(draw, (x0 + 295, y0 + 500), (x0 + 160, y0 + 370), FG)
    draw.line((x0 + 360, y0 + 385, x0 + 360, y0 + 480), fill=SUBTLE, width=3)


def draw_stage_card(draw: ImageDraw.ImageDraw, box: Tuple[int, int, int, int]) -> None:
    draw_card(draw, box, "STAGE CLEAR FLOW")
    x0, y0, _, _ = box
    for i, (label, color) in enumerate([("入場", ACCENT), ("ギミック", PINK), ("敵能力", YELLOW), ("王冠", GREEN)]):
        x = x0 + 130 + i * 170
        y = y0 + 320
        draw.rounded_rectangle((x - 65, y - 45, x + 65, y + 45), radius=16, outline=color, width=5)
        draw.text((x - text_size(draw, label, F_LABEL)[0] // 2, y - 14), label, font=F_LABEL, fill=FG)
        if i < 3:
            arrow(draw, (x + 70, y), (x + 105, y), FG, width=4)
    star(draw, x0 + 640, y0 + 520, 64, 28, fill=YELLOW, outline=FG)
    draw.text((x0 + 350, y0 + 590), "クリアで次の島を解放", font=F_CARD_BODY, fill=FG)


def draw_enemy_ability_card(draw: ImageDraw.ImageDraw, box: Tuple[int, int, int, int]) -> None:
    draw_card(draw, box, "CAPTURED ABILITIES")
    x0, y0, _, _ = box
    rows = [
        ("コウモリ", "滑空", ACCENT),
        ("目玉", "溜めビーム", YELLOW),
        ("ボマー", "ボム投げ", RED),
        ("巨大スライム", "分裂誘導", GREEN),
    ]
    for i, (name, ability, color) in enumerate(rows):
        y = y0 + 170 + i * 115
        draw.rounded_rectangle((x0 + 70, y, x0 + 710, y + 78), radius=28, outline=color, width=4)
        draw.text((x0 + 105, y + 18), name, font=F_CARD_BODY, fill=FG)
        draw.text((x0 + 430, y + 18), ability, font=F_CARD_BODY, fill=color)


def draw_ui_card(draw: ImageDraw.ImageDraw, box: Tuple[int, int, int, int]) -> None:
    draw_card(draw, box, "UI / SAVE / RESULT")
    x0, y0, _, _ = box
    draw.rounded_rectangle((x0 + 85, y0 + 175, x0 + 695, y0 + 285), radius=55, fill=(255, 182, 202), outline=(255, 245, 170), width=5)
    draw.text((x0 + 135, y0 + 200), "ファイル1", font=F_CARD_BODY, fill=(255, 255, 255))
    slime_icon(draw, x0 + 410, y0 + 230, 0.35)
    draw.text((x0 + 520, y0 + 205), "× 3", font=F_CARD_BODY, fill=(255, 255, 190))
    draw.rounded_rectangle((x0 + 85, y0 + 340, x0 + 695, y0 + 450), radius=55, outline=(120, 155, 200), width=4)
    draw.text((x0 + 135, y0 + 365), "ファイル2", font=F_CARD_BODY, fill=(160, 190, 225))
    draw.text((x0 + 125, y0 + 570), "HP / 残機 / コイン / 星を常時表示", font=F_CARD_BODY, fill=FG)


def draw_effect_card(draw: ImageDraw.ImageDraw, box: Tuple[int, int, int, int]) -> None:
    draw_card(draw, box, "HIT FEEDBACK")
    x0, y0, _, _ = box
    wire_box(draw, x0 + 270, y0 + 430, 190, 110)
    for a in range(0, 360, 35):
        r1 = 86
        r2 = 165
        cx, cy = x0 + 410, y0 + 330
        p1 = (cx + math.cos(math.radians(a)) * r1, cy + math.sin(math.radians(a)) * r1)
        p2 = (cx + math.cos(math.radians(a)) * r2, cy + math.sin(math.radians(a)) * r2)
        draw.line((p1, p2), fill=YELLOW if a % 70 == 0 else FG, width=4)
    draw.text((x0 + 520, y0 + 360), "衝撃", font=F_CARD_TITLE, fill=FG)
    draw.text((x0 + 520, y0 + 430), "破片 / 光 / SE", font=F_CARD_BODY, fill=ACCENT)


def draw_tool_card(draw: ImageDraw.ImageDraw, box: Tuple[int, int, int, int]) -> None:
    draw_card(draw, box, "EDITOR SUPPORT")
    x0, y0, _, _ = box
    items = [("Sprite JSON", ACCENT), ("Preset Palette", PINK), ("LOD Tool", YELLOW), ("Text PNG", GREEN)]
    for i, (label, color) in enumerate(items):
        x = x0 + 135 + (i % 2) * 315
        y = y0 + 250 + (i // 2) * 210
        draw.rounded_rectangle((x - 95, y - 70, x + 190, y + 70), radius=18, outline=color, width=5)
        draw.text((x - 55, y - 20), label, font=F_CARD_BODY, fill=FG)


def make_pages() -> List[Image.Image]:
    pages: List[Image.Image] = []

    img, draw = page_base("スライム・アクション仕様書", "ステージクリア型 3D アクションゲーム", 1)
    draw_text_block(draw, (90, 310), [
        "プレイヤーはスライム。身体を伸ばして敵を捕まえ、引き寄せ、拘束中に敵の能力を使いながらステージを攻略する。",
        "敵を倒すだけではなく、敵を一時的な道具として使うことでギミック突破・移動補助・攻撃手段を切り替える。",
    ], 950, F_H2, FG, 18)
    draw_bullets(draw, 120, 560, [
        "ジャンル：ステージクリア型 3D アクション",
        "プレイ感：探索、捕獲、能力活用、ゴール到達",
        "核となる遊び：敵を引き寄せて能力を借りる",
        "目標：3ステージ構成を想定し、王冠の力で次のステージを解放する",
    ], 880, ACCENT)
    draw_core_loop_card(draw, (1145, 225, 1790, 900))
    pages.append(img)

    img, draw = page_base("1. 基本ゲームフロー", "探索からゴールまでの一連の体験", 2)
    draw_sub_bullets(draw, 120, 305, "(1) ステージ開始", [
        "プレイヤーはステージ入口付近から開始する。",
        "残機、HP、コイン、スター取得状況を画面左上・右上に表示する。",
    ], 910)
    draw_sub_bullets(draw, 120, 515, "(2) 攻略", [
        "地形ギミック、敵、スイッチ、ゲートを組み合わせて進行する。",
        "フック移動は廃止し、敵の引き寄せ・拘束を中心に遊びを組み立てる。",
    ], 910)
    draw_sub_bullets(draw, 120, 760, "(3) クリア", [
        "ゴール地点の王冠に到達するとステージクリア。",
        "ステージセレクトで次の島やルートを解放する。",
    ], 910)
    draw_stage_card(draw, (1145, 250, 1790, 900))
    pages.append(img)

    img, draw = page_base("2. プレイヤー操作仕様", "スライムの身体を使うアクション", 3)
    draw_sub_bullets(draw, 120, 305, "(1) 移動・ジャンプ", [
        "通常移動、ジャンプ、回避を基本操作とする。",
        "ダッシュパネルなど一部ギミックでは速度上昇と旋回性能低下を発生させる。",
    ], 930)
    draw_sub_bullets(draw, 120, 530, "(2) スライム伸縮", [
        "狙った敵に向かって身体を伸ばし、命中したら拘束状態へ移行する。",
        "敵の引き寄せ、投げ、拘束中能力の使用に派生する。",
    ], 930)
    draw_sub_bullets(draw, 120, 750, "(3) 被弾・死亡", [
        "被弾時は赤点滅ではなく、ノックバックとスライムらしい伸び縮みで表現する。",
        "落下やHP0時は残機演出を挟み、リトライまたはゲームオーバーへ遷移する。",
    ], 930)
    draw_hook_card(draw, (1145, 245, 1790, 900))
    pages.append(img)

    img, draw = page_base("3. 敵拘束と能力使用", "敵を倒す前に利用するシステム", 4)
    draw_sub_bullets(draw, 120, 305, "(1) 拘束成立", [
        "スライムの伸縮が敵に当たると敵を拘束する。",
        "拘束中は敵AIを止め、プレイヤー側の能力入力を受け付ける。",
    ], 930)
    draw_sub_bullets(draw, 120, 530, "(2) 能力使用", [
        "コウモリ：落下速度を下げて滑空できる。",
        "目玉：Eキーで溜め、正面にビームを撃つ。",
        "ボマー：正面へボムを投げる。",
    ], 930)
    draw_sub_bullets(draw, 120, 785, "(3) 投げ・叩きつけ", [
        "投げ距離は短めにし、放物線で地面へ叩きつける。",
        "地面衝突時に衝撃判定を出し、敵や壊せるブロックへダメージを与える。",
    ], 930)
    draw_enemy_state_card(draw, (1145, 245, 1790, 900))
    pages.append(img)

    img, draw = page_base("4. 敵キャラクター仕様", "拘束中能力と通常行動の役割分担", 5)
    draw_sub_bullets(draw, 120, 300, "(1) キノコ", [
        "通常時：索敵範囲内で遠距離攻撃を行う。",
        "役割：距離を取る敵として、引き寄せの必要性を作る。",
    ], 930)
    draw_sub_bullets(draw, 120, 475, "(2) 巨大スライム", [
        "通常の引っ張りでは倒せず、一定量引くと分裂する。",
        "役割：力比べのような拘束演出と、分裂後の複数敵処理を作る。",
    ], 930)
    draw_sub_bullets(draw, 120, 675, "(3) コウモリ・目玉・ボマー", [
        "コウモリは周回飛行と降下攻撃、拘束中は滑空補助。",
        "目玉は距離を保ってビーム、拘束中はプレイヤー正面ビーム。",
        "ボマーは爆弾攻撃、拘束中はボム投げ能力として使う。",
    ], 930)
    draw_enemy_ability_card(draw, (1145, 230, 1790, 900))
    pages.append(img)

    img, draw = page_base("5. ステージ・ギミック仕様", "敵能力と地形を組み合わせる", 6)
    draw_sub_bullets(draw, 120, 300, "(1) 常設ギミック", [
        "沈む床、シーソー床、氷床、ダッシュパネル、一方通行床を配置する。",
        "プレイヤー操作の手触りを変えることで、単調な移動を避ける。",
    ], 930)
    draw_sub_bullets(draw, 120, 515, "(2) イベント連動ギミック", [
        "時限スイッチ床、出現床、水位・マグマ上下、連鎖崩れ床をID連携で制御する。",
        "スイッチは汎用化し、モード設定で複数の用途に使い回す。",
    ], 930)
    draw_sub_bullets(draw, 120, 735, "(3) 演出ギミック", [
        "レーザーは接続ノード同士を線で結び、専用シェーダーで表示する。",
        "ゲートは渦状シェーダーでステージ遷移や解放演出に使う。",
    ], 930)
    draw_stage_card(draw, (1145, 240, 1790, 900))
    pages.append(img)

    img, draw = page_base("6. UI・進行管理仕様", "ステージ選択とプレイ中表示", 7)
    draw_sub_bullets(draw, 120, 300, "(1) タイトル・セーブ", [
        "セーブデータは3つ。ファイルごとに残機、スター、王冠、プレイ時間を表示する。",
        "チュートリアル未クリアなら最初から、クリア済みならステージセレクトへ進む。",
    ], 930)
    draw_sub_bullets(draw, 120, 535, "(2) プレイ中UI", [
        "HPは最大6。マリオギャラクシー風の円形ライフUIで表示する。",
        "残機、コイン、スター取得状況を視認しやすい位置に置く。",
    ], 930)
    draw_sub_bullets(draw, 120, 755, "(3) ポーズ・設定", [
        "ポーズ項目はリトライ、設定、タイトルへ戻る。",
        "設定はBGM、SE、カメラ感度を同一シーン上のオーバーレイとして表示する。",
    ], 930)
    draw_ui_card(draw, (1145, 230, 1790, 900))
    pages.append(img)

    img, draw = page_base("7. 演出・フィードバック仕様", "当たった感と達成感を作る", 8)
    draw_sub_bullets(draw, 120, 300, "(1) 攻撃・投げ演出", [
        "敵を叩きつけた瞬間に地面衝撃波、短いカメラ揺れ、SEを入れる。",
        "爆発や破壊では破片エフェクト、白または黄色の短いフラッシュを重ねる。",
    ], 930)
    draw_sub_bullets(draw, 120, 535, "(2) シェーダー演出", [
        "水、マグマ、炎、レーザー、ゲート、ガラスひび割れを用途別に用意する。",
        "水面やゲートはポリゴンの三角面が見えないように、UV・ノイズ・アルファ処理を調整する。",
    ], 930)
    draw_sub_bullets(draw, 120, 765, "(3) ゲームオーバー", [
        "黒背景、スポットライト、倒れたスライム、スタン表現で演出する。",
        "リトライ時は残機を3へ戻し、ステージ先頭から再開する。",
    ], 930)
    draw_effect_card(draw, (1145, 230, 1790, 900))
    pages.append(img)

    img, draw = page_base("8. 制作ツール・調整方針", "Editorで調整できる範囲を増やす", 9)
    draw_sub_bullets(draw, 120, 300, "(1) JSON管理", [
        "ゲーム中UI、タイトル、ゲームオーバー、オプションはSprite JSONで配置を調整可能にする。",
        "コード固定のUIは減らし、位置・サイズ・色・選択演出をEditorで調整できるようにする。",
    ], 930)
    draw_sub_bullets(draw, 120, 535, "(2) 効率化ツール", [
        "テキストPNG、DDSキャッシュ、LODを制作補助に使う。",
        "重い処理は作業を止めない形で事前変換または明示的な生成ボタンに寄せる。",
    ], 930)
    draw_sub_bullets(draw, 120, 755, "(3) 仕上げ優先度", [
        "まず遊びの核となる敵拘束・能力使用・ステージクリア導線を安定させる。",
        "その後、シェーダーとUI演出で見た目の完成度を上げる。",
    ], 930)
    draw_tool_card(draw, (1145, 230, 1790, 900))
    pages.append(img)

    return pages


def save_pdf(pages: Sequence[Image.Image]) -> None:
    OUT_DIR.mkdir(exist_ok=True)
    PAGE_DIR.mkdir(exist_ok=True)
    page_paths = []
    for i, page in enumerate(pages, start=1):
        path = PAGE_DIR / f"page_{i:02}.png"
        page.save(path, "PNG")
        page_paths.append(path)

    pdf_pages = [Image.open(p).convert("RGB") for p in page_paths]
    pdf_pages[0].save(PDF_PATH, save_all=True, append_images=pdf_pages[1:], resolution=150.0)


def main() -> None:
    pages = make_pages()
    save_pdf(pages)
    print(PDF_PATH)


if __name__ == "__main__":
    main()
