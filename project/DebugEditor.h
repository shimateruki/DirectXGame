// [新規作成] DebugEditor.h
#pragma once

// (前方宣言)
class GamePlayScene;
class Object3d;

/// <summary>
/// デバッグビルド専用のインゲームエディタ
/// </summary>
class DebugEditor {
public:
    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="scene">操作対象のシーン</param>
    void Initialize(GamePlayScene* scene);

    /// <summary>
    /// 更新 (ImGuiのウィンドウ描画)
    /// </summary>
    void Update();

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

private:
    GamePlayScene* scene_ = nullptr; // 操作対象のシーン
    Object3d* selectedObject_ = nullptr; // 現在選択中のオブジェクト
};