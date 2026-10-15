#define NOMINMAX
#include "KeyConfig.h"
#include <dinput.h>
#include <Xinput.h>

KeyConfig* KeyConfig::GetInstance() {
    // 入力割当をゲーム全体で共有し、シーン切替で消さない。
    static KeyConfig instance;
    return &instance;
}

void KeyConfig::Initialize() {
    // 再初期化でも独自アクションが残らないよう、固定の操作表を作り直す。
    bindings_.clear();
    bindings_["Forward"] = { 0x11, -1, XINPUT_GAMEPAD_DPAD_UP };       // W / 上十字
    bindings_["Backward"] = { 0x1F, -1, XINPUT_GAMEPAD_DPAD_DOWN };    // S / 下十字
    bindings_["Left"] = { 0x1E, -1, XINPUT_GAMEPAD_DPAD_LEFT };        // A / 左十字
    bindings_["Right"] = { 0x20, -1, XINPUT_GAMEPAD_DPAD_RIGHT };      // D / 右十字
    bindings_["Jump"] = { 0x39, -1, XINPUT_GAMEPAD_A };                // Space / Aボタン
    bindings_["Attack"] = { 0x2C, -1, XINPUT_GAMEPAD_X };              // Z / Xボタン
    bindings_["Dash"] = { 0x2A, -1, XINPUT_GAMEPAD_RIGHT_SHOULDER };   // L-Shift / RB

    bindings_["Ability"] = { 0x12, -1, XINPUT_GAMEPAD_Y };
    bindings_["MorphRelease"] = { 0x13, -1, XINPUT_GAMEPAD_B };        // R / Bボタン

    bindings_["LockOn"] = { DIK_Q, -1, XINPUT_GAMEPAD_RIGHT_THUMB };
    bindings_["LockOnNext"] = { DIK_G, -1, XINPUT_GAMEPAD_LEFT_THUMB };
    bindings_["PrimaryAttack"] = { 0, 0, XINPUT_GAMEPAD_X };
    bindings_["SecondaryAction"] = { 0, 1, XINPUT_GAMEPAD_LEFT_SHOULDER };
    // 保存済みキー設定は読み込まず、全シーンで固定操作を使用する。
}

int KeyConfig::GetKeyCode(const std::string& actionName) const {
    // 未登録アクションはキーなしを意味する0で返す。
    auto it = bindings_.find(actionName);
    return (it != bindings_.end()) ? it->second.keyCode : 0;
}

WORD KeyConfig::GetPadCode(const std::string& actionName) const {
    // 未登録アクションのゲームパッド割当は空にする。
    auto it = bindings_.find(actionName);
    return (it != bindings_.end()) ? it->second.padCode : 0;
}

