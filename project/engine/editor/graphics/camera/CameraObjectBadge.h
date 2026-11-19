#pragma once

#ifdef USE_IMGUI
#include "engine/graphics/3d/camera/SceneCameraSettings.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"

// Camera Objectの用途ラベルと色を、HierarchyとGame Viewの両方で共有する。
struct CameraObjectBadge {
    const char* label;
    ImU32 color;
};

// 範囲による切替を優先し、経路終点など範囲を持たないMainは通常色で表示する。
inline CameraObjectBadge GetCameraObjectBadge(const SceneCameraSettings& settings) {
    if (settings.region.enabled)
        return {ICON_FA_VIDEO " [範囲]", IM_COL32(105, 220, 255, 255)};
    if (settings.role == SceneCameraRole::Cinematic)
        return {ICON_FA_VIDEO " [演出]", IM_COL32(255, 135, 218, 255)};
    return {ICON_FA_VIDEO, IM_COL32(218, 223, 230, 255)};
}
#endif
