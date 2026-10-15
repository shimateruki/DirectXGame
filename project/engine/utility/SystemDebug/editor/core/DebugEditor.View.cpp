#define NOMINMAX
#include "DebugEditor.h"
#include "game/ui/SettingsMenuOverlay.h"
#include "SceneManager.h"    
#include "BaseScene.h"      
#include "Object3d.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <fstream>
#include <string>
#include <vector>
#include "json.hpp"
#include "ImGuizmo.h"
#include "CameraManager.h"
#include "WinApp.h"
#include "Math.h"
#include "DirectXCommon.h"
#include "CollisionConfig.h"
#include "ModelManager.h"      
#include "InputManager.h"    
#include <cmath>
#include <cassert> 
#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include "GhostRecorder.h" 
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "Transform.h"
#include "ParticleManager.h"
#include "EditorManager.h"
#include "PostEffectEditor.h"
#include "SpriteDebugEditor.h"
#include "ParticleEditor.h"
#include "GPUParticleEditor.h"
#include "VFXSequencerEditor.h"
#include "LightEditor.h"      
#include "IconsFontAwesome5.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <DebugConsole.h>
#include <CollisionManager.h>
#include <filesystem> // ファイル操作用
#include <BulletManager.h>
#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.h"
#include <PresetEditor.h>
#include <MeshEffectManager.h>
#include <unordered_map>
#include <utility>
namespace fs = std::filesystem;




namespace {
    // 実ゲームでは不可視のカメラ等も、Editorのプレビューでは識別できる描画クラスへ切り替えます。
    bool ShouldUsePreviewDrawClassOverride(const engine::Object3d* object) {
        if (!object) return false;

        const std::string className = object->GetClassName();
        return object->IsCameraObject() ||
            className == "GPUParticle" ||
            className == "InvisibleBox";
    }
}

// ========================================================================
// 初期化
// ========================================================================

engine::Ray DebugEditor::ScreenPointToRay(const engine::Vector2& mousePos) {
    const Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return engine::Ray{};

    engine::Matrix4x4 matView = camera->GetViewMatrix();
    engine::Matrix4x4 matProj = camera->GetProjectionMatrix();
    engine::Matrix4x4 matViewProj = math_.Multiply(matView, matProj);
    engine::Matrix4x4 matInverseVP = math_.Inverse(matViewProj);


    float localMouseX = mousePos.x;
    float localMouseY = mousePos.y;

    // 2. 範囲外チェック
    if (localMouseX < 0 || localMouseX > gameViewSize_.x ||
        localMouseY < 0 || localMouseY > gameViewSize_.y) {
        return engine::Ray{ {0,0,0}, {0,0,0} };
    }

    // 3. NDC変換
    engine::Vector3 nearPos, farPos;
    nearPos.x = (2.0f * localMouseX) / gameViewSize_.x - 1.0f;
    nearPos.y = 1.0f - (2.0f * localMouseY) / gameViewSize_.y;
    nearPos.z = 0.0f;

    farPos = nearPos;
    farPos.z = 1.0f;

    // 4. ワールド変換
    engine::Vector3 worldNear = math_.Transform(nearPos, matInverseVP);
    engine::Vector3 worldFar = math_.Transform(farPos, matInverseVP);

    engine::Ray ray;
    ray.origin = worldNear;
    ray.diff = worldFar - worldNear;

    return ray;
}

// ---------------------------------------------------------
// レイと平面(Y=0)の交点を計算する関数
// 戻り値: 交差したら true, その座標を intersectOut に入れる
// ---------------------------------------------------------
bool DebugEditor::IntersectRayPlane(const engine::Ray& ray, engine::Vector3& intersectOut) {
    // 計算用のMathクラス
    engine::Math math;

    // 平面の定義（上向きの法線, 高さ0）
    engine::Vector3 planeNormal = { 0.0f, 1.0f, 0.0f };
    float planeHeight = 0.0f;

    // 平行かどうかチェック (内積が0に近い＝平行)
    // レイの方向ベクトルと平面の法線の内積をとる
    float denominator = math.Dot(ray.diff, planeNormal);

    // レイが水平に近いなら判定しない (0除算防止)
    if (std::abs(denominator) < 0.0001f) {
        return false;
    }

    // 交点までの距離 t を求める公式
    // t = (PlaneHeight - Dot(RayOrigin, PlaneNormal)) / Dot(RayDir, PlaneNormal)

    // まずレイの方向を正規化する
    engine::Vector3 rayDir = math.Normalize(ray.diff);
    float dotDir = math.Dot(rayDir, planeNormal);

    // 念のため再チェック
    if (std::abs(dotDir) < 0.0001f) return false;

    // 距離tの計算
    float t = (planeHeight - math.Dot(ray.origin, planeNormal)) / dotDir;

    // tがマイナス＝カメラの後ろ側にあるので無視
    if (t < 0.0f) {
        return false;
    }

    // 交点座標 = 原点 + 方向 * 距離
    // intersectOut = ray.origin + (rayDir * t)
    engine::Vector3 travel = { rayDir.x * t, rayDir.y * t, rayDir.z * t };
    intersectOut = { ray.origin.x + travel.x, ray.origin.y + travel.y, ray.origin.z + travel.z };

    return true;
}

