#define NOMINMAX
#include "KeyConfig.h"
#include "InputManager.h" 
#include "imgui.h"
#include "DebugConsole.h"
#include <fstream>
#include <iomanip>
#include <filesystem> 
#include "IconsFontAwesome5.h"
using json = nlohmann::json;

KeyConfig* KeyConfig::GetInstance() {
    static KeyConfig instance;
    return &instance;
}

void KeyConfig::Initialize() {
    // 1. デフォルト設定 (キーとパッドの両方を設定！)
    bindings_["Forward"] = { 0x11, XINPUT_GAMEPAD_DPAD_UP };      // W / 上字
    bindings_["Backward"] = { 0x1F, XINPUT_GAMEPAD_DPAD_DOWN };    // S / 下十字
    bindings_["Left"] = { 0x1E, XINPUT_GAMEPAD_DPAD_LEFT };    // A / 左十字
    bindings_["Right"] = { 0x20, XINPUT_GAMEPAD_DPAD_RIGHT };   // D / 右十字
    bindings_["Jump"] = { 0x39, XINPUT_GAMEPAD_A };            // Space / Aボタン
    bindings_["Attack"] = { 0x2C, XINPUT_GAMEPAD_X };            // Z / Xボタン
    bindings_["Dash"] = { 0x2A, XINPUT_GAMEPAD_RIGHT_SHOULDER };// L-Shift / RB

    Load();
}

void KeyConfig::Load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        DebugConsole::GetInstance()->AddLog("KeyConfig: JSONファイルが見つかりません。デフォルトを使用します。");
        return;
    }

    try {
        json j;
        file >> j;

        for (auto& [action, data] : j.items()) {
            // もし古い形式 (ただの数値) で保存されていた場合の互換性維持
            if (data.is_number()) {
                bindings_[action].keyCode = data.get<int>();
                bindings_[action].padCode = 0;
            }
            // 新しい形式 (オブジェクト) 
            else if (data.is_object()) {
                bindings_[action].keyCode = data.value("key", 0);
                bindings_[action].mouseButton = data.value("mouse", -1);
                bindings_[action].padCode = data.value("pad", 0);
            }
        }
        DebugConsole::GetInstance()->AddLog("KeyConfig: キー設定をロードしました");
    }
    catch (const json::exception& e) {
        DebugConsole::GetInstance()->AddLog("KeyConfig: JSON読込エラー: " + std::string(e.what()));
    }
}

void KeyConfig::Save(const std::string& filepath) {
    std::filesystem::path dir = std::filesystem::path(filepath).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    json j;
    // 辞書をJSONに変換（キーとパッドの2つを保存する）
    for (const auto& pair : bindings_) {
        j[pair.first]["key"] = pair.second.keyCode;
        j[pair.first]["mouse"] = pair.second.mouseButton;
        j[pair.first]["pad"] = pair.second.padCode;
    }

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << std::setw(4) << j << std::endl;
        DebugConsole::GetInstance()->AddLog("KeyConfig: キー設定をセーブしました");
    }
    else {
        DebugConsole::GetInstance()->AddLog("KeyConfig: [ERROR] 保存に失敗しました");
    }
}

int KeyConfig::GetKeyCode(const std::string& actionName) const {
    auto it = bindings_.find(actionName);
    return (it != bindings_.end()) ? it->second.keyCode : 0;
}

