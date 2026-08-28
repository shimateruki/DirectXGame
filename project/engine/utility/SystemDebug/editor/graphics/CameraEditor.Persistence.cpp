#include "CameraEditor.h"

#include "DebugConsole.h"
#include "PostEffect.h"
#include "json.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

// フォルダ内の .json ファイルを探してリストを更新する
void CameraEditor::RefreshFileList() {
  fileList_.clear();

  // 保存先が存在しない場合は先に作成する。
  if (!fs::exists(kDirectoryPath_)) {
    fs::create_directories(kDirectoryPath_);
    return;
  }

  // ディレクトリ内を走査して .json だけリストに追加
  for (const auto &entry : fs::directory_iterator(kDirectoryPath_)) {
    if (entry.path().extension() == ".json") {
      std::string fileName = entry.path().filename().string();
      // エディタ状態保存用のファイルはリストに表示しない
      if (fileName == "editor_camera_state.json")
        continue;
      fileList_.push_back(fileName);
    }
  }
}

void CameraEditor::SaveSettings() {
  // ディレクトリパス + 入力されたファイル名 を結合
  std::string filePath = kDirectoryPath_ + std::string(fileNameBuffer_);

  json j;
  // j["mode"] = static_cast<int>(settings_.currentMode);
  j["gameFollowMode"] = static_cast<int>(settings_.gameFollowMode);
  j["distance"] = settings_.distance;
  j["height"] = settings_.height;
  j["angle"] = {settings_.angle.x, settings_.angle.y, settings_.angle.z};
  j["lockOnOffset"] = {settings_.lockOnOffset.x, settings_.lockOnOffset.y,
                       settings_.lockOnOffset.z};

  //  周回(Orbit)モード用パラメータの保存
  j["orbitRadius"] = settings_.orbitRadius;
  j["orbitCenterOffset"] = {settings_.orbitCenterOffset.x,
                            settings_.orbitCenterOffset.y,
                            settings_.orbitCenterOffset.z};
  j["orbitCenterHeight"] = settings_.orbitCenterHeight;
  j["orbitHeight"] = settings_.orbitHeight;
  j["orbitSpeed"] = settings_.orbitSpeed;
  j["orbitStartAngleDeg"] = settings_.orbitStartAngleDeg;
  j["orbitGuideVisible"] = settings_.orbitGuideVisible;
  j["orbitCenterGizmoVisible"] = settings_.orbitCenterGizmoVisible;
  j["orbitGuideMarkerSize"] = settings_.orbitGuideMarkerSize;
  j["cameraSensitivity"] = settings_.cameraSensitivity;
  j["cameraGuideVisible"] = settings_.cameraGuideVisible;
  j["cameraBodyVisible"] = settings_.cameraBodyVisible;
  j["cameraPreviewVisible"] = settings_.cameraPreviewVisible;
  j["cameraPreviewFps"] = settings_.cameraPreviewFps;
  j["cameraPreviewResolutionScale"] = settings_.cameraPreviewResolutionScale;
  j["cameraGuideSize"] = settings_.cameraGuideSize;
  j["cameraFrustumLength"] = settings_.cameraFrustumLength;
  j["cameraPreviewHeight"] = settings_.cameraPreviewHeight;
  // エディタ設定
  j["moveSpeed"] = settings_.moveSpeed;
  j["boostSpeed"] = settings_.boostSpeed;
  j["mouseSensitivity"] = settings_.mouseSensitivity;
  j["fixedPointPos"] = {settings_.fixedPointPos.x, settings_.fixedPointPos.y,
                        settings_.fixedPointPos.z};
  j["fixedPointAngle"] = {settings_.fixedPointAngle.x,
                          settings_.fixedPointAngle.y,
                          settings_.fixedPointAngle.z};

  // editorCameraPos, editorCameraAngle はここでは保存しない
  // (SaveEditorStateに移行)

  json overridesJson = json::object();
  for (const auto &[name, param] : overrideParamsMap_) {
    json p;
    p["duration"] = param.duration;
    p["exitDuration"] = param.exitDuration;
    p["easing"] = static_cast<int>(param.easing);
    p["eyeSource"] = static_cast<int>(param.eyeSource);
    p["eyeObjectName"] = param.eyeObjectName;
    p["eyeObjectOffset"] = {param.eyeObjectOffset.x, param.eyeObjectOffset.y,
                            param.eyeObjectOffset.z};
    p["eyeFollowMode"] = static_cast<int>(param.eyeFollowMode);
    p["eyeFollowResponse"] = param.eyeFollowResponse;
    p["trackEyeX"] = param.trackEyeX;
    p["trackEyeY"] = param.trackEyeY;
    p["trackEyeZ"] = param.trackEyeZ;
    p["fixedEyePos"] = {param.fixedEyePos.x, param.fixedEyePos.y,
                        param.fixedEyePos.z};

    p["targetSource"] = static_cast<int>(param.targetSource);
    p["targetObjectName"] = param.targetObjectName;
    p["targetObjectOffset"] = {param.targetObjectOffset.x,
                               param.targetObjectOffset.y,
                               param.targetObjectOffset.z};
    p["eyeForwardDistance"] = param.eyeForwardDistance;
    p["targetFollowMode"] = static_cast<int>(param.targetFollowMode);
    p["targetFollowResponse"] = param.targetFollowResponse;
    p["trackTargetX"] = param.trackTargetX;
    p["trackTargetY"] = param.trackTargetY;
    p["trackTargetZ"] = param.trackTargetZ;
    p["fixedTargetPos"] = {param.fixedTargetPos.x, param.fixedTargetPos.y,
                           param.fixedTargetPos.z};

    overridesJson[name] = p; // JSONオブジェクトに追加
  }
  j["Overrides"] =
      overridesJson; // 大元のJSONに「Overrides」という項目でまとめる
  std::ofstream file(filePath);
  if (file.is_open()) {
    file << j.dump(4);
  }

  RefreshFileList();
}
void CameraEditor::LoadSettings() {
  // ディレクトリパス + 入力されたファイル名 を結合
  std::string filePath = kDirectoryPath_ + std::string(fileNameBuffer_);

  std::ifstream file(filePath);
  if (!file.is_open())
    return;

  try {
    json j;
    file >> j;
    /*  if (j.contains("mode")) settings_.currentMode =
     * static_cast<Mode>(j["mode"]);*/
    if (j.contains("gameFollowMode"))
      settings_.gameFollowMode =
          static_cast<Camera::FollowMode>(j["gameFollowMode"]);

    if (j.contains("distance"))
      settings_.distance = j["distance"];
    if (j.contains("height"))
      settings_.height = j["height"];
    if (j.contains("angle")) {
      if (j["angle"].is_array()) {
        settings_.angle.x = j["angle"][0];
        settings_.angle.y = j["angle"][1];
        settings_.angle.z = j["angle"][2];
      } else {
        // 古いデータ(float)の場合はX(Pitch)にだけ入れる
        settings_.angle.x = j["angle"];
        settings_.angle.y = 0.0f;
        settings_.angle.z = 0.0f;
      }
    }
    if (j.contains("lockOnOffset") && j["lockOnOffset"].is_array()) {
      settings_.lockOnOffset.x = j["lockOnOffset"][0];
      settings_.lockOnOffset.y = j["lockOnOffset"][1];
      settings_.lockOnOffset.z = j["lockOnOffset"][2];
    }

    //  周回(Orbit)モード用パラメータの読み込み
    if (j.contains("orbitRadius"))
      settings_.orbitRadius = j["orbitRadius"];
    if (j.contains("orbitCenterOffset") && j["orbitCenterOffset"].is_array() &&
        j["orbitCenterOffset"].size() >= 3) {
      settings_.orbitCenterOffset.x = j["orbitCenterOffset"][0];
      settings_.orbitCenterOffset.y = j["orbitCenterOffset"][1];
      settings_.orbitCenterOffset.z = j["orbitCenterOffset"][2];
    }
    if (j.contains("orbitCenterHeight"))
      settings_.orbitCenterHeight = j["orbitCenterHeight"];
    if (j.contains("orbitHeight"))
      settings_.orbitHeight = j["orbitHeight"];
    if (j.contains("orbitSpeed"))
      settings_.orbitSpeed = j["orbitSpeed"];
    if (j.contains("orbitStartAngleDeg"))
      settings_.orbitStartAngleDeg = j["orbitStartAngleDeg"];
    if (j.contains("orbitGuideVisible"))
      settings_.orbitGuideVisible = j["orbitGuideVisible"];
    if (j.contains("orbitCenterGizmoVisible"))
      settings_.orbitCenterGizmoVisible = j["orbitCenterGizmoVisible"];
    if (j.contains("orbitGuideMarkerSize"))
      settings_.orbitGuideMarkerSize = j["orbitGuideMarkerSize"];
    if (j.contains("cameraSensitivity"))
      settings_.cameraSensitivity = j["cameraSensitivity"];
    if (j.contains("cameraGuideVisible"))
      settings_.cameraGuideVisible = j["cameraGuideVisible"];
    if (j.contains("cameraBodyVisible"))
      settings_.cameraBodyVisible = j["cameraBodyVisible"];
    if (j.contains("cameraPreviewVisible"))
      settings_.cameraPreviewVisible = j["cameraPreviewVisible"];
    if (j.contains("cameraPreviewFps"))
      settings_.cameraPreviewFps = j["cameraPreviewFps"];
    if (j.contains("cameraPreviewResolutionScale")) {
      settings_.cameraPreviewResolutionScale =
          j["cameraPreviewResolutionScale"];
    }
    settings_.cameraPreviewFps = std::clamp(settings_.cameraPreviewFps, 1, 60);
    settings_.cameraPreviewResolutionScale =
        std::clamp(settings_.cameraPreviewResolutionScale, 0.25f, 1.0f);
    PostEffect::GetInstance()->SetCameraPreviewResolutionScale(
        settings_.cameraPreviewResolutionScale);
    InvalidateCameraPreviews();
    if (j.contains("cameraGuideSize"))
      settings_.cameraGuideSize = j["cameraGuideSize"];
    if (j.contains("cameraFrustumLength"))
      settings_.cameraFrustumLength = j["cameraFrustumLength"];
    if (j.contains("cameraPreviewHeight"))
      settings_.cameraPreviewHeight = j["cameraPreviewHeight"];

    if (j.contains("moveSpeed"))
      settings_.moveSpeed = j["moveSpeed"];
    if (j.contains("boostSpeed"))
      settings_.boostSpeed = j["boostSpeed"];
    if (j.contains("mouseSensitivity"))
      settings_.mouseSensitivity = j["mouseSensitivity"];
    if (j.contains("fixedPointPos") && j["fixedPointPos"].is_array()) {
      settings_.fixedPointPos.x = j["fixedPointPos"][0];
      settings_.fixedPointPos.y = j["fixedPointPos"][1];
      settings_.fixedPointPos.z = j["fixedPointPos"][2];
    }
    if (j.contains("fixedPointAngle") && j["fixedPointAngle"].is_array()) {
      settings_.fixedPointAngle.x = j["fixedPointAngle"][0];
      settings_.fixedPointAngle.y = j["fixedPointAngle"][1];
      settings_.fixedPointAngle.z = j["fixedPointAngle"][2];
    }

    // editorCameraPos, editorCameraAngle はここでは読み込まない
    // (LoadEditorStateに移行)
    overrideParamsMap_.clear();
    if (j.contains("Overrides")) {
      for (auto &[key, val] : j["Overrides"].items()) {
        Camera::CameraOverrideParams p;
        p.duration = val.value("duration", 1.0f);
        p.exitDuration = val.value("exitDuration", 0.35f);
        p.easing = static_cast<Camera::OverrideEasing>(
            std::clamp(val.value("easing", 4), 0, 4));
        p.eyeSource = static_cast<Camera::OverrideEyeSource>(
            std::clamp(val.value("eyeSource", 0), 0, 1));
        p.eyeObjectName = val.value("eyeObjectName", "");
        if (val.contains("eyeObjectOffset") &&
            val["eyeObjectOffset"].is_array() &&
            val["eyeObjectOffset"].size() >= 3) {
          p.eyeObjectOffset = {val["eyeObjectOffset"][0],
                               val["eyeObjectOffset"][1],
                               val["eyeObjectOffset"][2]};
        }
        p.eyeFollowMode = static_cast<Camera::OverrideFollowMode>(
            std::clamp(val.value("eyeFollowMode", 0), 0, 1));
        p.eyeFollowResponse = val.value("eyeFollowResponse", 12.0f);
        p.trackEyeX = val.value("trackEyeX", false);
        p.trackEyeY = val.value("trackEyeY", false);
        p.trackEyeZ = val.value("trackEyeZ", false);
        if (val.contains("fixedEyePos")) {
          p.fixedEyePos = {val["fixedEyePos"][0], val["fixedEyePos"][1],
                           val["fixedEyePos"][2]};
        }
        p.targetSource = static_cast<Camera::OverrideTargetSource>(
            std::clamp(val.value("targetSource", 0), 0, 2));
        p.targetObjectName = val.value("targetObjectName", "");
        if (val.contains("targetObjectOffset") &&
            val["targetObjectOffset"].is_array() &&
            val["targetObjectOffset"].size() >= 3) {
          p.targetObjectOffset = {val["targetObjectOffset"][0],
                                  val["targetObjectOffset"][1],
                                  val["targetObjectOffset"][2]};
        }
        p.eyeForwardDistance = val.value("eyeForwardDistance", 10.0f);
        p.targetFollowMode = static_cast<Camera::OverrideFollowMode>(
            std::clamp(val.value("targetFollowMode", 0), 0, 1));
        p.targetFollowResponse = val.value("targetFollowResponse", 14.0f);
        p.trackTargetX = val.value("trackTargetX", true);
        p.trackTargetY = val.value("trackTargetY", true);
        p.trackTargetZ = val.value("trackTargetZ", true);
        if (val.contains("fixedTargetPos")) {
          p.fixedTargetPos = {val["fixedTargetPos"][0],
                              val["fixedTargetPos"][1],
                              val["fixedTargetPos"][2]};
        }
        overrideParamsMap_[key] = p;
      }
    }

  } catch (...) {
    // エラーハンドリング
  }
}

