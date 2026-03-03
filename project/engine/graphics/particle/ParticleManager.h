#pragma once
#include <string>
#include <map>
#include <vector>
#include "ParticleSystem.h"
#include "engine/utility/math/Math.h"

/// <summary>
/// パーティクルパラメータの保存・読み込み・発生を管理するクラス
/// </summary>
class ParticleManager {
public:
    // シングルトンインスタンス取得
    static ParticleManager* GetInstance();

    // 初期化（ParticleSystemへの参照をもらう）
    void Initialize(ParticleSystem* particleSystem);

    // -------------------------------------------------------------
    // ファイル操作
    // -------------------------------------------------------------
    // パラメータをJSONファイルとして保存
    void SaveParam(const std::string& name, const ParticleSystem::EmitterParams& param);
    // JSONファイルからパラメータを読み込み
    void LoadParam(const std::string& name);
    // 指定フォルダ(Resources/json/particle/)内の全JSONを一括読み込み
    void LoadAllParams();

    // -------------------------------------------------------------
    // 実行時処理
    // -------------------------------------------------------------
    // 名前指定でパーティクルを発生させる（Object3dなどから毎フレーム呼ばれる想定）
    // name: エフェクト名, position: 発生位置, timer: 呼び出し元のタイマー(参照渡しで更新)
    void Emit(const std::string& name, const Vector3& position, float& timer);

    // 編集用：現在のパラメータリストを取得
    const std::map<std::string, ParticleSystem::EmitterParams>& GetParamsMap() const { return paramsMap_; }

    // パラメータの取得（編集用）
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