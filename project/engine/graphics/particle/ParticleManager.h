#pragma once
#include <string>
#include <map>
#include <vector>
#include "ParticleSystem.h"
#include "engine/utility/math/Math.h"

/// <summary>
/// CPUパーティクルの設定保存・読み込みと発生要求を管理する。
/// </summary>
class ParticleManager {
public:
    /// <summary>共有のCPUパーティクル管理インスタンスを取得する。</summary>
    static ParticleManager* GetInstance();

    /// <summary>粒子生成に使うParticleSystemを設定する。所有しない。</summary>
    void Initialize(ParticleSystem* particleSystem);

    /// <summary>指定名のパーティクル設定をJSONへ保存する。</summary>
    void SaveParam(const std::string& name, const ParticleSystem::EmitterParams& param);

    /// <summary>指定名のJSONからパーティクル設定を読み込む。</summary>
    void LoadParam(const std::string& name);

    /// <summary>Resources/json/particle 内の設定を一括読み込みする。</summary>
    void LoadAllParams();

    /// <summary>指定名の設定に従い、呼び出し元のタイマーを更新して粒子を発生させる。</summary>
    /// <param name="name">使用する設定名。</param>
    /// <param name="position">発生の中心位置。</param>
    /// <param name="timer">発生間隔の経過時間。呼び出し元で保持する。</param>
    /// <remarks>この経路は呼び出し1回を1/60秒として扱う。</remarks>
    void Emit(const std::string& name, const engine::Vector3& position, float& timer);

    /// <summary>エディタ表示用に、読み込み済み設定の一覧を返す。</summary>
    const std::map<std::string, ParticleSystem::EmitterParams>& GetParamsMap() const { return paramsMap_; }

    /// <summary>指定名の編集用設定を返す。未登録名は既定設定を追加する。</summary>
    ParticleSystem::EmitterParams& GetParam(const std::string& name) { return paramsMap_[name]; }

private:
    ParticleManager() = default;
    ~ParticleManager() = default;
    ParticleManager(const ParticleManager&) = delete;
    const ParticleManager& operator=(const ParticleManager&) = delete;

    ParticleSystem* particleSystem_ = nullptr;

    // 名前とパラメータの辞書 (例: "Fire" -> { params... })
    std::map<std::string, ParticleSystem::EmitterParams> paramsMap_;

    // JSON保存先パス
    const std::string kDirectoryPath = "Resources/json/particle/";
};