void CameraEditor::LoadFile(const std::string &fileName) {
  // 1. ファイル名バッファを更新
  strcpy_s(fileNameBuffer_, sizeof(fileNameBuffer_), fileName.c_str());

  // 2. パスを作成
  std::string filePath = kDirectoryPath_ + std::string(fileNameBuffer_);

  // 3. ファイルが存在するかチェック
  if (!fs::exists(filePath)) {
    const Mode selectedMode = settings_.currentMode;
    settings_ = Settings(); // デフォルトコンストラクタで初期化
    // シーン固有ファイルが未作成でも、利用中の自由カメラを解除しません。
    settings_.currentMode = selectedMode;

    // ログ出し (任意)
    DebugConsole::GetInstance()->AddLog("New Camera Setting Created: " +
                                        fileName);

    // 新規保存
    SaveSettings();
  }

  // 4. 改めて読み込み
  LoadSettings();
}

void CameraEditor::SaveEditorState() {
  if (IsEditorStateSaveBlocked())
    return;

  const fs::path filePath(kEditorStatePath_);
  std::error_code error;
  fs::create_directories(filePath.parent_path(), error);
  if (error) {
    return;
  }
  json j;
  j["editorCameraPos"] = {settings_.editorCameraPos.x,
                          settings_.editorCameraPos.y,
                          settings_.editorCameraPos.z};
  j["editorCameraAngle"] = {settings_.editorCameraAngle.x,
                            settings_.editorCameraAngle.y,
                            settings_.editorCameraAngle.z};
  std::ofstream file(filePath);
  if (file.is_open()) {
    file << j.dump(4);
    if (file.good()) {
      editorStateDirty_ = false;
    }
  }
}

void CameraEditor::LoadEditorState() {
  fs::path filePath(kEditorStatePath_);
  if (!fs::exists(filePath)) {
    // 旧保存先から一度だけ読み込み、次回保存時にEditor専用領域へ移行します。
    filePath = fs::path(kDirectoryPath_) / "editor_camera_state.json";
  }
  std::ifstream file(filePath);
  if (!file.is_open())
    return;
  try {
    json j;
    file >> j;
    if (j.contains("editorCameraPos") && j["editorCameraPos"].is_array()) {
      settings_.editorCameraPos.x = j["editorCameraPos"][0];
      settings_.editorCameraPos.y = j["editorCameraPos"][1];
      settings_.editorCameraPos.z = j["editorCameraPos"][2];
    }
    if (j.contains("editorCameraAngle") && j["editorCameraAngle"].is_array()) {
      settings_.editorCameraAngle.x = j["editorCameraAngle"][0];
      settings_.editorCameraAngle.y = j["editorCameraAngle"][1];
      settings_.editorCameraAngle.z = j["editorCameraAngle"][2];
    }
    editorStateDirty_ = false;
  } catch (...) {
  }
}