void DebugEditor::DrawPreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // 編集プレビューの描画時は、現在のライト定数を表示専用の描画経路へ渡す。
    auto drawPreviewObject = [&](engine::Object3d* object) {
        if (!object) return;

        std::string originalClassName;
        bool useClassOverride = ShouldUsePreviewDrawClassOverride(object);
        if (useClassOverride) {
            originalClassName = object->GetClassName();
            object->SetClassName("Model");
        }
        object->Draw(pointLightResource, spotLightResource);
        if (useClassOverride) {
            object->SetClassName(originalClassName);
        }
    };

    if (previewObject_) {
        drawPreviewObject(previewObject_.get());
        for (auto& child : previewChildObjects_) {
            drawPreviewObject(child.get());
        }
    }

    for (auto& object : brushPreviewObjects_) {
        drawPreviewObject(object.get());
    }

}

// ========================================================================
// セーブ通知のトリガー
// ========================================================================
void DebugEditor::TriggerSaveNotification(const std::string& filename) {
#ifdef USE_IMGUI
    saveNotificationTimer_ = 1.0f;
    saveNotificationMsg_ = "[ " + filename + " ] にセーブしました";
#endif
}

void DebugEditor::PollDDSCacheNotifications() {
#ifdef USE_IMGUI
    // 変換ツールの追記ログを前回の読み取り位置から読み、完了通知だけを表示する。
    const fs::path notificationLog = "Resources/.cache/dds_cache_notifications.jsonl";
    if (!fs::exists(notificationLog)) {
        ddsCacheNotificationReadOffset_ = 0;
        return;
    }

    std::error_code ec;
    const std::uintmax_t fileSize = fs::file_size(notificationLog, ec);
    if (ec) {
        return;
    }
    if (fileSize < ddsCacheNotificationReadOffset_) {
        // ログのローテートや作り直しでサイズが縮んだ場合は先頭から読み直す。
        ddsCacheNotificationReadOffset_ = 0;
    }
    if (fileSize == ddsCacheNotificationReadOffset_) {
        return;
    }

    std::ifstream ifs(notificationLog, std::ios::binary);
    if (!ifs) {
        return;
    }

    ifs.seekg(static_cast<std::streamoff>(ddsCacheNotificationReadOffset_), std::ios::beg);

    std::string line;
    bool hasCompletedConversion = false;
    while (std::getline(ifs, line)) {
        if (line.empty()) {
            continue;
        }

        try {
            // 途中書き込みなどで壊れた行があっても、次の通知の読み取りを続ける。
            nlohmann::json j = nlohmann::json::parse(line);
            if (j.value("status", "") != "built") {
                continue;
            }

            hasCompletedConversion = true;
        }
        catch (...) {
        }
    }

    ddsCacheNotificationReadOffset_ = fileSize;
    if (hasCompletedConversion) {
        saveNotificationTimer_ = 1.5f;
        saveNotificationMsg_ = "DDS変換が完了しました";
    }
#endif
}


// ========================================================================
// セーブ通知の描画処理 (フラッシュ & メッセージ)
// ========================================================================
void DebugEditor::DrawSaveNotification() {
#ifdef USE_IMGUI
    if (saveNotificationTimer_ <= 0.0f) return;

    // タイマー減算 (ImGuiのDeltaTimeを利用してフレームレート非依存に)
    saveNotificationTimer_ -= ImGui::GetIO().DeltaTime;

    // フェードアウトの割合 (1.0 -> 0.0 に向かって減っていく)
    float fadeRatio = saveNotificationTimer_ / 1.5f;
    if (fadeRatio > 1.0f) fadeRatio = 1.0f;

    // 最前面レイヤーを取得
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // 1. 全画面の白いフラッシュ描画
    int whiteAlpha = (int)(fadeRatio * 80.0f);
    drawList->AddRectFilled(ImVec2(0, 0), displaySize, IM_COL32(255, 255, 255, whiteAlpha));

    // 2. 文字の描画 (同じく最前面レイヤーに直接描く)
    float fontSize = ImGui::GetFontSize() * 2.0f; // 文字を2倍サイズに
    const char* text = saveNotificationMsg_.c_str();

    // 文字のピクセルサイズを計算して、画面中央の座標を求める
    ImVec2 baseTextSize = ImGui::CalcTextSize(text);
    ImVec2 textSize = ImVec2(baseTextSize.x * 2.0f, baseTextSize.y * 2.0f);
    ImVec2 textPos = ImVec2((displaySize.x - textSize.x) * 0.5f, (displaySize.y - textSize.y) * 0.5f);

    // 文字のアルファ値 (フェードに合わせて透明になっていく)
    int textAlpha = (int)(fadeRatio * 255.0f);
    ImU32 textColor = IM_COL32(40, 40, 40, textAlpha);         // 濃いグレー
    ImU32 outlineColor = IM_COL32(255, 255, 255, textAlpha);   // 白いフチ取り

    // 少し文字を見やすくするために白い縁取り（シャドウ）を先に描画
    drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(textPos.x + 2, textPos.y + 2), outlineColor, text);
    // メインの文字を描画
    drawList->AddText(ImGui::GetFont(), fontSize, textPos, textColor, text);
