#include "SceneController.h"

#include <utility>

namespace {
// 固有処理を追加しない既定の Scene Controller。基底の何もしないフックをそのまま使う。
class DefaultSceneController final : public ISceneController {};
} // namespace

SceneControllerFactory* SceneControllerFactory::GetInstance() {
    static SceneControllerFactory instance;
    return &instance;
}

SceneControllerFactory::SceneControllerFactory() {
    RegisterController("DEFAULT", [] { return std::make_unique<DefaultSceneController>(); });
}

// 同名を再登録した時は生成関数だけ差し替え、一覧の順番は保持します。
// シーン制御は名前から生成し、シーン本体とステージ固有の操作を切り離します。
bool SceneControllerFactory::RegisterController(const std::string& name, Creator creator) {
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
    const auto found = creators_.find(name);
    if (found == creators_.end() || !found->second) {
        return nullptr;
    }
    return found->second();
}

bool SceneControllerFactory::IsRegistered(const std::string& name) const {
    return creators_.find(name) != creators_.end();
}
