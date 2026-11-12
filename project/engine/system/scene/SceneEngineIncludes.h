#pragma once

// 担当：シーン実装で共通利用するエンジンAPIのインクルード集。
// 関連：BaseScene派生の.cppから利用。公開ヘッダーへの一括取込は避ける。

// シーンの .cpp で共通して使うエンジン API。公開ヘッダーからは読み込まない。
#include "engine/graphics/2d/Sprite.h"
#include "engine/graphics/2d/SpriteCommon.h"
#include "engine/graphics/3d/camera/CameraManager.h"
#include "engine/graphics/3d/light/LightManager.h"
#include "engine/graphics/3d/model/ModelManager.h"
#include "engine/graphics/3d/object/Object3D.h"
#include "engine/graphics/3d/object/Object3DCommon.h"
#include "engine/graphics/core/DirectXCommon.h"
#include "engine/graphics/core/TextureManager.h"
#include "engine/graphics/particle/ParticleSystem.h"
#include "engine/system/audio/AudioPlayer.h"
#include "engine/system/collision/CollisionManager.h"
#include "engine/system/input/InputManager.h"
#include "engine/system/scene/SceneManager.h"
