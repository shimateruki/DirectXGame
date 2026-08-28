#include "InspectorTextureCatalog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace {
std::string ToLowerAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

std::string NormalizeAssetPath(const fs::path &path) {
  return path.generic_string();
}

bool IsSupportedTextureFile(const fs::path &path) {
  const std::string ext = ToLowerAscii(path.extension().string());
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds";
}

bool IsDdsTexture(const std::string &path) {
  return ToLowerAscii(fs::path(path).extension().string()) == ".dds";
}

// 変換済みDDSを優先し、同じ用途の候補を安定して一つに絞る。
void AssignPreferredTexture(std::string &slot, const std::string &candidate) {
  if (slot.empty() || (!IsDdsTexture(slot) && IsDdsTexture(candidate))) {
    slot = candidate;
  }
}

// 解像度やマップ種別の接尾辞を除き、3枚セット共通のキーを作る。
std::string BuildPbrPresetKey(const fs::path &path) {
  std::string stem = ToLowerAscii(path.stem().string());
  for (char &c : stem) {
    if (c == '-' || c == ' ') {
      c = '_';
    }
  }

  static const std::set<std::string> ignoredTokens = {
      "1k",      "2k",     "4k",        "8k",       "dx",       "gl",
      "directx", "opengl", "diff",      "diffuse",  "albedo",   "basecolor",
      "base",    "color",  "colour",    "nor",      "normal",   "arm",
      "orm",     "ao",     "roughness", "metallic", "metalness"};

  std::string key;
  size_t start = 0;
  while (start <= stem.size()) {
    const size_t end = stem.find('_', start);
    const std::string token = stem.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (!token.empty() && ignoredTokens.count(token) == 0) {
      if (!key.empty()) {
        key += "_";
      }
      key += token;
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  return key.empty() ? stem : key;
}

std::string BuildPbrPresetName(const std::string &key) {
  std::string name = key;
  for (char &c : name) {
    if (c == '_') {
      c = ' ';
    }
  }
  if (!name.empty()) {
    name[0] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  }
  return name;
}
} // namespace

bool InspectorTextureCatalog::PbrPreset::IsComplete() const {
  return !albedoPath.empty() && !normalPath.empty() && !ormPath.empty();
}

void InspectorTextureCatalog::EnsureLoaded() {
  if (!isLoaded_) {
    Rebuild();
  }
}

void InspectorTextureCatalog::Refresh() { Rebuild(); }

void InspectorTextureCatalog::Rebuild() {
  albedoPaths_.clear();
  normalPaths_.clear();
  ormPaths_.clear();
  spritePaths_.clear();
  pbrPresets_.clear();

  const fs::path pbrDir = "Resources/texture/PBR/";
  if (fs::exists(pbrDir)) {
    std::vector<std::string> allFiles;
    std::set<std::string> ddsBaseNames;
    std::map<std::string, PbrPreset> presetMap;

    for (const auto &entry : fs::recursive_directory_iterator(pbrDir)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const fs::path path = entry.path();
      if (!IsSupportedTextureFile(path)) {
        continue;
      }

      const std::string normalized = NormalizeAssetPath(path);
      allFiles.push_back(normalized);
      if (ToLowerAscii(path.extension().string()) == ".dds") {
        ddsBaseNames.insert(
            NormalizeAssetPath(path.parent_path() / path.stem()));
      }
    }

    for (const std::string &pathString : allFiles) {
      const fs::path path(pathString);
      const std::string ext = ToLowerAscii(path.extension().string());
      const std::string base =
          NormalizeAssetPath(path.parent_path() / path.stem());
      if (ext != ".dds" && ddsBaseNames.count(base) != 0) {
        continue;
      }

      if (pathString.find("/Albedo/") != std::string::npos) {
        albedoPaths_.push_back(pathString);
      } else if (pathString.find("/Normal/") != std::string::npos) {
        normalPaths_.push_back(pathString);
      } else if (pathString.find("/ARM/") != std::string::npos) {
        ormPaths_.push_back(pathString);
      }

      const std::string key = BuildPbrPresetKey(path);
      PbrPreset &preset = presetMap[key];
      preset.name = BuildPbrPresetName(key);

      if (pathString.find("/Albedo/") != std::string::npos) {
        AssignPreferredTexture(preset.albedoPath, pathString);
      } else if (pathString.find("/Normal/") != std::string::npos) {
        AssignPreferredTexture(preset.normalPath, pathString);
      } else if (pathString.find("/ARM/") != std::string::npos) {
        AssignPreferredTexture(preset.ormPath, pathString);
      } else if (pathString.find("/Default/") != std::string::npos) {
        const std::string stem = ToLowerAscii(path.stem().string());
        if (stem.find("albedo") != std::string::npos ||
            stem.find("diff") != std::string::npos) {
          AssignPreferredTexture(preset.albedoPath, pathString);
        } else if (stem.find("normal") != std::string::npos ||
                   stem.find("nor") != std::string::npos) {
          AssignPreferredTexture(preset.normalPath, pathString);
        } else if (stem.find("arm") != std::string::npos ||
                   stem.find("orm") != std::string::npos) {
          AssignPreferredTexture(preset.ormPath, pathString);
        }
      }
    }

    for (const auto &[key, preset] : presetMap) {
      if (preset.IsComplete()) {
        pbrPresets_.push_back(preset);
      }
    }
  }

  const fs::path spriteDir = "Resources/sprite/";
  if (fs::exists(spriteDir)) {
    for (const auto &entry : fs::recursive_directory_iterator(spriteDir)) {
      if (entry.is_regular_file() && IsSupportedTextureFile(entry.path())) {
        spritePaths_.push_back(NormalizeAssetPath(entry.path()));
      }
    }
  }

  const auto sortPaths = [](std::vector<std::string> &paths) {
    std::sort(paths.begin(), paths.end());
  };
  sortPaths(albedoPaths_);
  sortPaths(normalPaths_);
  sortPaths(ormPaths_);
  sortPaths(spritePaths_);
  std::sort(pbrPresets_.begin(), pbrPresets_.end(),
            [](const PbrPreset &lhs, const PbrPreset &rhs) {
              return lhs.name < rhs.name;
            });

  isLoaded_ = true;
}