#endif
}


// ========================================================================
// 3D座標をGameViewのスクリーン座標に変換
// ========================================================================
engine::Vector3 DebugEditor::WorldToScreen(const engine::Vector3& worldPos) {
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (!cam) return { 0, 0, -1 }; // カメラが無い場合はZを-1(無効)にして返す

    engine::Matrix4x4 viewProj = math_.Multiply(cam->GetViewMatrix(), cam->GetProjectionMatrix());

    // NDC座標系（-1.0 ～ 1.0）への変換
    engine::Vector3 ndc = math_.Transform(worldPos, viewProj);

    // カメラの「後ろ」にある場合はZが0未満、またはW除算の関係で範囲外になる
    if (ndc.z < 0.0f || ndc.z > 1.0f) {
        return { 0, 0, -1 }; // 描画しないフラグとしてZに-1を入れる
    }

    // NDCからGameViewのピクセル座標へ変換
    float screenX = gameViewOffset_.x + (ndc.x + 1.0f) * 0.5f * gameViewSize_.x;
    float screenY = gameViewOffset_.y + (1.0f - ndc.y) * 0.5f * gameViewSize_.y;

    return { screenX, screenY, ndc.z };
}

// ========================================================================
// 3D空間へのアイコンオーバーレイ描画
// ========================================================================
void DebugEditor::Draw3DIcons() {
    int left=0,top=0,right=0,bottom=0;
    GetGameViewScreenRect(left,top,right,bottom);
    SettingsMenuOverlay::SetEditorViewport(left,top,right,bottom);
    if (SettingsMenuOverlay::IsAnyActive()) return;
#ifdef USE_IMGUI
    if (!drawSceneIcons_) return;
#endif
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    // 最前面に描画するためのImGuiレイヤーを取得
    // Game Viewの描画レイヤーに載せ、別ウィンドウの上へアイコンを重ねません。
    ImGuiWindow* viewWindow = ImGui::FindWindowByName("Game View");
    if (!viewWindow || !viewWindow->Active) return;
    ImDrawList* drawList = viewWindow->DrawList;
    drawList->PushClipRect(ImVec2(gameViewOffset_.x, gameViewOffset_.y), ImVec2(gameViewOffset_.x + gameViewSize_.x, gameViewOffset_.y + gameViewSize_.y), true);
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();

    for (const auto& obj : objects) {
        if (!obj->GetIsRenderVisible()) continue;
        std::string className = obj->GetClassName();
        const char* iconStr = nullptr;
        ImU32 iconColor = IM_COL32(255, 255, 255, 200); // デフォルト白（半透明）

        // クラスごとに表示するアイコンを変える！
        if (className == "InvisibleBox") {
            //  名前でトリガーか当たり判定かを区別する！
            if (obj->GetName().find("Trigger") != std::string::npos) {
                iconStr = ICON_FA_BOLT; // 雷マーク (トリガー用)
                iconColor = IM_COL32(255, 255, 0, 200); // 黄色
            }
            else {
                iconStr = ICON_FA_SHIELD_ALT; // 盾マーク (当たり判定用)
                iconColor = IM_COL32(100, 200, 255, 200); // 水色
            }
        }
        else if (className == "Spawner") {
            iconStr = ICON_FA_BOX_OPEN;
            iconColor = IM_COL32(255, 150, 50, 200);  // オレンジ
        }
        else if (obj->IsCameraObject()) {
            iconStr = ICON_FA_VIDEO;
            iconColor = IM_COL32(255, 100, 255, 200); // ピンク
        }

        // アイコンが設定されたオブジェクトのみ処理
        if (iconStr) {
            // オブジェクトのワールド座標を取得
            engine::Vector3 worldPos = { obj->GetWorldMatrix().m[3][0], obj->GetWorldMatrix().m[3][1], obj->GetWorldMatrix().m[3][2] };

            // カメラの場合は、頭上にアイコンを出すためにY軸を少し上げる
            if (obj->IsCameraObject()) {
                worldPos.y += 1.5f;
            }

            // 3D -> 2D 変換
            engine::Vector3 screenPos = WorldToScreen(worldPos);

            // Zが0以上の時だけ（カメラの前にいる時だけ）描画
            if (screenPos.z >= 0.0f) {
                // 選択中のオブジェクトはアイコンを少し大きく、不透明にする
                const bool isSelected = IsObjectSelected(obj.get());
                float fontSize = isSelected ? ImGui::GetFontSize() * 1.5f : ImGui::GetFontSize() * 1.2f;
                if (isSelected) iconColor = IM_COL32(255, 255, 0, 255); // 選択中は黄色

                // 影（アウトライン）を黒で描画して見やすくする
                ImVec2 pos = ImVec2(screenPos.x, screenPos.y);
                drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 255), iconStr);
                // 本体の描画
                drawList->AddText(ImGui::GetFont(), fontSize, pos, iconColor, iconStr);
            }
        }
    }
    drawList->PopClipRect();
