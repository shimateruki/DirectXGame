#pragma once
#include <string>

// 前方宣言 (インクルード地獄回避)
class Object3d;

class LevelEditor {
public:

    void Initialize();

    // 毎フレームの処理
    void Update();

    // ImGuiの描画
    void DrawImGui();

private:
    // アセットブラウザで現在開いているフォルダパス
    std::string currentDirectory_ = "resources";
};