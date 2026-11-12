#pragma once

#include <string>
#include <vector>

// 担当：Inspector用の画像・PBRセット一覧を収集し、必要時に再構築する。
// 関連：InspectorWindow（利用）、TextureManager（画像）
// 主な入口：EnsureLoaded / Refresh
class InspectorTextureCatalog {
public:
  // 同じ材質として選択する Albedo・Normal・ORM のファイル参照。画像本体は所有しない。
  // 関連：InspectorTextureCatalog（この型を使う機能の本体）
  struct PbrPreset {
    std::string name;
    std::string albedoPath;
    std::string normalPath;
    std::string ormPath;

    /// Albedo・Normal・ORMの3枚が揃っているかを返す。
    bool IsComplete() const;
  };

  /// 未構築の場合だけ、Resources以下からテクスチャを収集する。
  void EnsureLoaded();

  /// ファイル変更を反映するため、カタログを直ちに再構築する。
  void Refresh();

  const std::vector<std::string> &GetAlbedoPaths() const {
    return albedoPaths_;
  }

  const std::vector<std::string> &GetNormalPaths() const {
    return normalPaths_;
  }

  const std::vector<std::string> &GetOrmPaths() const { return ormPaths_; }

  const std::vector<std::string> &GetSpritePaths() const {
    return spritePaths_;
  }

  const std::vector<PbrPreset> &GetPbrPresets() const { return pbrPresets_; }

private:
  /// Resourcesを走査して、各用途の候補とPBRセットを作り直す。
  void Rebuild();

  std::vector<std::string> albedoPaths_;
  std::vector<std::string> normalPaths_;
  std::vector<std::string> ormPaths_;
  std::vector<std::string> spritePaths_;
  std::vector<PbrPreset> pbrPresets_;
  bool isLoaded_ = false;
};
