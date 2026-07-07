#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// nlohmann::json を使用
#include "json.hpp" 
#include "IEditable.h" 
#include <windows.h> // WORD型(パッド用)を使うために追加

// ==================================================
// ：1つのアクションにつき「キー」と「パッド」両方を保持する構造体
// ==================================================
// BindDataは、1つのアクションに割り当てるキーボード、マウス、パッド入力をまとめます。
struct BindData {
    int keyCode = 0;   // キーボードのキー (DIK_***)
    int mouseButton = -1;
    WORD padCode = 0;  // コントローラーのボタン (XINPUT_GAMEPAD_***)
};

// KeyConfigは、ゲーム内アクションと入力デバイスの割り当てを保存、読み込み、編集します。
class KeyConfig : public IEditable {
public:
        // 共通利用するキー設定インスタンスを取得します。
static KeyConfig* GetInstance();
        // デフォルトのアクション割り当てを準備します。
void Initialize();
        // JSONからキー設定を読み込みます。
void Load(const std::string& filepath = "Resources/json/key/keyconfig.json");
        // 現在のキー設定をJSONへ保存します。
void Save(const std::string& filepath = "Resources/json/key/keyconfig.json");

        // エディタ上でキー、マウス、パッド割り当てを編集するUIを描画します。
void DrawImGui() override;
    std::string GetName() override { return "Key Configuration"; }

    // アクション名からデータを取得する関数群
    int GetKeyCode(const std::string& actionName) const;
    WORD GetPadCode(const std::string& actionName) const; // ★追加：パッドコード取得

    // パッドの数値を「A」や「RB」などの文字列に変換する
    std::string GetKeyName(int keyCode) const;
    std::string GetPadName(WORD padCode) const;
        // アクション名に対応する入力割り当てを取得します。
const BindData* GetBindData(const std::string& actionName) const;
private:
    KeyConfig() = default;
    ~KeyConfig() = default;
    KeyConfig(const KeyConfig&) = delete;
    KeyConfig& operator=(const KeyConfig&) = delete;

private:
    // ：辞書の中身を int から BindData に変更！
    std::unordered_map<std::string, BindData> bindings_;

    // ImGuiエディタ用：現在変更待ちのアクションと、待機しているデバイス(キーかパッドか)
    std::string waitingForInputAction_ = "";
    enum class WaitMode { None, Key, Pad } waitMode_ = WaitMode::None; // ★追加

    char newActionName_[64] = "";
};