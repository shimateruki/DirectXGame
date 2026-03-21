#pragma once
#include <cstdint>

class DebugEditor;
class Object3d;

// 右パネル（Inspector / 詳細設定）の描画を担当するクラス
class InspectorWindow {
public:
    InspectorWindow() = default;
    ~InspectorWindow() = default;

    void Initialize(DebugEditor* editor);
    void Draw();

private:
    // Inspector専用のUIヘルパー関数群
    void DrawSpawnerSettings();
    void DrawEnemyTypeSelector();
    void DrawAttributeSelector(const char* label, uint32_t* attribute);

    DebugEditor* editor_ = nullptr;
};