#pragma once

#include "IEditable.h"
#include <string>
#include <vector>

class DebugEditor;
class Object3d;
class SceneManager;

/// Object同士のイベントIDと送信先IDを一覧化し、リンク切れや重複を見つけるデバッグウィンドウ。
class EventLinkGraph : public IEditable {
public:
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "Event Link Graph"; }

private:
    /// 1つのObjectが持つイベントID、送信先ID、異常状態を表示用にまとめる。
    struct NodeInfo {
        Object3d* object = nullptr;
        std::string name;
        int eventID = -1;
        int targetID = -1;
        bool hasMissingTarget = false;
        bool hasDuplicateID = false;
    };

    /// 現在シーンのObjectを走査し、イベントリンク表示用のノード一覧を作る。
    void CollectNodes();
    /// 指定IDを持つObjectが他に存在するかを調べ、送信先チェックに使う。
    bool HasEventID(int id, Object3d* ignoreObject) const;
    /// 同じイベントIDを持つObject数を数え、重複警告に使う。
    int CountEventID(int id) const;

    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;
    std::vector<NodeInfo> nodes_;
    bool showOnlyLinked_ = false;
};
