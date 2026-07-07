#pragma once

#include "IEditable.h"

#include <string>

class DebugEditor;

/// SE/BGMの音量やカテゴリ設定を確認・調整するための音声設定ウィンドウ。
class AudioSettingsWindow : public IEditable {
public:
    /// エディタから開くための参照を保持し、音声設定表示の初期状態を整える。
    void Initialize(DebugEditor* editor);
    /// 音声一覧、検索、音量調整、設定保存などのUIを描画する。
    void DrawImGui() override;
    std::string GetName() override { return "音声設定 (Audio Settings)"; }

private:
    /// 入力中の検索文字列に一致する音声項目だけを表示するための判定。
    bool MatchesSearch(const std::string& text) const;

    DebugEditor* editor_ = nullptr;
    char searchBuffer_[128] = "";
    std::string statusText_ = "SE/BGMの音量を調整できます。";
};
