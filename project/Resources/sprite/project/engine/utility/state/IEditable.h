#pragma once
#include <string>

class IEditable {
public:
    virtual ~IEditable() = default;

    // Inspectorに表示するUIの描画処理（各クラスで中身を書く）
    virtual void DrawImGui() = 0;

    // Inspectorの一番上に表示する名前（各クラスで返す名前を決める）
    virtual std::string GetName() = 0;
};