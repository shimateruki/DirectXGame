#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.Internal.h"

using namespace CameraEditorImplementation;

// 補助処理の定義を一か所に保持し、共有状態と呼び出し順を維持します。
namespace CameraEditorImplementation {
    bool ShouldHideGameCameraGuideDuringPlay() {
        SceneManager* sceneManager = SceneManager::GetInstance();
        return sceneManager && sceneManager->IsPlaying();
    }

    BaseScene* GetCurrentScene() {
        SceneManager* sceneManager = SceneManager::GetInstance();
        return sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    }

    bool IsObjectInCurrentScene(const Object3d* target) {
        BaseScene* scene = GetCurrentScene();
        if (!scene || !target) {
            return false;
        }
        return std::any_of(scene->GetObjects().begin(), scene->GetObjects().end(), [target](const std::unique_ptr<Object3d>& object) {
            return object.get() == target;
        });
    }

    float DegToRad(float degrees) {
        return degrees * kPi / 180.0f;
    }

    float RadToDeg(float radians) {
        return radians * 180.0f / kPi;
    }

    Matrix4x4 MakeLineBoxMatrix(const Vector3& start, const Vector3& end, float thickness) {
        Math math;
        Vector3 diff = end - start;
        float length = math.Length(diff);
        if (length < 0.001f) {
            length = 0.001f;
        }

        Vector3 center = (start + end) * 0.5f;
        float yaw = std::atan2(diff.x, diff.z);
        float horizontalLength = std::sqrt(diff.x * diff.x + diff.z * diff.z);
        float pitch = std::atan2(-diff.y, horizontalLength);

        Matrix4x4 scale = math.MakeScaleMatrix({ thickness, thickness, length });
        Matrix4x4 rotate = math.Multiply(math.MakeRotateXMatrix(pitch), math.MakeRotateYMatrix(yaw));
        Matrix4x4 translate = math.MakeTranslateMatrix(center);
        return math.Multiply(math.Multiply(scale, rotate), translate);
    }

    Vector3 CalculateForward(const Vector3& rotation) {
        // rotation.x = Pitch, rotation.y = Yaw
        float x = std::sin(rotation.y) * std::cos(rotation.x);
        float y = std::sin(rotation.x); // Y-Upの場合、Pitch上がプラスならsin(x)ではなく-sin(x)の場合も
        float z = std::cos(rotation.y) * std::cos(rotation.x);
        return { x, y, z };
    }

    Vector3 CalculateEditorForward(const Vector3& rotation) {
        float x = std::sin(rotation.y) * std::cos(rotation.x);
        float y = -std::sin(rotation.x);
        float z = std::cos(rotation.y) * std::cos(rotation.x);
        return { x, y, z };
    }

    Vector3 Cross(const Vector3& a, const Vector3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    float Length(const Vector3& v) {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    Vector3 NormalizeSafe(const Vector3& v, const Vector3& fallback) {
        float length = Length(v);
        if (length < 0.0001f) {
            return fallback;
        }
        return { v.x / length, v.y / length, v.z / length };
    }

    Vector3 MakeRotationFromForward(const Vector3& forward) {
        Vector3 normalized = NormalizeSafe(forward, { 0.0f, 0.0f, 1.0f });
        float pitch = std::asin(std::clamp(-normalized.y, -1.0f, 1.0f));
        float yaw = std::atan2(normalized.x, normalized.z);
        return { pitch, yaw, 0.0f };
    }

    Vector3 GetObjectWorldForward(const Object3d* object) {
        if (!object) {
            return { 0.0f, 0.0f, 1.0f };
        }
        Vector3 forward = Math::TransformNormal({ 0.0f, 0.0f, 1.0f }, object->GetWorldMatrix());
        return NormalizeSafe(forward, { 0.0f, 0.0f, 1.0f });
    }

    bool IsValidFocusBounds(const AABB& bounds) {
        return std::isfinite(bounds.min.x) && std::isfinite(bounds.min.y) && std::isfinite(bounds.min.z) &&
            std::isfinite(bounds.max.x) && std::isfinite(bounds.max.y) && std::isfinite(bounds.max.z) &&
            bounds.max.x >= bounds.min.x && bounds.max.y >= bounds.min.y && bounds.max.z >= bounds.min.z;
    }
}
