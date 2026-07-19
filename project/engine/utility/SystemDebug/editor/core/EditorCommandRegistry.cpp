#include "EditorCommandRegistry.h"

#include <algorithm>
#include <utility>

EditorCommandRegistry* EditorCommandRegistry::GetInstance() {
    static EditorCommandRegistry instance;
    return &instance;
}

bool EditorCommandRegistry::Register(EditorCommand command) {
    if (command.id.empty() || command.displayName.empty() || !command.execute) {
        return false;
    }

    auto existing = commands_.find(command.id);
    if (existing != commands_.end()) {
        if (existing->second.owner != command.owner) {
            return false;
        }
        existing->second = std::move(command);
        ++revision_;
        return true;
    }

    const std::string id = command.id;
    commands_.emplace(id, std::move(command));
    registrationOrder_.push_back(id);
    ++revision_;
    return true;
}

bool EditorCommandRegistry::Unregister(std::string_view id, const void* owner) {
    const std::string key(id);
    auto it = commands_.find(key);
    if (it == commands_.end() || (owner && it->second.owner != owner)) {
        return false;
    }

    commands_.erase(it);
    registrationOrder_.erase(
        std::remove(registrationOrder_.begin(), registrationOrder_.end(), key),
        registrationOrder_.end());
    ++revision_;
    return true;
}

void EditorCommandRegistry::UnregisterOwner(const void* owner) {
    if (!owner) {
        return;
    }

    bool removed = false;
    registrationOrder_.erase(
        std::remove_if(
            registrationOrder_.begin(),
            registrationOrder_.end(),
            [this, owner, &removed](const std::string& id) {
                const auto it = commands_.find(id);
                if (it == commands_.end()) {
                    return true;
                }
                if (it->second.owner != owner) {
                    return false;
                }
                commands_.erase(it);
                removed = true;
                return true;
            }),
        registrationOrder_.end());

    if (removed) {
        ++revision_;
    }
}

void EditorCommandRegistry::Clear() {
    if (commands_.empty() && registrationOrder_.empty()) {
        return;
    }
    commands_.clear();
    registrationOrder_.clear();
    ++revision_;
}

const EditorCommand* EditorCommandRegistry::Find(std::string_view id) const {
    const auto it = commands_.find(std::string(id));
    return it != commands_.end() ? &it->second : nullptr;
}

bool EditorCommandRegistry::Contains(std::string_view id) const {
    return Find(id) != nullptr;
}

bool EditorCommandRegistry::CanExecute(std::string_view id) const {
    const EditorCommand* command = Find(id);
    if (!command || !command->execute) {
        return false;
    }
    return !command->canExecute || command->canExecute();
}

bool EditorCommandRegistry::Execute(std::string_view id) const {
    const EditorCommand* command = Find(id);
    if (!command || !command->execute || (command->canExecute && !command->canExecute())) {
        return false;
    }
    command->execute();
    return true;
}

std::vector<const EditorCommand*> EditorCommandRegistry::GetCommands() const {
    std::vector<const EditorCommand*> result;
    result.reserve(registrationOrder_.size());
    for (const std::string& id : registrationOrder_) {
        const auto it = commands_.find(id);
        if (it != commands_.end()) {
            result.push_back(&it->second);
        }
    }
    return result;
}
