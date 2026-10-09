#pragma once

#include "json.hpp"

#include <string>
#include <vector>

namespace cg2::editor {

// ピンが制御フローか値の受け渡しかを表す。入力・出力の向きは NodePin::isInput で指定する。
enum class NodePinKind {
    Flow,
    Value,
};

// 値ピンが扱うデータ型。Flow は制御ピン用の特別な型。
enum class NodeValueType {
    Flow,
    Bool,
    Int,
    Float,
    String,
    Object,
    Effect,
    Scene,
    Unknown,
};

// Inspector に表示するノードプロパティの編集型です。
enum class NodePropertyType {
    Bool,
    Int,
    Float,
    String,
};

// 検証結果の重要度です。
enum class NodeIssueSeverity {
    Info,
    Warning,
    Error,
};

// id はグラフ内で接続先を特定するための識別子。isInput は入出力の向きを表します。
struct NodePin {
    int id = 0;
    std::string name;
    NodePinKind kind = NodePinKind::Flow;
    NodeValueType valueType = NodeValueType::Flow;
    bool isInput = false;
};

// ノードの編集値。type に対応する値だけを有効として扱い、表示名と保存名を分ける。
struct NodeProperty {
    std::string name;
    std::string displayName;
    NodePropertyType type = NodePropertyType::String;
    bool boolValue = false;
    int intValue = 0;
    float floatValue = 0.0f;
    std::string stringValue;
};

// 一つのノードの表示位置、接続ピン、編集可能な値をまとめます。
struct NodeData {
    int id = 0;
    std::string type;
    std::string title;
    std::string note;
    float editorX = 0.0f;
    float editorY = 0.0f;
    std::vector<NodePin> inputs;
    std::vector<NodePin> outputs;
    std::vector<NodeProperty> properties;
};

// startPinId は出力ピン、endPinId は入力ピンの ID です。
struct NodeLink {
    int id = 0;
    int startPinId = 0;
    int endPinId = 0;
};

// 接続検証の診断。nodeId と linkId を使って該当箇所を UI で示す。
struct NodeGraphIssue {
    NodeIssueSeverity severity = NodeIssueSeverity::Info;
    int nodeId = 0;
    int linkId = 0;
    std::string message;
};

// 到達順プレビューの一項目。実行結果やノード処理の完了を記録するものではない。
struct NodeExecutionStep {
    int index = 0;
    int nodeId = 0;
    std::string type;
    std::string title;
    std::string note;
};

// Effect Sequence Graph のデータ本体。ノード・リンクの編集、保存、検証、実行順プレビューを担当する。
class NodeGraphCore {
public:
    // すべてのノードとリンクを破棄して空のグラフに戻す。
    void Clear();
    void ResetToSample();

    // 追加したノードへの参照を返す。座標はノードエディター上の位置。
    NodeData& AddNode(const std::string& type, const std::string& title, float editorX, float editorY);
    NodePin& AddInputPin(NodeData& node, const std::string& name, NodePinKind kind, NodeValueType valueType = NodeValueType::Flow);
    NodePin& AddOutputPin(NodeData& node, const std::string& name, NodePinKind kind, NodeValueType valueType = NodeValueType::Flow);
    NodeProperty& AddProperty(NodeData& node, const std::string& name, const std::string& displayName, NodePropertyType type);
    // 出力から入力への型が合い、入力が未接続の場合だけ追加する。失敗時はnullptr。
    NodeLink* AddLink(int startPinId, int endPinId);

    bool RemoveNode(int nodeId);
    bool RemoveLink(int linkId);

    NodeData* FindNode(int nodeId);
    const NodeData* FindNode(int nodeId) const;
    NodePin* FindPin(int pinId);
    const NodePin* FindPin(int pinId) const;
    NodeData* FindNodeByPin(int pinId);
    const NodeData* FindNodeByPin(int pinId) const;
    NodeLink* FindLink(int linkId);
    const NodeLink* FindLink(int linkId) const;
    const NodeLink* FindIncomingLink(int inputPinId) const;
    const NodeData* FindOutputNodeConnectedTo(int inputPinId) const;

    int CountIncomingLinks(int inputPinId) const;
    int CountOutgoingLinks(int outputPinId) const;

    // 保存先の親ディレクトリを作成する。失敗時はfalseと任意のエラー文を返す。
    bool SaveToFile(const std::string& path, std::string* errorMessage = nullptr) const;
    // ファイルを開けなければ現状態を維持する。解析エラーではグラフを空にする。
    bool LoadFromFile(const std::string& path, std::string* errorMessage = nullptr);

    // 未接続ピン、型不一致、Eventから到達できない制御ノードなどを収集する。
    std::vector<NodeGraphIssue> Validate() const;
    // Eventからの制御フローを幅優先で辿る説明用の順序。実行結果は変更しない。
    std::vector<NodeExecutionStep> BuildExecutionPreview() const;

    std::vector<NodeData>& GetNodes() { return nodes_; }
    const std::vector<NodeData>& GetNodes() const { return nodes_; }
    std::vector<NodeLink>& GetLinks() { return links_; }
    const std::vector<NodeLink>& GetLinks() const { return links_; }

private:
    int AllocateNodeId();
    int AllocatePinId();
    int AllocateLinkId();

    std::vector<NodeData> nodes_;
    std::vector<NodeLink> links_;
    int nextNodeId_ = 1;
    int nextPinId_ = 1001;
    int nextLinkId_ = 2001;
};

std::string ToString(NodePinKind value);
std::string ToString(NodeValueType value);
std::string ToString(NodePropertyType value);
std::string ToString(NodeIssueSeverity value);
NodePinKind ParseNodePinKind(const std::string& value);
NodeValueType ParseNodeValueType(const std::string& value);
NodePropertyType ParseNodePropertyType(const std::string& value);
bool IsCompatibleValueType(NodeValueType outputType, NodeValueType inputType);

} // namespace cg2::editor
