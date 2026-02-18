#pragma once
#include "engine/utility/math/Math.h"
#include "Camera.h"
#include <vector>
#include <string>

// 前方宣言
class Object3d;

/// <summary>
/// カメラの調整・管理・保存を行うエディタクラス
/// </summary>
class CameraEditor {
public:
    // エディタ自体の動作モード
    enum class Mode {
        Game,   // ゲーム挙動 (プレイヤー追従)
        Editor  // 自由カメラ (WASD移動)
    };

    // 保存対象の設定データ
    struct Settings {
        Mode currentMode = Mode::Game;
        Camera::FollowMode gameFollowMode = Camera::FollowMode::kAimable;

        // --- Gameモード用パラメータ ---
        float distance = 15.0f;
        float height = 5.0f;
        Vector3 angle = { 25.0f, 0.0f, 0.0f };
        Vector3 lockOnOffset = { 0.0f, 4.0f, -12.0f };

        float orbitRadius = 15.0f;   // 半径
        float orbitHeight = 5.0f;    // 高さ
        float orbitSpeed = 0.005f;  // 回転速度

        // --- Editorモード用パラメータ ---
        float moveSpeed = 0.5f;       // 通常移動速度
        float boostSpeed = 2.0f;      // Shift押下時の速度
        float mouseSensitivity = 0.003f; // マウス感度
        Vector3 fixedPointPos = { 0.0f, 5.0f, -15.0f };
    };

public:
    static CameraEditor* GetInstance();

    void Initialize();

    /// <summary>
    /// 毎フレーム呼び出す処理
    /// </summary>
    /// <param name="player">追従対象のプレイヤー</param>
    /// <param name="isLockingOn">ロックオン中かどうか</param>
    void Update(Object3d* player, bool isLockingOn);

    // ImGui描画
    void DrawImGui();

    // JSON保存・読み込み
    void SaveSettings();
    void LoadSettings();

    void LoadFile(const std::string& fileName);

    void RefreshFileList();

    // ゲッター
    bool IsEditorMode() const { return settings_.currentMode == Mode::Editor; }

    // モードを強制設定
    void SetMode(Mode mode);

    // 現在のモードを取得（元に戻すときに使う）
    Mode GetMode() const { return settings_.currentMode; }

    // エディタカメラの位置と回転を強制設定
    void SetEditorCameraTransform(const Vector3& position, const Vector3& rotation);

private:
    // 内部ヘルパー: 自由カメラの移動処理
    void UpdateFreeCamera(Camera* camera);

private:
    CameraEditor() = default;
    ~CameraEditor() = default;
    CameraEditor(const CameraEditor&) = delete;
    const CameraEditor& operator=(const CameraEditor&) = delete;

    Settings settings_;
    char fileNameBuffer_[64] = "camera_settings.json";

    // 保存先ディレクトリパス
    const std::string kDirectoryPath_ = "Resources/json/camera/";
    std::vector<std::string> fileList_;



};