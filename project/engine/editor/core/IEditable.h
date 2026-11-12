#pragma once
#include <string>

// 担当：Editorに表示するUIと表示名の共通契約を定義する。
// 関連：DebugEditor / GameEditorController（各ツールを呼び出す）
// 主な入口：DrawImGui / GetName
class IEditable {
public:
    virtual ~IEditable() = default;

    // 現在の Inspector コンテキストへ UI を描画します。
    virtual void DrawImGui() = 0;

    // Inspector 上部に表示する名前を返します。
    virtual std::string GetName() = 0;
};