WORD KeyConfig::GetPadCode(const std::string& actionName) const {
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
// エディタ画面の描画（Inspectorに表示されるUI）
// =================================================================
void KeyConfig::DrawImGui() {


    // ---------------------------------------------------------
    // 1. セーブ＆ロード ボタン
    // ---------------------------------------------------------
    if (ImGui::Button(ICON_FA_SAVE " セーブ (Save)")) {
        Save();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " ロード (Load)")) {
        Load();
    }
    ImGui::Separator();

    // ---------------------------------------------------------
    // 2. 新規アクションの追加エリア
    // ---------------------------------------------------------
    ImGui::Text(ICON_FA_PLUS_CIRCLE " 新規アクションの追加");

    ImGui::PushItemWidth(180.0f); // テキストボックスの幅
    ImGui::InputText("##NewActionName", newActionName_, sizeof(newActionName_));
    ImGui::PopItemWidth();

    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_PLUS " 追加")) {
        std::string newName(newActionName_);
        // 空文字ではなく、まだ登録されていない名前なら辞書に追加！
        if (!newName.empty() && bindings_.find(newName) == bindings_.end()) {
            bindings_[newName] = { 0, 0 }; // 初期値（未設定）で登録
            newActionName_[0] = '\0';      // 入力欄を綺麗にクリア
        }
    }
    ImGui::Separator();

    // ---------------------------------------------------------
    // 3. キーバインド設定一覧（テーブル表示）
    // ---------------------------------------------------------
    // テーブルの設定（4列、境界線あり、背景色を交互に変える）
    if (ImGui::BeginTable("ConfigTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {

        // カラム(列)のヘッダー設定とアイコン
        ImGui::TableSetupColumn(ICON_FA_RUNNING " アクション", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn(ICON_FA_KEYBOARD " キーボード", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn(ICON_FA_GAMEPAD " パッド", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn(ICON_FA_TRASH_ALT " 削除", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableHeadersRow();

        InputManager* input = InputManager::GetInstance();
        std::string actionToRemove = ""; // 後で削除するための予約メモ

        // 登録されているすべてのアクションをループで表示
        for (auto& pair : bindings_) {
            const std::string& actionName = pair.first;
            BindData& bind = pair.second;

            ImGui::TableNextRow();

            // -------------------------------------------------
            // ① アクション名
            // -------------------------------------------------
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", actionName.c_str());

    
          // -------------------------------------------------
            // ② キーボード / マウス設定 (ボタンを押して割り当て)
            // -------------------------------------------------
            ImGui::TableSetColumnIndex(1);
            if (waitMode_ == WaitMode::Key && waitingForInputAction_ == actionName) {
                // 入力待ち状態：キーボードとマウスの両方を監視！
                ImGui::Button((ICON_FA_SPINNER " 入力待ち...##" + actionName).c_str(), ImVec2(-1, 0));

                // A. キーボードチェック
                const auto& pressedKeys = input->GetPressedKeys();
                if (!pressedKeys.empty()) {
                    bind.keyCode = pressedKeys[0];
                    bind.mouseButton = -1; // キーが優先されたのでマウス設定を消す
                    waitMode_ = WaitMode::None;
                    waitingForInputAction_ = "";
                }

                // B. マウスボタンチェック (★追加)
                int mBtn = input->GetPressedMouseButton();
                if (mBtn != -1) {
                    bind.mouseButton = mBtn;
                    bind.keyCode = 0; // マウスが優先されたのでキー設定を消す
                    waitMode_ = WaitMode::None;
                    waitingForInputAction_ = "";
                }
            }
            else {
                // 通常表示：マウスかキーボード、設定されている方を表示
                std::string label;
                if (bind.mouseButton != -1) {
                    // マウスボタンの名前を表示 (例: Mouse:Left)
                    const char* mNames[] = { "Left", "Right", "Middle", "Side" };
                    label = "Mouse:" + std::string(mNames[bind.mouseButton]);
                }
                else {
                    label = GetKeyName(bind.keyCode);
                }

                std::string btnLabel = "[ " + label + " ] ##Key_" + actionName;
                if (ImGui::Button(btnLabel.c_str(), ImVec2(-1, 0))) {
                    waitingForInputAction_ = actionName;
                    waitMode_ = WaitMode::Key;
                }
            }

            // -------------------------------------------------
            // ③ ゲームパッド設定（プルダウンで選択）
            // -------------------------------------------------
            ImGui::TableSetColumnIndex(2);

            // プルダウン用のデータ群
            const WORD padValues[] = {
                0,
                XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
                XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_LEFT_SHOULDER,
                XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
                XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK,
                XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB
            };
            const char* padNames[] = {
                "None",
                "A", "B", "X", "Y",
                "RB", "LB",
                "D-Up", "D-Down", "D-Left", "D-Right",
                "Start", "Back",
                "L3 (Stick)", "R3 (Stick)"
            };

            // 現在設定されているボタンの「リスト内のインデックス番号」を探す
            int currentItem = 0;
            for (int i = 0; i < IM_ARRAYSIZE(padValues); i++) {
                if (bind.padCode == padValues[i]) {
                    currentItem = i;
                    break;
                }
            }

            ImGui::PushItemWidth(-1); // セル幅いっぱいに広げる
            std::string comboLabel = "##Combo_" + actionName;
            if (ImGui::Combo(comboLabel.c_str(), &currentItem, padNames, IM_ARRAYSIZE(padNames))) {
                // プレイヤーが別の項目を選んだら、その場で値を更新！
                bind.padCode = padValues[currentItem];
            }
            ImGui::PopItemWidth();

            // -------------------------------------------------
            // ④ 削除ボタン
            // -------------------------------------------------
            ImGui::TableSetColumnIndex(3);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // ボタンを赤くする

            // アイコンだけの「X」ボタン
            if (ImGui::Button((ICON_FA_TIMES "##Rm_" + actionName).c_str(), ImVec2(-1, 0))) {
                actionToRemove = actionName; // ループの中では直接消さず、名前をメモしておく
            }
            ImGui::PopStyleColor();
        }

        // テーブルの描画終了
        ImGui::EndTable();

        // ---------------------------------------------------------
        // 4. アクションの削除処理（ループが終わった後に安全に行う）
        // ---------------------------------------------------------
        if (!actionToRemove.empty()) {
            bindings_.erase(actionToRemove);

            // もし「入力待ち」にしていたアクションを消した場合は、入力待ちもキャンセルする
            if (waitingForInputAction_ == actionToRemove) {
                waitMode_ = WaitMode::None;
                waitingForInputAction_ = "";
            }
        }
    }
}

const BindData* KeyConfig::GetBindData(const std::string& actionName) const {
    auto it = bindings_.find(actionName);
    if (it != bindings_.end()) {
        return &it->second; // データのポインタを返す
    }
    return nullptr; // 見つからない場合
}