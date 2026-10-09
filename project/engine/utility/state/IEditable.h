#pragma once
#include <string>

// エディタへ表示する対象の最小契約。描画と表示名は実装側が提供します。
class IEditable {
public:
    virtual ~IEditable() = default;

    // 現在の Inspector コンテキストへ UI を描画します。
    virtual void DrawImGui() = 0;

    // Inspector 上部に表示する名前を返します。
    virtual std::string GetName() = 0;
};
