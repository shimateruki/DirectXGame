#include "engine/animation/cinematic/CinematicSequence.h"
#include "json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

int main(int argc,char** argv) {
    try {
        Check(argc==2,"temporary output path required");
        engine::CinematicSequence asset;
        Check(asset.Load("Resources/json/scenario/crown_defeat.json"),"migrated asset did not load");
        Check(asset.presentation.objects.size()==49,"missing cinematic objects");
        Check(asset.presentation.particles.size()==3 && !asset.presentation.view.empty(),"missing particles/camera");
        Check(asset.presentation.hideObjects==std::vector<std::string>{"$Actor"},"hide policy changed");
        Check(std::abs(asset.GetAuthoredDuration()-12.0f)<0.001f,"authored duration changed");
        const auto path=std::filesystem::path(argv[1]);
        Check(asset.Save(path.string()),"save failed");
        engine::CinematicSequence copy;
        Check(copy.Load(path.string()),"roundtrip failed");
        Check(copy.presentation.objects.size()==asset.presentation.objects.size(),"roundtrip object loss");
        for (size_t i=0;i<asset.presentation.objects.size();++i) {
            const auto& a=asset.presentation.objects[i];const auto& b=copy.presentation.objects[i];
            Check(a.name==b.name && a.source==b.source && a.model==b.model && a.keys.size()==b.keys.size(),"roundtrip binding/key loss");
            Check(a.spawn==b.spawn && a.positionSpace==b.positionSpace && a.floorHeight==b.floorHeight,"roundtrip motion policy loss");
            Check(a.clipCenterOffset==b.clipCenterOffset,"roundtrip clip origin loss");
            for (size_t k=0;k<a.keys.size();++k) {
                Check(a.keys[k].time==b.keys[k].time && a.keys[k].position.y==b.keys[k].position.y &&
                    a.keys[k].clipHeight==b.keys[k].clipHeight && a.keys[k].visible==b.keys[k].visible &&
                    a.keys[k].color.w==b.keys[k].color.w,"roundtrip sample loss");
            }
        }
        Check(copy.presentation.view.size()==asset.presentation.view.size(),"roundtrip camera loss");
        engine::CinematicSequence finale, finaleCopy;
        Check(finale.Load("Resources/json/scenario/crown_finale.json"),"inheritance asset did not load");
        Check(finale.presentation.objects.size()==69 && finale.presentation.viewWorldSpace,"inheritance presentation missing");
        Check(finale.GetSignalTime("absorb")<finale.GetSignalTime("wings"),"inheritance markers out of order");
        Check(finale.Save(path.string()) && finaleCopy.Load(path.string()),"inheritance roundtrip failed");
        Check(!finaleCopy.presentation.objects[0].animate && finaleCopy.presentation.objects[4].followScale,
              "game-owned animation or live scale policy was lost");
        Check(finaleCopy.presentation.objects.back().inheritVisibility,"fracture visibility policy was lost");
        for (size_t i=0;i<finale.presentation.view.size();++i) {
            const auto& a=finale.presentation.view[i]; const auto& b=finaleCopy.presentation.view[i];
            Check(a.reference==b.reference && a.lookToward==b.lookToward && a.eyeReference==b.eyeReference,
                  "camera target/eye binding was lost");
        }
        Check(asset.Save(path.string()),"defeat fixture restore failed");
        // 古いVFX専用素材と通常の移動トラックも新しいCodecで読めることを確かめる。
        engine::CinematicSequence old;
        Check(old.Load("Resources/json/scenario/crown_defeat_vfx.json"),"v6 compatibility failed");
        Check(old.vfxTracks.size()==2 && old.presentation.objects.empty(),"legacy VFX changed");
        // 不正なサンプルは途中まで生成せず、読込全体を失敗させる。
        nlohmann::json invalid;
        { std::ifstream file(path);file>>invalid; }
        invalid["presentation"]["objects"][0]["keys"][0]=nlohmann::json::array({0,1});
        { std::ofstream file(path);file<<invalid; }
        Check(!copy.Load(path.string()) && copy.presentation.objects.empty(),"invalid row accepted/partially retained");
        // 旧項目名・省略値は、共通の型変換へ移しても同じ意味で読む。
        const auto legacy=nlohmann::json::parse(R"({
            "version":2,
            "tracks":[{"targetName":"Actor","delayTime":1.25,"keys":[{"time":0,"scale":[1]}]}],
            "vfxTracks":[{"sequenceName":"LegacyVfx","delayTime":2}],
            "cameraShots":[{"targetName":"LegacyCamera"}],
            "animationClips":[{}]
        })");
        { std::ofstream file(path);file<<legacy; }
        Check(copy.Load(path.string()),"legacy defaults failed");
        Check(copy.transformTracks[0].startTime==1.25f && copy.transformTracks[0].name=="Actor" &&
            copy.transformTracks[0].keys[0].scale.x==1,"legacy transform/default scale changed");
        Check(copy.vfxTracks[0].startTime==2 && copy.cameraShots[0].name=="LegacyCamera" &&
            copy.animationClips[0].driver=="Model","legacy aliases/defaults changed");

        // 既存のEasingと、焼き込んだ長い回転を同じ補間基盤で維持する。
        engine::CinematicTransformKey from,to;
        from.easingToNext=0;to.time=2;to.position={10,0,0};
        const std::vector<engine::CinematicTransformKey> keys{from,to};
        Check(engine::SampleCinematicTransform(keys,0.5f).position.x==2.5f,"linear transform changed");
        from.easingToNext=4;
        Check(std::abs(engine::InterpolateCinematicTransform(from,to,0.5f).position.x-1.03515625f)<0.00001f,"legacy easing changed");
        engine::CinematicVisualKey visualFrom,visualTo;
        visualTo.time=1;visualTo.rotation.y=25.1327412f;
        Check(std::abs(engine::InterpolateCinematicTransform(visualFrom,visualTo,0.5f,true).rotation.y-12.5663706f)<0.00001f,"baked turns were shortened");
        std::cout<<"Cinematic asset roundtrip, v6 compatibility and malformed-input checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
