#pragma once

#include <string>

// ユーザー設定ファイルから音量と操作設定を読み書きするクラス。
class GameSettingsManager {
  public:
    // ステージセレクトの天候を自動、晴れ、雨に切り替える設定。
    enum class StageSelectWeatherMode { Auto = 0, Clear = 1, Rain = 2 };

    static GameSettingsManager* GetInstance();

    // 初回は設定を読み込み、以降は保持済み音量を再適用する。
    void Initialize();
    // 不正な項目は現在値または既定値で補い、音量を即時反映する。
    void Load();
    // 同じ JSON にある別機能の設定は維持する。書き込み成功時だけ true。
    bool Save() const;

    float GetBGMVolume() const { return bgmVolume_; }
    float GetSEVolume() const { return seVolume_; }
    float GetCameraSensitivity() const { return cameraSensitivity_; }
    bool GetCameraInvertX() const { return cameraInvertX_; }
    bool GetCameraInvertY() const { return cameraInvertY_; }
    void SetCameraInversion(bool x, bool y) {
        cameraInvertX_ = x;
        cameraInvertY_ = y;
    }
    StageSelectWeatherMode GetStageSelectWeatherMode() const { return stageSelectWeatherMode_; }

    // 音量は 0～1、カメラ感度は 0.2～1 に制限する。変更は Save() まで永続化されない。
    void SetBGMVolume(float volume);
    void SetSEVolume(float volume);
    void SetCameraSensitivity(float sensitivity);
    void SetStageSelectWeatherMode(StageSelectWeatherMode mode);
    void ApplyAudioSettings() const;

  private:
    GameSettingsManager() = default;
    GameSettingsManager(const GameSettingsManager&) = delete;
    GameSettingsManager& operator=(const GameSettingsManager&) = delete;

    std::string filePath_ = "Resources/json/user_config.json";
    bool initialized_ = false;
    float bgmVolume_ = 0.8f;
    float seVolume_ = 0.8f;
    float cameraSensitivity_ = 1.0f;
    bool cameraInvertX_ = false;
    bool cameraInvertY_ = false;
    StageSelectWeatherMode stageSelectWeatherMode_ = StageSelectWeatherMode::Auto;
};
