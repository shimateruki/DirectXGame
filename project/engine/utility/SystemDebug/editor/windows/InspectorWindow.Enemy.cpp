#include "InspectorWindow.h"
#include "DebugEditor.h"
#include "Object3d.h"
#include "imgui.h"

void InspectorWindow::DrawEnemyTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const char* enemyTypes[] = { "Slime", "BossCore", "Bomb", "Bomber", "Mushroom", "GiantSlime", "PrismSlime", "MagmaSlime", "FireSlime", "ThunderSlime", "WindSlime", "Bat", "BeamDrone" };
    std::string currentType = selectedObject->GetEnemyType();

    int currentIndex = -1;
    for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
        if (currentType == enemyTypes[i]) {
            currentIndex = i;
            break;
        }
    }

    const char* previewValue = (currentIndex >= 0) ? enemyTypes[currentIndex] : "(未設定)";

    if (ImGui::BeginCombo("敵の種族 (Enemy Type)", previewValue)) {
        for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
            bool isSelected = (currentIndex == i);
            if (ImGui::Selectable(enemyTypes[i], isSelected)) {
                selectedObject->SetEnemyType(enemyTypes[i]);
                selectedObject->SetName("Enemy_" + std::string(enemyTypes[i]));
                if (std::string(enemyTypes[i]) == "Bat") {
                    selectedObject->SetModel("Characters/bat");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetScale({ 0.72f, 0.72f, 0.72f });
                    selectedObject->animName_ = "ArmatureAction";
                    selectedObject->isAnimLoop_ = true;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(1.25f);
                }
                else if (std::string(enemyTypes[i]) == "BeamDrone") {
                    selectedObject->SetModel("Characters/eye");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetEmissive(1.4f);
                    selectedObject->SetScale({ 0.85f, 0.85f, 0.85f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(1.1f);
                }
                else if (std::string(enemyTypes[i]) == "GiantSlime") {
                    selectedObject->SetModel("Characters/slime");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetEmissive(1.0f);
                    selectedObject->SetScale({ 2.8f, 2.8f, 2.8f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kGround | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(2.2f);
                }
                else if (std::string(enemyTypes[i]) == "PrismSlime") {
                    selectedObject->SetModel("Characters/prism_slime");
                    selectedObject->SetColor({ 0.62f, 0.94f, 1.0f, 1.0f });
                    selectedObject->SetEmissive(1.08f);
                    selectedObject->SetScale({ 4.2f, 4.2f, 4.2f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kGround | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(0.78f);
                }
                else if (std::string(enemyTypes[i]) == "MagmaSlime") {
                    selectedObject->SetModel("Characters/magma_slime");
                    selectedObject->SetMaterialType(0);
                    selectedObject->SetColor({ 1.0f, 0.94f, 0.86f, 1.0f });
                    selectedObject->SetEmissive(1.18f);
                    selectedObject->SetScale({ 3.8f, 3.8f, 3.8f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kGround | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(0.96f);
                }
                else if (std::string(enemyTypes[i]) == "FireSlime") {
                    selectedObject->SetModel("Characters/slime_red");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetEmissive(1.15f);
                    selectedObject->SetScale({ 0.95f, 0.95f, 0.95f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kGround | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(0.95f);
                }
                else if (std::string(enemyTypes[i]) == "ThunderSlime") {
                    selectedObject->SetModel("Characters/slime_yellow");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetEmissive(1.2f);
                    selectedObject->SetScale({ 0.95f, 0.95f, 0.95f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kGround | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(0.95f);
                }
                else if (std::string(enemyTypes[i]) == "WindSlime") {
                    selectedObject->SetModel("Characters/slime_wind");
                    selectedObject->SetColor({ 0.78f, 1.0f, 0.92f, 1.0f });
                    selectedObject->SetEmissive(1.08f);
                    selectedObject->SetScale({ 0.95f, 0.95f, 0.95f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kGround | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(0.95f);
                }
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ロード時に生成される敵クラスを指定します。\nEmptyの場合はただの箱になります。");
#endif
}

