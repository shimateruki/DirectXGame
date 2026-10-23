#pragma once
#include <string>
#include <unordered_map>

#include <windows.h> // WORD型(パッド用)を使うために追加

// ==================================================
// ：1つのアクションにつき「キー」と「パッド」両方を保持する構造体
// ==================================================
// BindDataは、1つのアクションに割り当てるキーボード、マウス、パッド入力をまとめます。
struct BindData {
    int keyCode = 0;   // キーボードのキー (DIK_***)
    // マウスは0が左ボタンとして有効なため、未割当には-1を使う。
    int mouseButton = -1;
    WORD padCode = 0;  // コントローラーのボタン (XINPUT_GAMEPAD_***)
};

/// <summary>ゲーム内アクションの固定入力表。変更・保存・読み込み機能は持たない。</summary>
class KeyConfig {
public:
    /// <summary>共通利用するキー設定インスタンスを取得する。</summary>
    static KeyConfig* GetInstance();

    /// <summary>既定のアクション割り当てを準備する。</summary>
    void Initialize();

    // アクション名からデータを取得する関数群
    // 未登録アクションは0を返し、割当キーなしとして扱う。
    int GetKeyCode(const std::string& actionName) const;

    // 未登録アクションは0を返す。複数デバイスの統合判定はInputManager側で行う。
    WORD GetPadCode(const std::string& actionName) const;

    // パッドの数値を「A」や「RB」などの文字列に変換する
    // 引数はDIKキーコード。未対応の値も数値を含む表示名として返す。
    std::string GetKeyName(int keyCode) const;

    /// <summary>ゲームパッドのボタンコードに対応する表示名を返す。</summary>
    std::string GetPadName(WORD padCode) const;

    /// <summary>アクション名に対応する入力割り当てを取得する。</summary>
    // 戻り値は固定表への借用参照。未登録ならnullptrで、呼び出し側から解放しない。
    const BindData* GetBindData(const std::string& actionName) const;

    /// <summary>指定キーがいずれかのゲーム操作へ割り当てられているか返す。</summary>
    bool IsKeyAssigned(int keyCode) const;

private:
    KeyConfig() = default;

    ~KeyConfig() = default;

    KeyConfig(const KeyConfig&) = delete;

    KeyConfig& operator=(const KeyConfig&) = delete;

private:
    // 外部にはconst参照だけを公開する固定の入力表。
    std::unordered_map<std::string, BindData> bindings_;

};
