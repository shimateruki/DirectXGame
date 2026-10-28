#pragma once
#ifdef USE_IMGUI
#include "DebugEditor.h"
#include "BaseEnemy.h"
#include <cmath>
#include "EditorManager.h"
#include "game/editor/EnemyAttackPreviewRegistry.h"
#include "game/editor/RegisterGameActorDebug.h"
#include "SceneManager.h"
#include "WinApp.h"
#include <fstream>
#include <stdexcept>

// 専用起動時だけ、既存の全攻撃を実際の隔離プレビューへ順番に接続します。
struct AttackPreviewRegistryRuntimeProbe {
    struct Case { std::string type; int attack; std::string option; };
    static bool Enabled() { return GetEnvironmentVariableA("CG2_ATTACK_PREVIEW_REGISTRY_TEST", nullptr, 0) > 0; }
    static void Require(bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    }
    static void AfterDraw(SceneManager* manager, DebugEditor* editor, const std::string& path) {
        static std::ofstream log(path, std::ios::trunc);
        static int phase=0, wait=0;
        static size_t index=0;
        static std::vector<Case> cases;
        static bool done=false;
        if (done || !manager || !editor || manager->IsTransitioning()) return;
        auto finish=[&](const std::string& text) { log<<text<<std::endl; done=true; WinApp::CloseNow(); };
        try {
            if (phase==0) { manager->SetIsPlaying(false); manager->ChangeScene("GAMEPLAY"); phase=1; return; }
            if (manager->GetCurrentSceneName()!="GAMEPLAY" || ++wait<3) return;
            auto& preview=*GetGameActorDebugPanel();
            if (phase==1) {
                const std::vector<std::string> expected={"Slime","FireSlime","ThunderSlime","GiantSlime",
                    "PrismSlime","MagmaSlime","Bomber","WindSlime","FalseKingSlime"};
                const auto& entries=EnemyAttackPreviewRegistry::GetEntries();
                Require(entries.size()==expected.size(), "legacy enemy count");
                BaseEnemy mismatched;
                for(size_t i=0;i<entries.size();++i) {
                    const auto& entry=entries[i];
                    Require(entry.type==expected[i], "legacy type ID/order");
                    Require(!entry.selectAttack(mismatched,"invalid"), "wrong actor type accepted");
                    EnemyAttackProfile profile;
                    profile.LoadForEnemy(entry.type);
                    // 元のAssetは変更せず、専用出力で既存保存形式と攻撃IDの往復を確認します。
                    const auto roundTripPath=path+"."+entry.type+".json";
                    Require(profile.SaveToFile(roundTripPath),"profile round-trip save");
                    EnemyAttackProfile loaded;
                    Require(loaded.LoadFromFile(roundTripPath),"profile round-trip load");
                    Require(loaded.enemyType==profile.enemyType && loaded.attacks.size()==profile.attacks.size(),
                        "profile round-trip identity/count");
                    for(const auto& attack:profile.attacks)
                        Require(loaded.FindAttack(attack.id)!=nullptr,"profile round-trip attack ID");
                    bool added=false;
                    for(int a=0;a<static_cast<int>(profile.attacks.size());++a) {
                        const auto& attack=profile.attacks[a];
                        if(attack.sequence.enabled && attack.sequence.binding=="player_special") continue;
                        Require(profile.FindAttack(attack.id)!=nullptr,"attack ID lookup");
                        cases.push_back({entry.type,a,{}});
                        added=true;
                    }
                    if(!added) cases.push_back({entry.type,0,{}});
                    for(const auto& option:entry.options) cases.push_back({entry.type,0,option.id});
                }
                // 重複・不正な一括登録は、既存一覧へ一部だけ追加しません。
                auto duplicate=entries.front();
                auto extra=duplicate; extra.type="__test_only";
                Require(!EnemyAttackPreviewRegistry::Register({extra,duplicate}) &&
                    EnemyAttackPreviewRegistry::GetEntries().size()==expected.size(),"non-atomic duplicate rejection");
                extra.options={{"duplicate","A",1.0f,[]{return 1.0f;}},{"duplicate","B",1.0f,[]{return 1.0f;}}};
                Require(!EnemyAttackPreviewRegistry::Register({extra}),"duplicate option accepted");
                Require(!EnemyAttackPreviewRegistry::Find("unknown") &&
                    !EnemyAttackPreviewRegistry::FindOption("Slime","unknown"),"unknown ID accepted");
                log<<"OK 9 legacy types, attack IDs, wrong-type guards, atomic rejection; cases="<<cases.size()<<std::endl;
                preview.loop_=false;
                EditorManager::GetInstance()->SetSelectedObject(&preview);
                phase=2;
            }
            if(phase==2) {
                if(index==cases.size()) {
                    preview.StopPreview();
                    // 登録追加後も、既に選んだ敵は番号ではなくIDで保持します。
                    auto extra=*EnemyAttackPreviewRegistry::Find("Slime");
                    extra.type="__test_only"; extra.displayName="追加登録テスト";
                    preview.enemyType_="FireSlime";
                    Require(EnemyAttackPreviewRegistry::Register({extra}),"additional registration failed");
                    Require(std::string(preview.GetSelectedEnemyType())=="FireSlime","selection changed after append");
                    finish("PASS: registered enemy/attack selection, real preview generation, fixed-step seek and cleanup");
                    return;
                }
                const auto& test=cases[index];
                preview.enemyType_=test.type;
                preview.LoadSelectedProfile();
                Require(preview.previewOptionId_.empty(),"previous special selection leaked");
                preview.profileAttackIndex_=test.attack;
                preview.previewOptionId_=test.option;
                preview.ApplyRecommendedSettings();
                if(const auto* option=preview.GetPreviewOption()) {
                    Require(preview.targetDistance_==option->targetDistance && preview.loopDuration_==option->duration(),
                        "special settings mismatch");
                    if(option->suppressProfile) Require(!preview.GetSelectedAttackDefinition(),"special profile suppression");
                } else if(const auto* attack=preview.GetSelectedAttackDefinition()) {
                    Require(preview.targetDistance_==attack->recommendedTargetDistance &&
                        preview.loopDuration_==attack->previewDuration,"profile settings mismatch");
                }
                preview.enabled_=true;
                preview.CreatePreviewObjects();
                Require(preview.previewEnemy_ && preview.previewTarget_,"preview actor generation");
                const auto* descriptor=preview.GetPreviewDescriptor();
                Require(descriptor && descriptor->selectAttack(*preview.previewEnemy_,test.option.empty()
                    ? (preview.GetSelectedAttackDefinition()?preview.GetSelectedAttackDefinition()->id:std::string{})
                    :test.option),"registered attack rejected real actor");
                Require(preview.previewEnemy_->GetEnemyType()==test.type,"different enemy generated");
                preview.playing_=false;
                wait=0; phase=3;
                return;
            }
            if(phase==3) {
                preview.SeekPreview(0.5f);
                Require(preview.GetCurrentPhaseName()!=nullptr,"phase label missing");
                Require(std::abs(preview.elapsedTime_-0.5f)<0.001f,"forward seek mismatch");
                preview.SeekPreview(0.25f);
                Require(std::abs(preview.elapsedTime_-0.25f)<0.001f,"backward seek mismatch");
                const auto& test=cases[index];
                log<<"OK "<<test.type<<" attack="<<test.attack<<" option="<<test.option<<
                    " phase="<<preview.GetCurrentPhaseName()<<std::endl;
                preview.StopPreview();
                Require(!preview.previewEnemy_ && !preview.previewTarget_ && !preview.enabled_,"cleanup retained actor");
                ++index; wait=0; phase=2;
            }
        } catch(const std::exception& error) {
            finish(std::string("FAIL: ")+error.what());
        }
    }
};
#endif
