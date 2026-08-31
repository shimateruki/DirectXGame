#pragma once

#include "BaseGimmick.h"

#include <array>
#include <memory>
#include <string>

class Player;

// ボス前で3種類のコピー能力から一つを、実物のコアへ触れて選択する記憶台です。
class GimmickCopyMemoryStation final : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    bool OnCollision(Object3d* other) override;
    void OnTrigger() override;
    void OnSwitchEvent(bool active) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    static constexpr size_t kCoreCount = 3;

    void BeginPlayState();
    void ResetEditorState();
    void UpdateCoreVisuals(float deltaTime);
    void ConfigureCore(size_t index, const std::string& enemyType);
    Vector3 GetCoreWorldPosition(size_t index) const;
    std::string GetConfiguredType(size_t index) const;
    int FindTouchedCore(const Vector3& playerPosition) const;
    void ActivateCore(Player& player, size_t index);

    std::array<std::unique_ptr<Object3d>, kCoreCount> coreVisuals_{};
    std::array<std::string, kCoreCount> displayedTypes_{};
    std::array<float, kCoreCount> corePulseTimers_{};
    float visualTimer_ = 0.0f;
    float selectionCooldown_ = 0.0f;
    int latchedCore_ = -1;
    bool initializedForPlay_ = false;
    bool runtimeEnabled_ = true;
    bool collisionObserved_ = false;
};
