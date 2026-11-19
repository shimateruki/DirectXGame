#pragma once

#include <string>

class BaseScene;
namespace engine { class Object3D; }

// メッシュVFXの追従名をScene登録順と子の深さ優先順で探す。空名や一致がない場合はnullptrを返す。
engine::Object3D* FindMeshEffectTarget(BaseScene* scene, const std::string& name);