// =================================================================
// 数値のキーコードを「A」や「Space」などの分かりやすい文字列に変換する（完全版）
// =================================================================
std::string KeyConfig::GetKeyName(int keyCode) const {
    if (keyCode == 0) return "None";

    switch (keyCode) {
        // --- メイン文字 ---
    case 0x01: return "Esc";
    case 0x02: return "1"; case 0x03: return "2"; case 0x04: return "3"; case 0x05: return "4";
    case 0x06: return "5"; case 0x07: return "6"; case 0x08: return "7"; case 0x09: return "8";
    case 0x0A: return "9"; case 0x0B: return "0";
    case 0x0C: return "-"; case 0x0D: return "="; case 0x0E: return "Backspace";
    case 0x0F: return "Tab";
    case 0x10: return "Q"; case 0x11: return "W"; case 0x12: return "E"; case 0x13: return "R";
    case 0x14: return "T"; case 0x15: return "Y"; case 0x16: return "U"; case 0x17: return "I";
    case 0x18: return "O"; case 0x19: return "P"; case 0x1A: return "["; case 0x1B: return "]";
    case 0x1C: return "Enter"; case 0x1D: return "L-Ctrl";
    case 0x1E: return "A"; case 0x1F: return "S"; case 0x20: return "D"; case 0x21: return "F";
    case 0x22: return "G"; case 0x23: return "H"; case 0x24: return "J"; case 0x25: return "K";
    case 0x26: return "L"; case 0x27: return ";"; case 0x28: return "'"; case 0x29: return "`";
    case 0x2A: return "L-Shift"; case 0x2B: return "\\";
    case 0x2C: return "Z"; case 0x2D: return "X"; case 0x2E: return "C"; case 0x2F: return "V";
    case 0x30: return "B"; case 0x31: return "N"; case 0x32: return "M";
    case 0x33: return ","; case 0x34: return "."; case 0x35: return "/"; case 0x36: return "R-Shift";
    case 0x37: return "Numpad *"; case 0x38: return "L-Alt"; case 0x39: return "Space";
    case 0x3A: return "Caps Lock";

        // --- ファンクションキー ---
    case 0x3B: return "F1";  case 0x3C: return "F2";  case 0x3D: return "F3";  case 0x3E: return "F4";
    case 0x3F: return "F5";  case 0x40: return "F6";  case 0x41: return "F7";  case 0x42: return "F8";
    case 0x43: return "F9";  case 0x44: return "F10"; case 0x57: return "F11"; case 0x58: return "F12";

        // --- テンキー関連 ---
    case 0x45: return "Num Lock"; case 0x46: return "Scroll Lock";
    case 0x47: return "Num 7"; case 0x48: return "Num 8"; case 0x49: return "Num 9"; case 0x4A: return "Num -";
    case 0x4B: return "Num 4"; case 0x4C: return "Num 5"; case 0x4D: return "Num 6"; case 0x4E: return "Num +";
    case 0x4F: return "Num 1"; case 0x50: return "Num 2"; case 0x51: return "Num 3"; case 0x52: return "Num 0";
    case 0x53: return "Num ."; case 0x9C: return "Num Enter"; case 0xB5: return "Num /";

        // --- 特殊・移動系 ---
    case 0x9D: return "R-Ctrl";
    case 0xB8: return "R-Alt";
    case 0xC7: return "Home";     case 0xC8: return "Up Arrow";   case 0xC9: return "PgUp";
    case 0xCB: return "Left Arrow"; case 0xCD: return "Right Arrow";
    case 0xCF: return "End";      case 0xD0: return "Down Arrow"; case 0xD1: return "PgDn";
    case 0xD2: return "Insert";   case 0xD3: return "Delete";

        // --- その他(メディア等) ---
    case 0xDB: return "L-Win"; case 0xDC: return "R-Win"; case 0xDD: return "Apps";
    }

    // リストにない場合は内部コードを表示
    char hex[16];
    sprintf_s(hex, "0x%02X", keyCode);
    return "Unknown (" + std::string(hex) + ")";
}

std::string KeyConfig::GetPadName(WORD padCode) const {
    // 画面に出せない未知のコードも数値付きで表示し、割当の確認を可能にする。
    if (padCode == 0) return "None";
    switch (padCode) {
    case XINPUT_GAMEPAD_A: return "A";
    case XINPUT_GAMEPAD_B: return "B";
    case XINPUT_GAMEPAD_X: return "X";
    case XINPUT_GAMEPAD_Y: return "Y";
    case XINPUT_GAMEPAD_RIGHT_SHOULDER: return "RB";
    case XINPUT_GAMEPAD_LEFT_SHOULDER:  return "LB";
    case XINPUT_GAMEPAD_DPAD_UP:    return "D-Up";
    case XINPUT_GAMEPAD_DPAD_DOWN:  return "D-Down";
    case XINPUT_GAMEPAD_DPAD_LEFT:  return "D-Left";
    case XINPUT_GAMEPAD_DPAD_RIGHT: return "D-Right";
    case XINPUT_GAMEPAD_START: return "Start";
    case XINPUT_GAMEPAD_BACK:  return "Back";
    case XINPUT_GAMEPAD_LEFT_THUMB:  return "L3 (Stick)";
    case XINPUT_GAMEPAD_RIGHT_THUMB: return "R3 (Stick)";
    }
    return "Pad:" + std::to_string(padCode);
}

// =================================================================
// 固定操作表の読み取り
// =================================================================
const BindData* KeyConfig::GetBindData(const std::string& actionName) const {
    // 呼び出し側へ所有権を渡さず、登録済みの割当だけ参照させる。
    auto it = bindings_.find(actionName);
    if (it != bindings_.end()) {
        return &it->second; // データのポインタを返す
    }
    return nullptr; // 見つからない場合
}

bool KeyConfig::IsKeyAssigned(int keyCode) const {
    // エディターのショートカットと固定ゲーム操作が競合しないよう調べる。
    for (const auto& [action, binding] : bindings_) {
        if (binding.keyCode == keyCode) return true;
    }
    return false;
}
