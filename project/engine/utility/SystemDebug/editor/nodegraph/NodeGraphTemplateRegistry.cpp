#include "NodeGraphTemplateRegistry.h"

#include <algorithm>
#include <utility>

namespace cg2::editor {
namespace {

NodePinDefinition FlowInput(const char* name = "入") {
    return NodePinDefinition{name, NodePinKind::Flow, NodeValueType::Flow, true};
}

NodePinDefinition FlowOutput(const char* name = "出") {
    return NodePinDefinition{name, NodePinKind::Flow, NodeValueType::Flow, false};
}

NodePinDefinition ValueInput(const char* name, NodeValueType type, bool required = false) {
    return NodePinDefinition{name, NodePinKind::Value, type, required};
}

NodePinDefinition ValueOutput(const char* name, NodeValueType type) {
    return NodePinDefinition{name, NodePinKind::Value, type, false};
}

NodePropertyDefinition StringProperty(const char* name, const char* displayName, const char* value = "", bool required = false) {
    NodePropertyDefinition property;
    property.name = name;
    property.displayName = displayName;
    property.type = NodePropertyType::String;
    property.stringValue = value;
    property.required = required;
    return property;
}

NodePropertyDefinition FloatProperty(const char* name, const char* displayName, float value = 0.0f, bool required = false) {
    NodePropertyDefinition property;
    property.name = name;
    property.displayName = displayName;
    property.type = NodePropertyType::Float;
    property.floatValue = value;
    property.required = required;
    return property;
}

NodePropertyDefinition BoolProperty(const char* name, const char* displayName, bool value = false, bool required = false) {
    NodePropertyDefinition property;
    property.name = name;
    property.displayName = displayName;
    property.type = NodePropertyType::Bool;
    property.boolValue = value;
    property.required = required;
    return property;
}

NodeTemplateDefinition MakeTemplate(
    const char* type,
    const char* title,
    const char* description,
    const char* note,
    NodeTemplateCategory category,
    NodeExecutionKind executionKind,
    NodeDryRunBehavior dryRunBehavior,
    std::vector<NodePinDefinition> inputs,
    std::vector<NodePinDefinition> outputs,
    std::vector<NodePropertyDefinition> properties = {}) {

    NodeTemplateDefinition definition;
    definition.type = type;
    definition.title = title;
    definition.description = description;
    definition.defaultNote = note;
    definition.category = category;
    definition.executionKind = executionKind;
    definition.dryRunBehavior = dryRunBehavior;
    definition.inputs = std::move(inputs);
    definition.outputs = std::move(outputs);
    definition.properties = std::move(properties);
    return definition;
}

} // namespace

const NodeGraphTemplateRegistry& NodeGraphTemplateRegistry::Instance() {
    static NodeGraphTemplateRegistry instance;
    return instance;
}

NodeGraphTemplateRegistry::NodeGraphTemplateRegistry() {
    templates_.push_back(MakeTemplate(
        "Event.Start",
        "開始",
        "最初に実行されるイベントノードです。検証やサンプル用の入口として使います。",
        "このノードから制御フローが流れます。",
        NodeTemplateCategory::Event,
        NodeExecutionKind::Event,
        NodeDryRunBehavior::BuiltIn,
        {},
        {FlowOutput("実行")}));

    templates_.push_back(MakeTemplate(
        "Event.OnEnterGate",
        "ゲート接触",
        "プレイヤーがステージゲートへ入った時に開始するイベントです。",
        "ゲート突入演出の開始地点です。",
        NodeTemplateCategory::Event,
        NodeExecutionKind::Event,
        NodeDryRunBehavior::BuiltIn,
        {},
        {FlowOutput("実行")}));

    templates_.push_back(MakeTemplate(
        "Event.OnHit",
        "接触イベント",
        "敵やギミックとの接触を入口にするイベントです。今はテンプレートだけ用意しています。",
        "将来的に接触演出やダメージ演出の入口にできます。",
        NodeTemplateCategory::Event,
        NodeExecutionKind::Event,
        NodeDryRunBehavior::BuiltIn,
        {},
        {FlowOutput("実行")}));

    templates_.push_back(MakeTemplate(
        "Flow.Wait",
        "待機",
        "指定秒数だけ次の処理へ進むのを待ちます。演出の間を作る基本ノードです。",
        "短い余韻やフェード前の待ち時間に使います。",
        NodeTemplateCategory::Flow,
        NodeExecutionKind::FlowControl,
        NodeDryRunBehavior::BuiltIn,
        {FlowInput()},
        {FlowOutput()},
        {FloatProperty("seconds", "待機秒数", 0.35f, true)}));

    templates_.push_back(MakeTemplate(
        "Flow.Branch",
        "条件分岐",
        "Bool 値によって True / False の実行先を切り替える予定のノードです。現時点では基礎テンプレートです。",
        "Data.Bool と接続して条件分岐に使う想定です。",
        NodeTemplateCategory::Flow,
        NodeExecutionKind::FlowControl,
        NodeDryRunBehavior::None,
        {FlowInput(), ValueInput("条件", NodeValueType::Bool, true)},
        {FlowOutput("True"), FlowOutput("False")}));

    templates_.push_back(MakeTemplate(
        "Action.LockPlayerControl",
        "操作停止",
        "ムービー中にプレイヤー入力を受け付けない状態へ切り替えます。",
        "演出開始直後に入れて、プレイヤー操作と演出が競合しないようにします。",
        NodeTemplateCategory::Action,
        NodeExecutionKind::Action,
        NodeDryRunBehavior::Immediate,
        {FlowInput()},
        {FlowOutput()},
        {BoolProperty("locked", "ロックする", true)}));

    templates_.push_back(MakeTemplate(
        "Action.AlignToGateEntry",
        "ゲート正面へ補間",
        "プレイヤーをゲート正面の安全な入口位置へ補間して、カメラに対して見やすい状態を作ります。",
        "ゲートの端や裏側から触れても演出が破綻しないようにするための整列処理です。",
        NodeTemplateCategory::Action,
        NodeExecutionKind::Action,
        NodeDryRunBehavior::Timed,
        {FlowInput(), ValueInput("ゲート", NodeValueType::Object)},
        {FlowOutput()},
        {FloatProperty("duration", "補間時間", 0.35f)}));

    templates_.push_back(MakeTemplate(
        "Action.CameraGateEntry",
        "カメラ固定演出",
        "ゲート突入を見せやすい位置へカメラを補間し、演出中は追従を一時的に止めます。",
        "カメラめり込みや追尾で何が起きたかわからない問題を避けるためのノードです。",
        NodeTemplateCategory::Action,
        NodeExecutionKind::Action,
        NodeDryRunBehavior::Timed,
        {FlowInput(), ValueInput("ゲート", NodeValueType::Object)},
        {FlowOutput()},
        {FloatProperty("duration", "補間時間", 0.45f)}));

    templates_.push_back(MakeTemplate(
        "Action.MoveToGateInside",
        "ゲートへ吸い込み",
        "プレイヤーをゲート中心へ吸い込ませる移動演出です。",
        "小さくするだけではなく、ゲートの奥へ入っていくように見せるための中心移動です。",
        NodeTemplateCategory::Action,
        NodeExecutionKind::Action,
        NodeDryRunBehavior::Timed,
        {FlowInput(), ValueInput("ゲート", NodeValueType::Object)},
        {FlowOutput()},
        {FloatProperty("duration", "移動時間", 0.55f)}));

    templates_.push_back(MakeTemplate(
        "Action.PlayEffect",
        "エフェクト再生",
        "指定したエフェクトを対象位置で再生します。粒子や衝撃波のような補助演出に使います。",
        "ゲート中心やプレイヤー位置に、吸い込まれる粒子を出す想定です。",
        NodeTemplateCategory::Action,
        NodeExecutionKind::Action,
        NodeDryRunBehavior::Immediate,
        {FlowInput(), ValueInput("対象", NodeValueType::Object), ValueInput("エフェクト", NodeValueType::Effect)},
        {FlowOutput()},
        {StringProperty("effectName", "エフェクト名", "gate_absorb_spark") }));

    templates_.push_back(MakeTemplate(
        "Action.PlaySE",
        "SE再生",
        "演出に合わせた効果音を再生します。",
        "音を入れると演出のタイミング確認がしやすくなります。",
        NodeTemplateCategory::Action,
        NodeExecutionKind::Action,
        NodeDryRunBehavior::Immediate,
        {FlowInput()},
        {FlowOutput()},
        {StringProperty("soundName", "SE名", "gate_enter") }));

    templates_.push_back(MakeTemplate(
        "Action.DissolvePlayer",
        "プレイヤーディゾルブ",
        "プレイヤーを粒子化またはディゾルブして、ゲートへ消える見え方を作ります。",
        "単純非表示ではなく、ゲートと馴染ませて消すためのノードです。",
        NodeTemplateCategory::Action,
        NodeExecutionKind::Action,
        NodeDryRunBehavior::Timed,
        {FlowInput(), ValueInput("対象", NodeValueType::Object)},
        {FlowOutput()},
        {FloatProperty("duration", "消える時間", 0.4f)}));

    templates_.push_back(MakeTemplate(
        "Action.Fade",
        "画面フェード",
        "画面全体をフェードさせます。シーン遷移の直前に入れると切り替えが自然になります。",
        "ゲート突入後、画面を隠すために使います。",
        NodeTemplateCategory::Action,
        NodeExecutionKind::Action,
        NodeDryRunBehavior::Timed,
        {FlowInput()},
        {FlowOutput()},
        {FloatProperty("duration", "フェード時間", 0.45f), StringProperty("color", "色", "black")}));

    templates_.push_back(MakeTemplate(
        "Action.SetVisible",
        "表示切替",
        "対象オブジェクトの表示状態を切り替えます。補助的な制御に使います。",
        "演出完了後に見せたくないものを隠す用途です。",
        NodeTemplateCategory::Action,
        NodeExecutionKind::Action,
        NodeDryRunBehavior::Immediate,
        {FlowInput(), ValueInput("対象", NodeValueType::Object)},
        {FlowOutput()},
        {BoolProperty("visible", "表示する", false)}));

    templates_.push_back(MakeTemplate(
        "Action.ChangeScene",
        "シーン遷移",
        "指定したシーンへ切り替えます。演出グラフの最後に置く想定です。",
        "フェード完了後にステージへ移動します。",
        NodeTemplateCategory::Action,
        NodeExecutionKind::Action,
        NodeDryRunBehavior::Immediate,
        {FlowInput(), ValueInput("シーン", NodeValueType::Scene)},
        {},
        {StringProperty("sceneName", "遷移先", "stage1", true)}));

    templates_.push_back(MakeTemplate(
        "Data.Bool",
        "Bool値",
        "true / false の値を出力する Data ノードです。条件分岐の基礎として使います。",
        "値ピンの接続テスト用です。",
        NodeTemplateCategory::Data,
        NodeExecutionKind::Data,
        NodeDryRunBehavior::BuiltIn,
        {},
        {ValueOutput("値", NodeValueType::Bool)},
        {BoolProperty("value", "値", false)}));

    templates_.push_back(MakeTemplate(
        "Data.Float",
        "Float値",
        "小数値を出力する Data ノードです。待機時間や補間時間の入力に使う想定です。",
        "将来的に数値ピンへ接続できるようにするための基礎です。",
        NodeTemplateCategory::Data,
        NodeExecutionKind::Data,
        NodeDryRunBehavior::BuiltIn,
        {},
        {ValueOutput("値", NodeValueType::Float)},
        {FloatProperty("value", "値", 1.0f)}));

    templates_.push_back(MakeTemplate(
        "Data.String",
        "文字列",
        "文字列を出力する Data ノードです。ログ、SE名、シーン名などへ接続する想定です。",
        "名前系データをノード上で扱うための基礎です。",
        NodeTemplateCategory::Data,
        NodeExecutionKind::Data,
        NodeDryRunBehavior::BuiltIn,
        {},
        {ValueOutput("値", NodeValueType::String)},
        {StringProperty("value", "値", "") }));

    templates_.push_back(MakeTemplate(
        "Data.ObjectRef",
        "Object参照",
        "シーン上のオブジェクト参照を表す Data ノードです。今は名前文字列を保持する基礎段階です。",
        "ゲートやプレイヤーなどの対象指定に使う想定です。",
        NodeTemplateCategory::Data,
        NodeExecutionKind::Data,
        NodeDryRunBehavior::BuiltIn,
        {},
        {ValueOutput("対象", NodeValueType::Object)},
        {StringProperty("objectName", "オブジェクト名", "Player") }));

    templates_.push_back(MakeTemplate(
        "Data.EffectRef",
        "Effect参照",
        "エフェクト名を出力する Data ノードです。今は名前文字列を保持する基礎段階です。",
        "PlayEffect へ接続する想定です。",
        NodeTemplateCategory::Data,
        NodeExecutionKind::Data,
        NodeDryRunBehavior::BuiltIn,
        {},
        {ValueOutput("エフェクト", NodeValueType::Effect)},
        {StringProperty("effectName", "エフェクト名", "gate_absorb_spark") }));

    templates_.push_back(MakeTemplate(
        "Data.SceneRef",
        "Scene参照",
        "遷移先シーン名を出力する Data ノードです。今は名前文字列を保持する基礎段階です。",
        "ChangeScene へ接続する想定です。",
        NodeTemplateCategory::Data,
        NodeExecutionKind::Data,
        NodeDryRunBehavior::BuiltIn,
        {},
        {ValueOutput("シーン", NodeValueType::Scene)},
        {StringProperty("sceneName", "シーン名", "stage1") }));

    templates_.push_back(MakeTemplate(
        "Debug.Log",
        "ログ表示",
        "ドライラン時にメッセージを出します。処理順の確認に使います。",
        "ノード実行順を確認するためのログです。",
        NodeTemplateCategory::Debug,
        NodeExecutionKind::Debug,
        NodeDryRunBehavior::BuiltIn,
        {FlowInput()},
        {FlowOutput()},
        {StringProperty("message", "表示文字", "ノード実行") }));

    templates_.push_back(MakeTemplate(
        "Comment",
        "コメント",
        "実行されないメモ用ノードです。演出意図や調整メモを書き残します。",
        "ここに設計メモを書きます。",
        NodeTemplateCategory::Utility,
        NodeExecutionKind::Utility,
        NodeDryRunBehavior::BuiltIn,
        {},
        {},
        {StringProperty("text", "メモ", "") }));
}

const NodeTemplateDefinition* NodeGraphTemplateRegistry::Find(const std::string& type) const {
    const auto it = std::find_if(templates_.begin(), templates_.end(), [&](const NodeTemplateDefinition& definition) {
        return definition.type == type;
    });
    return it != templates_.end() ? &(*it) : nullptr;
}

std::vector<const NodeTemplateDefinition*> NodeGraphTemplateRegistry::GetTemplatesByCategory(NodeTemplateCategory category) const {
    std::vector<const NodeTemplateDefinition*> result;
    for (const NodeTemplateDefinition& definition : templates_) {
        if (definition.category == category) {
            result.push_back(&definition);
        }
    }
    return result;
}

std::vector<NodeTemplateCategory> NodeGraphTemplateRegistry::GetDisplayCategories() const {
    return {
        NodeTemplateCategory::Event,
        NodeTemplateCategory::Flow,
        NodeTemplateCategory::Action,
        NodeTemplateCategory::Data,
        NodeTemplateCategory::Debug,
        NodeTemplateCategory::Utility,
    };
}

NodeData* NodeGraphTemplateRegistry::CreateNode(NodeGraphCore& graph, const std::string& type, float editorX, float editorY) const {
    const NodeTemplateDefinition* definition = Find(type);
    if (!definition) {
        return nullptr;
    }

    NodeData& node = graph.AddNode(definition->type, definition->title, editorX, editorY);
    node.note = definition->defaultNote;

    for (const NodePinDefinition& pin : definition->inputs) {
        graph.AddInputPin(node, pin.name, pin.kind, pin.valueType);
    }
    for (const NodePinDefinition& pin : definition->outputs) {
        graph.AddOutputPin(node, pin.name, pin.kind, pin.valueType);
    }
    for (const NodePropertyDefinition& propertyDefinition : definition->properties) {
        NodeProperty& property = graph.AddProperty(node, propertyDefinition.name, propertyDefinition.displayName, propertyDefinition.type);
        property.boolValue = propertyDefinition.boolValue;
        property.intValue = propertyDefinition.intValue;
        property.floatValue = propertyDefinition.floatValue;
        property.stringValue = propertyDefinition.stringValue;
    }

    return &node;
}

bool NodeGraphTemplateRegistry::IsEventNode(const std::string& type) const {
    const NodeTemplateDefinition* definition = Find(type);
    return definition && definition->executionKind == NodeExecutionKind::Event;
}

bool NodeGraphTemplateRegistry::IsCommentNode(const std::string& type) const {
    return type == "Comment";
}

bool NodeGraphTemplateRegistry::IsDataNode(const std::string& type) const {
    const NodeTemplateDefinition* definition = Find(type);
    return definition && definition->executionKind == NodeExecutionKind::Data;
}

NodeDryRunBehavior NodeGraphTemplateRegistry::GetDryRunBehavior(const std::string& type) const {
    const NodeTemplateDefinition* definition = Find(type);
    return definition ? definition->dryRunBehavior : NodeDryRunBehavior::None;
}

const char* NodeGraphTemplateRegistry::ToDisplayName(NodeTemplateCategory category) {
    switch (category) {
    case NodeTemplateCategory::Event:
        return "Event / 開始条件";
    case NodeTemplateCategory::Flow:
        return "Flow / 制御";
    case NodeTemplateCategory::Action:
        return "Action / 演出処理";
    case NodeTemplateCategory::Data:
        return "Data / 値";
    case NodeTemplateCategory::Debug:
        return "Debug / 確認";
    case NodeTemplateCategory::Utility:
        return "Utility / 補助";
    default:
        return "Unknown";
    }
}

const char* NodeGraphTemplateRegistry::ToString(NodeExecutionKind kind) {
    switch (kind) {
    case NodeExecutionKind::Event:
        return "Event";
    case NodeExecutionKind::FlowControl:
        return "Flow";
    case NodeExecutionKind::Action:
        return "Action";
    case NodeExecutionKind::Data:
        return "Data";
    case NodeExecutionKind::Utility:
        return "Utility";
    case NodeExecutionKind::Debug:
        return "Debug";
    default:
        return "Unknown";
    }
}

} // namespace cg2::editor