#endif
}

// ========================================================================
// イベントIDのオーバーレイ描画
// ========================================================================
void DebugEditor::DrawEventIDOverlay() {
#ifdef USE_IMGUI
    if (!drawEventIDs_ || !sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    // Game Viewの描画レイヤーに載せ、別ウィンドウの上へアイコンを重ねません。
    ImGuiWindow* viewWindow = ImGui::FindWindowByName("Game View");
    if (!viewWindow || !viewWindow->Active) return;
    ImDrawList* drawList = viewWindow->DrawList;
    drawList->PushClipRect(ImVec2(gameViewOffset_.x, gameViewOffset_.y), ImVec2(gameViewOffset_.x + gameViewSize_.x, gameViewOffset_.y + gameViewSize_.y), true);
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();

    // 1. EventIDを持つオブジェクトをマップに集める (線を描画するため)
    std::unordered_map<int, std::vector<engine::Object3d*>> eventMap;
    for (const auto& obj : objects) {
        int eID = obj->GetEventID();
        if (eID != -1) eventMap[eID].push_back(obj.get());
    }

    for (const auto& obj : objects) {
        if (!obj->GetIsRenderVisible()) continue;

        // イベント関連のIDを取得
        int myID = obj->GetEventID();
        int targetID = obj->GetTargetID();

        // --- 2. ID間のリレーション線を描画 ---
        if (targetID != -1 && eventMap.count(targetID)) {
            engine::Vector3 startPos = obj->GetWorldPosition();
            engine::Vector3 startScreen = WorldToScreen(startPos);

            if (startScreen.z >= 0.0f) {
                for (engine::Object3d* targetObj : eventMap[targetID]) {
                    engine::Vector3 endPos = targetObj->GetWorldPosition();
                    engine::Vector3 endScreen = WorldToScreen(endPos);

                    if (endScreen.z >= 0.0f) {
                        // 線を描画 (少し透明な水色)
                        drawList->AddLine(
                            ImVec2(startScreen.x, startScreen.y),
                            ImVec2(endScreen.x, endScreen.y),
                            IM_COL32(0, 255, 255, 150),
                            2.0f
                        );

                        // 終点に小さな丸を描画して方向を示す
                        drawList->AddCircleFilled(ImVec2(endScreen.x, endScreen.y), 4.0f, IM_COL32(0, 255, 255, 200));
                    }
                }
            }
        }

        // --- 3. IDラベルの描画 (既存処理) ---
        std::string label = "";
        if (myID != -1) label += ICON_FA_FINGERPRINT " [ID: " + std::to_string(myID) + "] ";
        if (targetID != -1) label += ICON_FA_BULLSEYE " [Target: " + std::to_string(targetID) + "]";

        if (label.empty()) continue;

        // 3D座標の取得と変換（頭上に表示）
        engine::Vector3 worldPos = obj->GetWorldPosition();
        worldPos.y += obj->GetTransform()->scale.y + 0.5f;

        engine::Vector3 screenPos = WorldToScreen(worldPos);

        if (screenPos.z >= 0.0f) {
            ImVec2 pos = ImVec2(screenPos.x, screenPos.y);
            ImU32 color = (myID != -1) ? IM_COL32(255, 255, 0, 255) : IM_COL32(100, 200, 255, 255);
            if (IsObjectSelected(obj.get())) color = IM_COL32(255, 255, 255, 255); // 選択中は白

            // アウトライン
            drawList->AddText(NULL, 0.0f, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 255), label.c_str());
            // 本体
            drawList->AddText(NULL, 0.0f, pos, color, label.c_str());
        }
    }
    drawList->PopClipRect();
#endif
}

// ==========================================
//  一発・床ピタッ！ (接地機能)
// ==========================================
