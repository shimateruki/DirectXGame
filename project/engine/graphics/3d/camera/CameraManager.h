#pragma once
#include "Camera.h"
#include <memory>

namespace engine { class InputManager; }

// 担当：メイン・一時カメラを選択し、領域切替と視界確保の結果を管理する。
// 関連：Camera（視点計算）、SceneCameraSettings（領域設定）、MeshRenderer（視界確保の反映）
// 主な入口：Initialize / Update / SetActiveCamera / GetVisibilityFrame
class CameraManager {
public:
    /// <summary>共有のカメラ管理インスタンスを取得する。</summary>
    static CameraManager* GetInstance();

    /// <summary>既定のメインカメラと管理状態を初期化する。</summary>
    void Initialize();

    /// <summary>所有するメインカメラを更新する。一時的なカメラはここでは更新しない。</summary>
    void Update(float deltaTime = 1.0f / 60.0f);

    /// <summary>カメラ操作で参照する入力管理を設定する。</summary>
    void SetInputManager(engine::InputManager* inputManager);

    /// <summary>所有するメインカメラを返す。</summary>
    Camera* GetMainCamera() { return mainCamera_.get(); }

    /// <summary>一時的なカメラがあればそれを、なければメインカメラを返す。</summary>
    Camera* GetActiveCamera() { return activeCamera_ ? activeCamera_ : mainCamera_.get(); }

    /// <summary>一時的なカメラへの参照を返す。未設定時はnullptr。</summary>
    Camera* GetActiveCameraOverride() const { return activeCamera_; }

    /// <summary>一時的に有効にするカメラを設定する。所有しない。</summary>
    void SetActiveCamera(Camera* camera) { activeCamera_ = camera; }

    /// <summary>メインカメラを演出用の固定視点へ切り替える。多重開始時はfalseを返す。</summary>
    bool BeginMainCameraPresentation();

    /// <summary>演出終了前に通常追従へ戻し、Camera ObjectのBlend Out先を設定する。</summary>
    void PrepareMainCameraReturn(engine::Object3D* followTarget, const engine::Vector3& eye,
                                 const engine::Vector3& target);

    /// <summary>プレイヤーを追う距離へ即座に合わせてから入力と有効カメラを復元する。</summary>
    void SnapMainCameraReturn(float distance, float height, float pitch);

    /// <summary>演出開始前のカメラ入力と有効カメラを復元する。</summary>
    void EndMainCameraPresentation();

    /// <summary>有効カメラへ画面揺れを要求する。</summary>
    void PlayShake(float duration, float amplitude, float frequency = 24.0f, const engine::Vector3& axisWeight = { 1.0f, 1.0f, 0.5f });

    /// <summary>有効カメラへ視野角のパルス演出を要求する。</summary>
    void PlayFovPulse(float duration, float amountRadians, float attackRatio = 0.12f);

    /// <summary>有効カメラの一時的な演出レイヤーを消去する。</summary>
    void ClearPresentationLayers();

    /// Editorと実行時が同じ計算でCamera Objectのワールド視点を解決する。
    static bool ResolveSceneCameraPose(const engine::Object3D& object,
        const std::vector<std::unique_ptr<engine::Object3D>>& objects, engine::Vector3& eye, engine::Vector3& target, bool requireTarget = false);
    /// 有効な範囲をまとめて評価する。手動・照準・ロックオン・演出には割り込まない。
    void UpdateCameraRegions(const std::vector<std::unique_ptr<engine::Object3D>>& objects,
        engine::Object3D* subject, float deltaTime, bool enabled, bool manualInput);
    /// シーン切替前に所有中の範囲カメラと非所有参照を解除する。
    void ResetCameraRegions();
    const std::string& GetActiveRegionName() const { return activeRegionName_; }
    const std::string& GetRegionStatus() const { return regionStatus_; }

    // カメラ更新後の値を描画へ渡す。Objectを保存せず、シーン切替時に無効化する。
    // 関連：CameraManager（この型を使う機能の本体）
    struct VisibilityFrame {
        engine::Vector4 centerRadius{};
        engine::Vector4 parameters{}; // 強度、境界幅、足元高さ、対象手前の奥行余白。
        engine::Vector4 foregroundBarrierCenter{}; // 闘技場の中心と有効な重み。
        engine::Vector4 foregroundBarrierParameters{}; // 床高さ、残す高さ、最小不透明度、境界幅。
        float deltaTime = 0.0f;
        uint64_t serial = 0;
        bool enabled = false;
    };
    const VisibilityFrame& GetVisibilityFrame() const { return visibilityFrame_; }
    // 指定カメラの視界確保で、対象を透過してよいか判定する。
    bool CanFadeObject(const engine::Object3D* object, const Camera* camera) const;
    // 視界確保の対象と透過量を更新する。deltaTimeは秒。
    void UpdateVisibility(float deltaTime);

private:
    float routeProgress_ = 0.0f;
    std::string routeProgressId_;
    VisibilityFrame visibilityFrame_;
    engine::Vector4 regionBarrierCenter_{};
    engine::Vector4 regionBarrierParameters_{};
    CameraManager() = default;

    ~CameraManager() = default;

    CameraManager(const CameraManager&) = delete;

    CameraManager& operator=(const CameraManager&) = delete;

private:
    std::string activeRegionId_, pendingRegionId_, activeRegionName_;
    std::string regionStatus_ = "範囲カメラなし";
    float pendingRegionTime_ = 0.0f; // 切替候補の領域に留まった秒数。
    float manualRegionDelay_ = 0.0f;
    float regionExitDuration_ = 0.3f; // 領域から通常視点へ戻るブレンド秒数。
    uint64_t regionOverrideRevision_ = 0;
    bool ownsRegionOverride_ = false;

    std::unique_ptr<Camera> mainCamera_; // 通常のゲーム視点を所有する。
    Camera* activeCamera_ = nullptr; // 一時的に選んだカメラ。借用参照。
    Camera* presentationPreviousActiveCamera_ = nullptr; // メインカメラ演出の開始前に有効だった視点。
    bool presentationPreviousInputEnabled_ = true;
    bool mainCameraPresentationActive_ = false;
};
