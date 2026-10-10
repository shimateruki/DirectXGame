#pragma once
// 分割実装だけが使う補助関数の宣言です。状態を重複して持たせません。
#define NOMINMAX
#include "engine/core/editor/GameEditorController.h"

#ifdef USE_IMGUI

#include "BaseScene.h"
#include "AssetReferenceExplorer.h"
#include "PlayModeChangeTracker.h"
#include "Player.h"
#include "Camera.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "CaptureToolWindow.h"
#include "CameraManager.h"
#include "DebrisEffectEditor.h"
#include "DebrisEffectManager.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "DirectXCommon.h"
#include "EditorCommandRegistry.h"
#include "EditorToolSettings.h"
#include "KeyConfig.h"
#include "PresetEditor.h"
#include "EditorQuickSearch.h"
#include "EditorAssetDragPayload.h"
#include "EditorManager.h"
#include "EditorTransactionManager.h"
#include "EngineManualWindow.h"
#include "GhostDirector.h"
#include "GhostRecorder.h"
#include "GPUParticleEditor.h"
#include "GPUParticleManager.h"
#include "ImguiManager.h"
#include "InputManager.h"
#include "LightEditor.h"
#include "LightManager.h"
#include "MeshEffectEditor.h"
#include "MeshEffectManager.h"
#include "Object3d.h"
#include "ParticleEditor.h"
#include "PostEffect.h"
#include "PostEffectEditor.h"
#include "ProfilerManager.h"
#include "RenderStats.h"
#include "engine/utility/SystemDebug/editor/windows/replay/ReplayDebugger.h"
#include "ProjectWindow.h"
#include "SceneManager.h"
#include "SceneWorkspace.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SpriteDebugEditor.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "TrailEmitterEditor.h"
#include "VFXSequencer.h"
#include "VFXSequencerEditor.h"
#include "WinApp.h"
#include "IconsFontAwesome5.h"
#include "ImGuizmo.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace GameEditorControllerImplementation {
inline constexpr float kEditorViewportAspect = 16.0f / 9.0f;

// 利用可能領域の中央へ固定アスペクトの描画領域を収めます。
GameViewArea CalculateFixedAspectArea(const ImVec2& canvasScreenPos, const ImVec2& canvasSize);

// 固定アスペクト外の余白をレターボックスとして塗ります。
void DrawViewportLetterbox(const ImVec2& canvasScreenPos, const ImVec2& canvasSize);

// 選択中オブジェクトが現在シーンにまだ存在しているか確認する。

bool IsObjectInCurrentScene(SceneManager* sceneManager, Object3d* object);
// 2Dベクトルの長さを計算する。

float Length2D(const ImVec2& value);
// 2Dベクトルを正規化し、長さがほぼ0ならゼロベクトルを返す。

ImVec2 Normalize2D(const ImVec2& value);
// 原点から方向ベクトルへ指定距離だけ進めた3D座標を返す。

Vector3 AddScaled(const Vector3& origin, const Vector3& direction, float scale);
// 3軸スケールの中で最も大きい絶対値を返し、ギズモ表示サイズの基準にする。

float GetLargestAbsScale(const Vector3& scale);
// 3Dワールド座標をゲームビュー上の2D座標へ投影する。

bool ProjectWorldToGameView(const Vector3& worldPos, const Matrix4x4& viewProjection, const GameViewArea& area, ImVec2& outScreen);
// 2D線分の先端に矢印を付けて描画する。

void DrawArrow2D(ImDrawList* drawList, const ImVec2& start, const ImVec2& end, ImU32 color, float thickness);
// 文字の視認性を上げるため、薄い縁取り付きでテキストを描画する。

void DrawTextWithOutline(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text);
// エディタ画面右上に、現在カメラ基準のワールド軸ギズモを描画する。

void DrawSceneDirectionGizmo(const GameViewArea& area, Camera* camera);
// 選択オブジェクトのローカル軸をゲームビュー上に重ねて描画する。

void DrawSelectedObjectOrientation(const GameViewArea& area, Camera* camera, Object3d* selectedObject);

}

// GameEditorControllerの既定コンストラクタ。メンバ生成はInitializeで行う。


#endif
