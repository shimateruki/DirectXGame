#include "SceneController.h"

#include <utility>

namespace {
// 固有処理を追加しない既定の Scene Controller。基底の何もしないフックをそのまま使う。
class DefaultSceneController final : public ISceneController {};
} // namespace

SceneControllerFactory* SceneControllerFactory::GetInstance() {
    // シーン名と制御クラスの登録表を一つのインスタンスで共有する。
    static SceneControllerFactory instance;
    return &instance;
}

SceneControllerFactory::SceneControllerFactory() {
    // 明示的な制御クラスがないシーンにも既定の動作を用意する。
    RegisterController("DEFAULT", [] { return std::make_unique<DefaultSceneController>(); });
}

// 同名を再登録した時は生成関数だけ差し替え、一覧の順番は保持します。
// シーン制御は名前から生成し、シーン本体とステージ固有の操作を切り離します。
bool SceneControllerFactory::RegisterController(const std::string& name, Creator creator) {
    // 既存名なら生成関数を置き換え、新規登録かどうかを返す。
    if (name.empty() || !creator) {
        return false;
    }
    const bool isNew = creators_.find(name) == creators_.end();
    creators_[name] = std::move(creator);
    if (isNew) {
        registrationOrder_.push_back(name);
    }
    return true;
}

std::unique_ptr<ISceneController> SceneControllerFactory::Create(const std::string& name) const {
    // 未登録または無効な生成関数はnullptrで通知する。
    const auto found = creators_.find(name);
    if (found == creators_.end() || !found->second) {
        return nullptr;
    }
    return found->second();
}

bool SceneControllerFactory::IsRegistered(const std::string& name) const {
    // 生成前の名前検証には登録表だけを参照する。
    return creators_.find(name) != creators_.end();
}
