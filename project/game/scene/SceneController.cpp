#include "SceneController.h"

#include <utility>

namespace {
class DefaultSceneController final : public ISceneController {
};
}

SceneControllerFactory* SceneControllerFactory::GetInstance() {
    static SceneControllerFactory instance;
    return &instance;
}

SceneControllerFactory::SceneControllerFactory() {
    RegisterController("DEFAULT", [] { return std::make_unique<DefaultSceneController>(); });
}

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
