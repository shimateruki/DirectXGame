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

/// <summary>ゲーム内アクションと入力デバイスの割り当てを保存、読み込み、編集する。</summary>
class KeyConfig : public IEditable {
public:
    /// <summary>共通利用するキー設定インスタンスを取得する。</summary>
    static KeyConfig* GetInstance();

    /// <summary>既定のアクション割り当てを準備する。</summary>
    void Initialize();

    /// <summary>JSONからキー設定を読み込む。</summary>
    void Load(const std::string& filepath = "Resources/json/key/keyconfig.json");

    /// <summary>現在のキー設定をJSONへ保存する。</summary>
    bool Save(const std::string& filepath = "Resources/json/key/keyconfig.json");

    /// <summary>キー、マウス、パッド割り当てを編集するUIを描画する。</summary>
    void DrawImGui() override;
    std::string GetName() override { return "Key Configuration"; }

    // アクション名からデータを取得する関数群
    int GetKeyCode(const std::string& actionName) const;
    WORD GetPadCode(const std::string& actionName) const;

    // パッドの数値を「A」や「RB」などの文字列に変換する
    std::string GetKeyName(int keyCode) const;

    /// <summary>ゲームパッドのボタンコードに対応する表示名を返す。</summary>
    std::string GetPadName(WORD padCode) const;

    /// <summary>アクション名に対応する入力割り当てを取得する。</summary>
    const BindData* GetBindData(const std::string& actionName) const;
    /// <summary>指定キーがいずれかのゲーム操作へ割り当てられているか返す。</summary>
    bool IsKeyAssigned(int keyCode) const;

    // ゲーム内設定はコピー上で編集し、適用時だけ既存アクションを書き換えます。
    std::unordered_map<std::string, BindData> GetBindings() const { return bindings_; }

    /// <summary>操作名に対応する入力割り当てを変更する。対象がないか値が不正ならfalse。</summary>
    bool SetBinding(const std::string& actionName, const BindData& binding);
private:
    KeyConfig() = default;
    ~KeyConfig() = default;
    KeyConfig(const KeyConfig&) = delete;
    KeyConfig& operator=(const KeyConfig&) = delete;

private:
    // ：辞書の中身を int から BindData に変更！
    std::unordered_map<std::string, BindData> bindings_;

    // 入力割り当ての編集中に、入力待ちのアクションとデバイスを保持する。
    std::string waitingForInputAction_ = "";

    /// <summary>キー割り当てUIが待ち受ける入力デバイス。</summary>
    enum class WaitMode { None, Key, Pad } waitMode_ = WaitMode::None;

    char newActionName_[64] = "";
};
