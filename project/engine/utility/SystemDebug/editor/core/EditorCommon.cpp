#include "EditorCommon.h"
#ifdef USE_IMGUI
#include "imgui.h"
#include "ImGuizmo.h"

void EditorCommon::DecomposeWorldPose(const engine::Matrix4x4& world, engine::Vector3& position,
    engine::Vector3& rotation, engine::Vector3& scale) {
    float degrees[3]{};
    ImGuizmo::DecomposeMatrixToComponents(&world.m[0][0], &position.x, degrees, &scale.x);
    constexpr float toRadians = 3.14159265358979323846f / 180.0f;
    rotation = {degrees[0] * toRadians, degrees[1] * toRadians, degrees[2] * toRadians};
}
#endif
