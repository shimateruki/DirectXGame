#pragma once
#include "engine/utility/math/Math.h"
#include "Camera.h"
#include "IEditable.h"
#include <vector>
#include <string>
#include <map>
class Object3d;

class CameraEditor : public IEditable {
public:
    enum class Mode {
        Game,   // ゲーム挙動 (プレイヤー追従など)
        Editor  // 自由カメラ (WASD移動)
    };

    struct Settings {
        Mode currentMode = Mode::Game;
        Camera::FollowMode gameFollowMode = Camera::FollowMode::kAimable;

        // --- Gameモード用パラメータ ---
        float distance = 15.0f;
        float height = 5.0f;
        Vector3 angle = { 25.0f, 0.0f, 0.0f };
        Vector3 lockOnOffset = { 0.0f, 4.0f, -12.0f };

        float orbitRadius = 15.0f;
        float orbitHeight = 5.0f;
        float orbitSpeed = 0.005f;
        int cameraSensitivity = 0;
        // --- Editorモード用パラメータ ---
        float moveSpeed = 0.5f;
        float boostSpeed = 2.0f;
        float mouseSensitivity = 0.003f;
        Vector3 fixedPointPos = { 0.0f, 5.0f, -15.0f };
    };

public:
    static CameraEditor* GetInstance();

    void Initialize();
    void Update(Object3d* player, bool isLockingOn);

    // Inspectorに表示するUI描画処理
    void DrawImGui() override;

    // Inspector上部に表示される名前
    std::string GetName() override { return "Camera Editor"; }

    void SaveSettings();
    void LoadSettings();
    void LoadFile(const std::string& fileName);
    void RefreshFileList();

    bool IsEditorMode() const { return settings_.currentMode == Mode::Editor; }
    void SetMode(Mode mode);
    Mode GetMode() const { return settings_.currentMode; }
    void SetEditorCameraTransform(const Vector3& position, const Vector3& rotation);
    void SetGameViewHovered(bool hovered) { isGameViewHovered_ = hovered; }
    /// <summary>
    /// 保存されている名前を呼び出すだけで、指定カメラを自動でオーバーライドさせる
    /// </summary>
    bool PlayOverrideCamera(Camera* camera, const std::string& cameraName);
    int GetCameraSensitivity() const { return settings_.cameraSensitivity; }
    void SetCameraSensitivity(int val) {
        settings_.cameraSensitivity = val;
        SaveSettings(); // 変更された瞬間に自動セーブ！
    }


private:
    void UpdateFreeCamera(Camera* camera);

private:
    CameraEditor() = default;
    ~CameraEditor() = default;
    CameraEditor(const CameraEditor&) = delete;
    const CameraEditor& operator=(const CameraEditor&) = delete;

    Settings settings_;
    char fileNameBuffer_[64] = "camera_settings.json";

    const std::string kDirectoryPath_ = "Resources/json/camera/";
    std::vector<std::string> fileList_;
    bool isGameViewHovered_ = false;
    std::map<std::string, Camera::CameraOverrideParams> overrideParamsMap_;
    std::string selectedOverrideName_ = ""; // 現在エディタで選択中のカメラ名
    char newOverrideNameBuffer_[64] = "";   // 新規作成時の名前入力欄
    Object3d* targetPlayer_ = nullptr;
};
