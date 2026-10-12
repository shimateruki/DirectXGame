#pragma once

#include <string>
#include <vector>

// ステージ選択に必要な表示情報と、各種シーン資産への参照を保持します。
struct StageData {
    // id は並び替えても変えない保存用識別子。unlockStageIndex は一覧の配列位置で、-1 は前提なしです。
    std::string id;
    std::string name;
    std::string levelPath;
    std::string spritePath;
    std::string bgmPath;
    std::string description;
    int unlockStageIndex = -1;
    bool defaultUnlocked = false;
    std::string lightPath;
    std::string skyboxPath;
    // ステージセレクトのゲート上に表示する、透過文字画像。空なら名前札を表示しません。
    std::string gateNameTexturePath;
};

// ステージ JSON の検証と変換を担当し、現在選択中のステージ状態は保持しません。
class StageCatalog {
  public:
    /// <summary>有効なステージだけを読み込みます。失敗時は output を変更しません。</summary>
    /// <param name="path">ステージ一覧 JSON のパス</param>
    /// <param name="output">読み込み成功時に置き換えるステージ一覧</param>
    static bool LoadFromJson(const std::string& path, std::vector<StageData>& output);

    // 外部 JSON が利用できない開発環境でも最低限のステージへ入れるよう、内蔵一覧を返します。
    static std::vector<StageData> CreateFallback();
};
