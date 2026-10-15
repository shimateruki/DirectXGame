#include "engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.h"
#include "Object3d.h"
#include "MeshRenderer.h"
#include <algorithm>
#ifdef USE_IMGUI
#include "imgui.h"
#include "IconsFontAwesome5.h"
#endif

// UIの順序・ID・設定値は従来と同じ。既存の変更フラグを参照で受け、先行する編集結果も保持する。
void InspectorWindow::DrawEffectMaterialSettings(engine::Object3d* selectedObject, int currentMatType, bool& isGraphicsChanged) {
#ifdef USE_IMGUI
    // 特殊材質の各 Shader は WaterParamData を共用し、同じフィールドを材質ごとに別の意味で使います。
    // 編集フラグは呼び出し元から受けた値へ加算し、このパネルより前の変更を消しません。
    if ((currentMatType >= 8 && currentMatType <= 22) || currentMatType == 26) {
        ImGui::Separator();

        if (selectedObject->GetMeshRenderer() && selectedObject->GetMeshRenderer()->GetWaterParamData()) {
            auto* waterData = selectedObject->GetMeshRenderer()->GetWaterParamData();

            // effectType は材質内の見た目の種類を表し、該当するプルダウンだけを表示します。
            if (currentMatType == 11) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.08f, 1.0f), ICON_FA_FIRE " --- Fire Settings ---");
                const char* fireTypes[] = {
                    "炎の形 (Flame Shape)",
                    "炎の球 (Fire Ball)",
                    "まとい炎 (Wrapped Flame)",
                    "炎の流れ (Flame Stream)",
                    "立体かがり火 (Volumetric Brazier)"
                };
                int fireType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 4);
                if (ImGui::Combo("炎タイプ (Fire Type)", &fireType, fireTypes, IM_ARRAYSIZE(fireTypes))) {
                    waterData->effectType = static_cast<float>(fireType);
                    isGraphicsChanged = true;
                }

                // 立体炎だけ奥行き・乱流・傾きの調整が必要です。
                if (ImGui::DragFloat("揺らぎ速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("炎の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 20.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("模様スケール (Pattern Scale)", &waterData->effectScale, 0.01f, 0.05f, 5.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("描画サイズ (Render Size)", &waterData->billboardScale, 0.01f, 0.05f, 3.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("炎の横幅 (Width)", &waterData->effectScaleX, 0.01f, 0.12f, 4.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("炎の高さ (Height)", &waterData->effectScaleY, 0.01f, 0.12f, 4.0f)) isGraphicsChanged = true;
                if (fireType == 2 || fireType == 4) {
                    if (ImGui::DragFloat("炎の奥行き (Depth)", &waterData->effectScaleZ, 0.01f, 0.12f, 4.0f)) isGraphicsChanged = true;
                    if (ImGui::SliderFloat("立体の揺らぎ (Turbulence)", &waterData->waveHeight, 0.0f, 1.5f)) isGraphicsChanged = true;
                    if (ImGui::SliderFloat("風による傾き (Wind)", &waterData->flowSpeedX, -1.0f, 1.0f)) isGraphicsChanged = true;
                    ImGui::TextDisabled("閉じた立体の内部に炎を描画。柔らかさで縁の薄さを調整できます。");
                }
                if (ImGui::SliderFloat("輪郭の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("炎の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 5.0f)) isGraphicsChanged = true;
            }
            else if (currentMatType == 12) {
                ImGui::TextColored(ImVec4(0.25f, 0.85f, 1.0f, 1.0f), ICON_FA_BOLT " --- Laser Settings ---");
                if (ImGui::DragFloat("流れる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 20.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("縞の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("模様スケール (Pattern Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                if (ImGui::SliderFloat("外側の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("発光の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 8.0f)) isGraphicsChanged = true;
            }
            else if (currentMatType == 13) {
                ImGui::TextColored(ImVec4(0.15f, 1.0f, 0.8f, 1.0f), ICON_FA_WATER " --- Slime Gel Settings ---");
                if (ImGui::DragFloat("ぷるぷる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("変形量 (Wobble Height)", &waterData->waveHeight, 0.01f, 0.0f, 3.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("内部模様の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 20.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("屈折の強さ (Refraction)", &waterData->effectScale, 0.01f, 0.05f, 4.0f)) isGraphicsChanged = true;
                if (ImGui::SliderFloat("表面の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("透明感の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 5.0f)) isGraphicsChanged = true;
            }
            else if (currentMatType == 14) {
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), ICON_FA_BULLSEYE " --- Ground Shockwave Settings ---");
                if (ImGui::DragFloat("広がる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("ひびの細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 24.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("衝撃波の半径 (Radius Scale)", &waterData->effectScale, 0.01f, 0.05f, 10.0f)) isGraphicsChanged = true;
                if (ImGui::SliderFloat("リング幅 (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("明るさ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 6.0f)) isGraphicsChanged = true;
            }
            else if (currentMatType == 15) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.18f, 1.0f), ICON_FA_BURN " --- Liquid Contact Settings ---");
                const char* contactTypes[] = { "水の泡 (Foam)", "マグマ蒸気 (Steam)" };
                int contactType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 1);
                if (ImGui::Combo("接触タイプ (Contact Type)", &contactType, contactTypes, IM_ARRAYSIZE(contactTypes))) {
                    waterData->effectType = static_cast<float>(contactType);
                    isGraphicsChanged = true;
                }
                if (ImGui::DragFloat("流れる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("泡/蒸気の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 24.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("帯の幅 (Band Width)", &waterData->effectScale, 0.01f, 0.05f, 10.0f)) isGraphicsChanged = true;
                if (ImGui::SliderFloat("境界の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("明るさ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 6.0f)) isGraphicsChanged = true;

                // 接触帯の移動方向を別の Flow パラメータとして調整します。
                ImGui::Separator();
                ImGui::Text(ICON_FA_WIND " --- Flow Settings ---");
                if (ImGui::DragFloat("Flow Speed X", &waterData->flowSpeedX, 0.01f, -50.0f, 50.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("Flow Speed Y", &waterData->flowSpeedY, 0.01f, -50.0f, 50.0f)) isGraphicsChanged = true;
            }
            else if (currentMatType == 16) {
                ImGui::TextColored(ImVec4(1.0f, 0.32f, 0.18f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " --- Damage Crack Settings ---");
                const char* crackTypes[] = { "ブロック亀裂 (Block Crack)", "ガラス亀裂 (Glass Crack)", "弱点コア亀裂 (Core Crack)" };
                int crackType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                if (ImGui::Combo("亀裂タイプ (Crack Type)", &crackType, crackTypes, IM_ARRAYSIZE(crackTypes))) {
                    waterData->effectType = static_cast<float>(crackType);
                    isGraphicsChanged = true;
                }
                if (ImGui::DragFloat("脈動速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 12.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("亀裂の深さ (Depth)", &waterData->waveHeight, 0.01f, 0.0f, 3.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("亀裂の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("亀裂スケール (Crack Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                if (ImGui::SliderFloat("縁の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("発光の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 8.0f)) isGraphicsChanged = true;
            }
            else if (currentMatType == 17) {
                ImGui::TextColored(ImVec4(0.45f, 0.95f, 1.0f, 1.0f), ICON_FA_WIND " --- Updraft Settings ---");
                const char* updraftTypes[] = { "上昇柱 (Column)", "渦リング (Vortex Ring)", "横風スラッシュ (Wind Slash)" };
                int updraftType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                if (ImGui::Combo("風タイプ (Wind Type)", &updraftType, updraftTypes, IM_ARRAYSIZE(updraftTypes))) {
                    waterData->effectType = static_cast<float>(updraftType);
                    isGraphicsChanged = true;
                }
                if (ImGui::DragFloat("流れる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 16.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("揺らぎの高さ (Wobble)", &waterData->waveHeight, 0.01f, 0.0f, 4.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("筋の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("渦の広さ (Vortex Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                if (ImGui::SliderFloat("境界の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("透明感/強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 8.0f)) isGraphicsChanged = true;
            }
            else if (currentMatType == 18) {
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.25f, 1.0f), ICON_FA_BOLT " --- Stun Bind Settings ---");
                const char* stunTypes[] = { "拘束リング (Bind Rings)", "電撃ケージ (Electric Cage)", "スタン球 (Stun Orb)" };
                int stunType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                if (ImGui::Combo("拘束タイプ (Bind Type)", &stunType, stunTypes, IM_ARRAYSIZE(stunTypes))) {
                    waterData->effectType = static_cast<float>(stunType);
                    isGraphicsChanged = true;
                }
                if (ImGui::DragFloat("電撃速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 20.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("リング間隔 (Ring Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 32.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("拘束の太さ (Bind Width)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                if (ImGui::SliderFloat("外側の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("発光の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
            }
            else if (currentMatType == 19) {
                ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.25f, 1.0f), ICON_FA_MAGIC " --- Crown Unlock Settings ---");
                const char* crownTypes[] = { "魔法陣 (Magic Circle)", "王冠バースト (Crown Burst)", "解放ポータル (Unlock Portal)" };
                int crownType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                if (ImGui::Combo("解放タイプ (Unlock Type)", &crownType, crownTypes, IM_ARRAYSIZE(crownTypes))) {
                    waterData->effectType = static_cast<float>(crownType);
                    isGraphicsChanged = true;
                }
                if (ImGui::DragFloat("演出速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 12.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("光線の数/細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 32.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("紋章スケール (Glyph Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                if (ImGui::SliderFloat("縁の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("神々しさ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 10.0f)) isGraphicsChanged = true;
            }
            else if (currentMatType == 20) {
                ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.25f, 1.0f), ICON_FA_SMOG " --- Poison Spore Settings ---");
                const char* sporeTypes[] = { "毒霧 (Poison Mist)", "胞子雲 (Spore Cloud)", "毒リング (Poison Ring)" };
                int sporeType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                if (ImGui::Combo("毒タイプ (Poison Type)", &sporeType, sporeTypes, IM_ARRAYSIZE(sporeTypes))) {
                    waterData->effectType = static_cast<float>(sporeType);
                    isGraphicsChanged = true;
                }
                if (ImGui::DragFloat("漂う速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 12.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("霧の厚み (Density)", &waterData->waveHeight, 0.01f, 0.0f, 4.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("胞子の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("模様スケール (Pattern Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                if (ImGui::SliderFloat("境界の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("毒の強さ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 8.0f)) isGraphicsChanged = true;
            }
            else if (currentMatType == 21) {
                ImGui::TextColored(ImVec4(0.75f, 0.9f, 1.0f, 1.0f), ICON_FA_CLOUD " --- Cloud Settings ---");
                const char* cloudTypes[] = { "雲の塊 (Puff)", "流れる雲 (Drift)", "足元の煙 (Ground Mist)" };
                int cloudType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                if (ImGui::Combo("雲タイプ (Cloud Type)", &cloudType, cloudTypes, IM_ARRAYSIZE(cloudTypes))) {
                    waterData->effectType = static_cast<float>(cloudType);
                    isGraphicsChanged = true;
                }
                if (ImGui::DragFloat("流れる速度 (Speed)", &waterData->waveSpeed, 0.05f, 0.05f, 12.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("厚み (Density)", &waterData->waveHeight, 0.01f, 0.0f, 4.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("雲の細かさ (Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("雲スケール (Cloud Scale)", &waterData->effectScale, 0.01f, 0.05f, 8.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("描画サイズ (Billboard Size)", &waterData->billboardScale, 0.01f, 0.05f, 4.0f)) isGraphicsChanged = true;
                if (ImGui::SliderFloat("輪郭の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("明るさ (Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 5.0f)) isGraphicsChanged = true;
            }
            else if (currentMatType == 22) {
                // ゲートは幅・高さ・厚みを別々に調整するため、より細かい Drag 刻みを使います。
                ImGui::TextColored(ImVec4(1.0f, 0.58f, 0.18f, 1.0f), ICON_FA_MAGIC " --- Gate Portal Settings ---");
                const char* gateTypes[] = { "渦ポータル (Swirl Portal)", "暖色ゲート (Warm Gate)", "封印ゲート (Sealed Gate)" };
                int gateType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                if (ImGui::Combo("ゲートタイプ (Gate Type)", &gateType, gateTypes, IM_ARRAYSIZE(gateTypes))) {
                    waterData->effectType = static_cast<float>(gateType);
                    isGraphicsChanged = true;
                }
                if (ImGui::DragFloat("渦の速度 (Speed)", &waterData->waveSpeed, 0.01f, 0.05f, 12.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::DragFloat("奥行きの強さ (Depth)", &waterData->waveHeight, 0.005f, 0.05f, 5.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::DragFloat("渦の細かさ (Detail)", &waterData->waveFrequency, 0.01f, 0.1f, 36.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::DragFloat("模様スケール (Pattern Scale)", &waterData->effectScale, 0.001f, 0.01f, 8.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::DragFloat("全体サイズ (Portal Size)", &waterData->billboardScale, 0.001f, 0.01f, 8.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::DragFloat("横スケール (Scale X)", &waterData->effectScaleX, 0.001f, 0.05f, 6.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::DragFloat("縦スケール (Scale Y)", &waterData->effectScaleY, 0.001f, 0.05f, 6.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::DragFloat("厚みスケール (Scale Z)", &waterData->effectScaleZ, 0.001f, 0.0f, 3.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::SliderFloat("輪郭の柔らかさ (Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("発光の強さ (Intensity)", &waterData->effectIntensity, 0.01f, 0.05f, 8.0f, "%.3f")) isGraphicsChanged = true;
            }
            else if (currentMatType == 26) {
                ImGui::TextColored(ImVec4(0.42f, 1.0f, 0.82f, 1.0f), ICON_FA_WIND " --- Wind Orb Settings ---");
                const char* windOrbTypes[] = { "安定した風弾 (Stable)", "高速の渦 (Fast)", "圧縮した風 (Compressed)" };
                int windOrbType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 2);
                if (ImGui::Combo("風弾タイプ (Wind Orb Type)", &windOrbType, windOrbTypes, IM_ARRAYSIZE(windOrbTypes))) {
                    waterData->effectType = static_cast<float>(windOrbType);
                    isGraphicsChanged = true;
                }
                if (ImGui::DragFloat("帯の流速 (Band Speed)", &waterData->waveSpeed, 0.01f, 0.05f, 12.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::DragFloat("表面の揺れ (Surface Motion)", &waterData->waveHeight, 0.005f, 0.0f, 3.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::DragFloat("内部の細かさ (Inner Detail)", &waterData->waveFrequency, 0.05f, 0.1f, 30.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::DragFloat("屈折の強さ (Refraction)", &waterData->effectScale, 0.005f, 0.05f, 3.0f, "%.3f")) isGraphicsChanged = true;
                if (ImGui::SliderFloat("帯の柔らかさ (Band Softness)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("発光の強さ (Intensity)", &waterData->effectIntensity, 0.01f, 0.05f, 5.0f, "%.3f")) isGraphicsChanged = true;
            }
            else if (currentMatType == 9) {
                ImGui::TextColored(ImVec4(1.0f, 0.28f, 0.04f, 1.0f), ICON_FA_FIRE " --- Magma Settings ---");
                if (ImGui::DragFloat("流れる速度 (Flow Speed)", &waterData->waveSpeed, 0.01f, 0.05f, 6.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("隆起の高さ (Heave Height)", &waterData->waveHeight, 0.01f, 0.0f, 4.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("塊の細かさ (Blob Detail)", &waterData->waveFrequency, 0.05f, 0.15f, 12.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("粘度/引き伸ばし (Viscosity)", &waterData->effectScale, 0.01f, 0.1f, 5.0f)) isGraphicsChanged = true;
                if (ImGui::SliderFloat("黒いクラストの幅 (Crust Width)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("熱の強さ (Heat Intensity)", &waterData->effectIntensity, 0.05f, 0.05f, 5.0f)) isGraphicsChanged = true;
                ImGui::Separator();
                ImGui::Text(ICON_FA_WIND " --- Slow Flow Direction ---");
                if (ImGui::DragFloat("Flow Speed X", &waterData->flowSpeedX, 0.01f, -5.0f, 5.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("Flow Speed Y", &waterData->flowSpeedY, 0.01f, -5.0f, 5.0f)) isGraphicsChanged = true;
            }
            else {
                // 残る材質は水と氷。水だけはスタイルと元モデル使用の組み合わせを選べます。
                const char* settingTitle = (currentMatType == 8) ? ICON_FA_TINT " --- Water Settings ---" :
                    ICON_FA_SNOWFLAKE " --- Ice Settings ---";
                ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), settingTitle);
                if (currentMatType == 8) {
                    // effectType は「スタイル * 2 + メッシュ種別」の符号化値です。
                    // 既存の0/1を維持したまま、表現と形状を独立して選択します。
                    int waterStyle = waterData->effectType >= 1.5f ? 1 : 0;
                    int waterMeshType = std::clamp(static_cast<int>(waterData->effectType + 0.5f), 0, 3) % 2;
                    const char* waterStyles[] = { "従来の海", "明るく穏やかな海" };
                    if (ImGui::Combo("水の表現", &waterStyle, waterStyles, IM_ARRAYSIZE(waterStyles))) {
                        waterData->effectType = static_cast<float>(waterStyle * 2 + waterMeshType);
                        isGraphicsChanged = true;
                    }
                    const char* waterMeshTypes[] = {
                        "専用グリッド (Recommended)",
                        "元モデルを使用 (Legacy)"
                    };
                    if (ImGui::Combo("水面形状 (Surface Mesh)", &waterMeshType, waterMeshTypes, IM_ARRAYSIZE(waterMeshTypes))) {
                        waterData->effectType = static_cast<float>(waterStyle * 2 + waterMeshType);
                        isGraphicsChanged = true;
                    }

                    if (waterStyle == 1) {
                        // スタイル切替自体では調整値を変えず、推奨値は明示ボタンで適用します。
                        ImGui::TextWrapped("従来の屈折・反射・岸の泡を残し、上下の波を抑えた明るい海です。切替だけでは調整値を変更しません。");
                        if (ImGui::Button("明るく穏やかな海・推奨値を適用")) {
                            waterData->waveSpeed = 0.85f;
                            waterData->waveHeight = 0.0f;
                            waterData->waveFrequency = 5.40f;
                            waterData->flowSpeedX = 0.018f;
                            waterData->flowSpeedY = 0.010f;
                            waterData->effectScale = 0.55f;
                            waterData->effectSoftness = 0.36f;
                            waterData->effectIntensity = 0.98f;
                            waterData->billboardScale = 0.65f;
                            waterData->effectScaleX = 12.0f;
                            waterData->effectScaleY = 1.05f;
                            waterData->effectScaleZ = 0.85f;
                            waterData->topDownDetail = 0.75f;
                            isGraphicsChanged = true;
                        }
                        ImGui::TextWrapped("色味はマテリアル色で調整できます。推奨値の適用は共通の水パラメーターを変更します。");
                        ImGui::TextWrapped("この海の Flow Speed は揺らぎの位相速度です。模様全体は流さず、複数方向に往復して反射が変化します。");
                    } else {
                        // 従来の海には用途別の二つの値セットを用意し、選んだときだけ共通パラメータを書き換えます。
                        if (ImGui::Button(ICON_FA_MAGIC " 南国の透明な海プリセット")) {
                            waterData->effectType = 0.0f;
                            waterData->waveSpeed = 0.85f;
                            waterData->waveHeight = 1.50f;
                            waterData->waveFrequency = 5.40f;
                            waterData->flowSpeedX = 0.018f;
                            waterData->flowSpeedY = 0.010f;
                            waterData->effectScale = 0.95f;
                            waterData->effectSoftness = 0.48f;
                            waterData->effectIntensity = 0.98f;
                            waterData->billboardScale = 1.00f;
                            waterData->effectScaleX = 12.0f;
                            waterData->effectScaleY = 0.85f;
                            waterData->effectScaleZ = 1.15f;
                            waterData->topDownDetail = 1.15f;
                            isGraphicsChanged = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button(ICON_FA_WATER " 穏やかな水プリセット")) {
                            waterData->effectType = 0.0f;
                            waterData->waveSpeed = 0.70f;
                            waterData->waveHeight = 0.32f;
                            waterData->waveFrequency = 3.60f;
                            waterData->flowSpeedX = 0.020f;
                            waterData->flowSpeedY = 0.012f;
                            waterData->effectScale = 0.55f;
                            waterData->effectSoftness = 0.36f;
                            waterData->effectIntensity = 0.95f;
                            waterData->billboardScale = 0.72f;
                            waterData->effectScaleX = 10.0f;
                            waterData->effectScaleY = 0.78f;
                            waterData->effectScaleZ = 0.95f;
                            waterData->topDownDetail = 1.00f;
                            isGraphicsChanged = true;
                        }
                        ImGui::TextDisabled("専用グリッドは元モデルに関係なく128分割の水面として描画します");
                        ImGui::TextDisabled("透明な浅瀬には、水面の下に水底のメッシュが必要です");
                        ImGui::TextDisabled("Wave Height はワールド単位のうねり。細波は Ripple Strength で別調整");
                    }
                    // 表現を切り替えても同じ時計を確認できるように、共通部分に表示します。
                    ImGui::Text("水面時間: %.3f 秒", waterData->time);
                    if (ImGui::TreeNode("水面アニメーションの確認")) {
                        // 診断表示と時間送りは保存用の材質変更フラグを立てません。
                        int debugView = static_cast<int>(waterData->waterDebugView);
                        const char* debugViews[] = { "通常の水面", "GPU時間（色で確認）", "水面の動き（法線）" };
                        if (ImGui::Combo("描画診断（保存対象外）", &debugView, debugViews, IM_ARRAYSIZE(debugViews))) {
                            waterData->waterDebugView = static_cast<float>(debugView);
                        }
                        ImGui::TextWrapped("GPU時間は水面の色が変化します。時間送りでも色が変わらない場合、設定値ではなく描画経路・読み込んだシェーダーを確認します。");
                        ImGui::Text("模様の時刻: %.3f", waterData->time * (std::max)(waterData->waveSpeed, 0.0f));
                        ImGui::Text("流れの積算: X %.4f / Y %.4f", waterData->uvOffsetX, waterData->uvOffsetY);
                        ImGui::TextWrapped("一時停止してから進めると、水面だけの変化を比較できます。時間の操作は保存しません。");
                        // 本体と同じ更新処理で時計と流れを一緒に進め、設定値は変更しません。
                        if (ImGui::Button("水面を1秒進める")) {
                            selectedObject->GetMeshRenderer()->AdvanceWaterAnimation(1.0f);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("水面を5秒進める")) {
                            selectedObject->GetMeshRenderer()->AdvanceWaterAnimation(5.0f);
                        }
                        ImGui::TreePop();
                    }
                }
                if (ImGui::DragFloat("Wave Speed (波の速さ)", &waterData->waveSpeed, 0.05f, 0.0f, 10.0f)) isGraphicsChanged = true;
            // 穏やかな海では上下動を使わないため、波高の入力を無効化します。
                ImGui::BeginDisabled(currentMatType == 8 && waterData->effectType >= 1.5f);
                if (ImGui::DragFloat("Wave Height (波の高さ)", &waterData->waveHeight, 0.05f, 0.0f, 10.0f)) isGraphicsChanged = true;
                ImGui::EndDisabled();
                if (ImGui::DragFloat("Wave Frequency (波の細かさ)", &waterData->waveFrequency, 0.05f, 0.0f, 20.0f)) isGraphicsChanged = true;
                ImGui::Separator();
                ImGui::Text(ICON_FA_WIND " --- Flow Settings ---");
                if (ImGui::DragFloat("Flow Speed X", &waterData->flowSpeedX, 0.01f, -50.0f, 50.0f)) isGraphicsChanged = true;
                if (ImGui::DragFloat("Flow Speed Y", &waterData->flowSpeedY, 0.01f, -50.0f, 50.0f)) isGraphicsChanged = true;
                if (currentMatType == 8) {
                    ImGui::Separator();
                    ImGui::Text(ICON_FA_TINT " --- Surface Settings ---");
                    if (waterData->effectType >= 1.5f) {
                        ImGui::TextWrapped("水面の上下動はありません。細波の強さは細かな揺らぎだけを調整し、0でも大きな流れと反射は残ります。");
                    }
                    if (ImGui::DragFloat("屈折の強さ (Refraction)", &waterData->effectScale, 0.01f, 0.0f, 3.0f)) isGraphicsChanged = true;
                    if (ImGui::SliderFloat("泡の広がり (Foam Width)", &waterData->effectSoftness, 0.0f, 1.0f)) isGraphicsChanged = true;
                    if (ImGui::DragFloat("水面の明るさ (Brightness)", &waterData->effectIntensity, 0.05f, 0.05f, 4.0f)) isGraphicsChanged = true;
                    if (ImGui::DragFloat("水中の色むら (Subsurface Variation)", &waterData->billboardScale, 0.01f, 0.0f, 2.0f)) isGraphicsChanged = true;
                    if (ImGui::DragFloat("深さ色の範囲 (Depth Range)", &waterData->effectScaleX, 0.10f, 1.0f, 30.0f)) isGraphicsChanged = true;
                    if (ImGui::DragFloat("反射の明るさ (Reflection Brightness)", &waterData->effectScaleY, 0.01f, 0.0f, 3.0f)) isGraphicsChanged = true;
                    if (ImGui::DragFloat("接触泡の強さ (Foam Intensity)", &waterData->effectScaleZ, 0.01f, 0.0f, 3.0f)) isGraphicsChanged = true;
                    if (ImGui::DragFloat("細波の強さ (Ripple Strength)", &waterData->topDownDetail, 0.01f, 0.0f, 2.5f)) isGraphicsChanged = true;
                }
            }
        }
    }
#else
    (void)selectedObject;
    (void)currentMatType;
    (void)isGraphicsChanged;
#endif
}
