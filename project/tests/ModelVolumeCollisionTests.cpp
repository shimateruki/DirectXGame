#include "engine/component/Collider.h"
#include "engine/component/ConvexCollision.h"
#include "json.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <memory>
#include <functional>

namespace {
void Require(bool condition,const char* message) {
    // 失敗箇所をプロセスの終了コードへ反映する。
    if(!condition)
        throw std::runtime_error(message);
}

std::vector<ModelSurfaceTriangle> ReadObj(const std::string& model) {
    // 実際にStage2で使用するOBJを三角形へ展開し、描画初期化なしで判定本体を検証する。
    const auto asset=std::filesystem::path(model);
    const auto file=asset.extension()==".gltf"
        ? std::filesystem::path("Resources/3DModel")/asset.parent_path()/(asset.parent_path().filename().string()+".obj")
        : std::filesystem::path("Resources/3DModel")/asset/(asset.filename().string()+".obj");
    std::ifstream stream(file);
    Require(bool(stream),"OBJ missing");
    std::vector<engine::Vector3> vertices;
    std::vector<ModelSurfaceTriangle> triangles;
    std::string line;

    while(std::getline(stream,line)){
        std::istringstream tokens(line);
        std::string kind;
        tokens>>kind;

        if(kind=="v"){engine::Vector3 v{};tokens>>v.x>>v.y>>v.z;vertices.push_back(v);}
        if(kind=="f"){
            std::vector<int> ids;
            std::string value;
            while(tokens>>value)
                ids.push_back(std::stoi(value)-1);

            for(size_t i=1;i+1<ids.size();++i)
                triangles.push_back({vertices.at(ids[0]),vertices.at(ids[i]),vertices.at(ids[i+1])});
        }
    }

    return triangles;
}
}

int main(){
    try{
        Transform groundTransform;
        Collider ground(&groundTransform);
        ColliderConfig config;
        config.type=ColliderType::Obb;
        config.useModelVolume=true;
        ground.SetConfig(config);
        ground.SetModelSurface(ReadObj("Stages/magma_fortress_foundation"));
        Require(ground.HasModelVolume(),"hull missing");
        float distance{};
        engine::Vector3 normal{};
        Require(ground.RaycastModelVolume({0,10,0},{0,-1,0},30,distance,normal)&&std::abs(distance-10)<0.01f&&normal.y>0.99f,"top ray");
        Require(ground.RaycastModelVolume({0,-10,0},{0,1,0},30,distance,normal)&&std::abs(distance-5.2f)<0.02f&&normal.y<-.99f,"bottom ray");
        Require(ground.RaycastModelVolume({30,-2,0},{-1,0,0},30,distance,normal)&&distance>13&&distance<14.5f&&normal.x>.8f,"side ray");
        Require(!ground.RaycastModelVolume({30,2,0},{-1,0,0},60,distance,normal),"empty air ray");
        Transform actorTransform;
        Collider actor(&actorTransform);
        ColliderConfig actorConfig;
        actorConfig.type=ColliderType::Aabb;
        actorConfig.size={.5f,.5f,.5f};
        actor.SetConfig(actorConfig);
        const auto contact=[&](engine::Vector3 position){actorTransform.translate=position;actorTransform.UpdateMatrix();return actor.CheckCollision(&ground);};
        auto hit=contact({0,.25f,0});
        Require(hit.isColliding&&hit.normal.y>.99f&&std::abs(hit.penetration-.25f)<.01f,"box top contact");
        hit=contact({0,-5,0});
        Require(hit.isColliding&&hit.normal.y<-.99f,"box bottom contact");
        hit=contact({16.6f,-2,0});
        Require(hit.isColliding&&hit.normal.x>.8f,"box side contact");
        Require(!contact({16.5f,.5f,12.4f}).isColliding,"chamfer corner must be empty");
        actorConfig.type=ColliderType::Sphere;
        actor.SetConfig(actorConfig);
        hit=contact({16.6f,-2,0});
        Require(hit.isColliding&&hit.normal.x>.8f,"sphere side");
        hit=contact({0,-5,0});
        Require(hit.isColliding&&hit.normal.y<-.99f,"sphere bottom");
        Require(!contact({17.2f,-2,0}).isColliding,"sphere outside");
        Transform parent;
        parent.scale={2,3,4};
        parent.rotate={0,.4f,0};
        parent.isQuaternionMaster=false;
        parent.translate={10,5,20};
        parent.UpdateMatrix();
        groundTransform.parent=&parent;
        groundTransform.UpdateMatrix();
        Require(ground.RaycastModelVolume({10,10,20},{0,-1,0},30,distance,normal)&&std::abs(distance-5)<.01f,"parent scale ray");
        Require(ground.GetAabb().min.y< -9.39f,"parent scale bounds");
        groundTransform.parent=nullptr;
        groundTransform.rotate={0,0,.2f};
        groundTransform.isQuaternionMaster=false;
        groundTransform.UpdateMatrix();
        Require(ground.RaycastModelVolume({0,10,0},{0,-1,0},30,distance,normal)&&normal.y>.97f&&std::abs(normal.x)>.15f,"sloped normal");
        groundTransform.rotate={};
        groundTransform.isQuaternionMaster=false;
        groundTransform.UpdateMatrix();
        std::ifstream sceneFile("Resources/json/3Dobject/stage2_object.json");
        nlohmann::json scene;
        sceneFile>>scene;
        const auto findSceneObject=[&](const char* name)->const nlohmann::json& {
            for(const auto& obj:scene["objects"])
                if(obj.value("name","")==name)
                    return obj;
            throw std::runtime_error("sinking floor missing");
        };
        const auto readVector=[](const nlohmann::json& value) {
            return engine::Vector3{value[0],value[1],value[2]};
        };
        const auto setSceneTransform=[&](Transform& transform,const nlohmann::json& obj) {
            transform.translate=readVector(obj.at("position"));
            transform.rotate=readVector(obj.at("rotation"));
            transform.scale=readVector(obj.at("scale"));
            transform.isQuaternionMaster=false;
            transform.UpdateMatrix();
        };
        Transform sinkParent,sinkChild;
        const auto& sinkParentData=findSceneObject("SinkWide_1_Copy2");
        const auto& sinkChildData=findSceneObject("SinkWide_1_Copy2_Copy3");
        setSceneTransform(sinkParent,sinkParentData);
        sinkChild.parent=&sinkParent;
        setSceneTransform(sinkChild,sinkChildData);
        Collider slopedChild(&sinkChild);
        slopedChild.SetConfig(config);
        slopedChild.SetModelSurface(ReadObj(sinkChildData.at("modelName")));
        Require(slopedChild.HasModelVolume(),"sloped child hull missing");
        const engine::Vector3 deckCenter=engine::Math::Transform({0,0,0},sinkChild.matWorld);
        Require(slopedChild.RaycastModelVolume(deckCenter+engine::Vector3{0,10,0},
            {0,-1,0},20,distance,normal)&&normal.y>.5f&&std::abs(distance-10)<.2f,
            "scaled parent sloped child top ray");
        actorConfig.type=ColliderType::Sphere;
        actor.SetConfig(actorConfig);
        actorTransform.translate=deckCenter+normal*.4f;
        actorTransform.UpdateMatrix();
        hit=actor.CheckCollision(&slopedChild);
        Require(hit.isColliding&&hit.normal.y>.5f,"scaled parent sloped child contact");
        size_t count=0;
        const auto begin=std::chrono::steady_clock::now();

        for(const auto& obj:scene["objects"]){
            if(!obj.value("collider",nlohmann::json::object()).value("useModelVolume",false))
                continue;
            ground.SetConfig(config);
            const auto triangles=ReadObj(obj.at("modelName"));
            ground.SetModelSurface(triangles);
            Require(ground.HasModelVolume(),"terrain hull missing");
            const auto hull=ground.GetWorldHull();

            for(const auto& t:triangles)for(const auto& p:{t.a,t.b,t.c})for(const auto& f:hull)
                Require(ConvexCollision::Dot(ConvexCollision::Normal(f),p-f[0])<.0003f,"hull clips visible mesh");
            const auto bounds=ground.GetAabb();
            const auto center=(bounds.min+bounds.max)*.5f;
            Require(ground.RaycastModelVolume({center.x,bounds.max.y+10,center.z},{0,-1,0},100,distance,normal)&&normal.y>0,"terrain top");
            Require(ground.RaycastModelVolume({center.x,bounds.min.y-10,center.z},{0,1,0},100,distance,normal)&&normal.y<0,"terrain bottom");
            Require(ground.RaycastModelVolume({bounds.max.x+10,center.y,center.z},{-1,0,0},100,distance,normal)&&normal.x>0,"terrain side");
            ++count;
        }

        // 実配置の動く床対地形も計測する。小さい箱対地形だけでは高負荷を見逃す。
        struct Entry { Transform transform; std::unique_ptr<Collider> collider; nlohmann::json data; };
        std::vector<std::unique_ptr<Entry>> entries;

        for (const auto& obj : scene["objects"]) {
            if (!obj.value("collider",nlohmann::json::object()).value("useModelVolume",false))
                continue;
            auto e=std::make_unique<Entry>();
            e->data=obj;
            const auto vector=[](const auto& a){return engine::Vector3{a[0],a[1],a[2]};};
            e->transform.translate=vector(obj["position"]);
            e->transform.scale=vector(obj["scale"]);
            e->transform.rotate=vector(obj["rotation"]);
            e->transform.isQuaternionMaster=false;
            e->collider=std::make_unique<Collider>(&e->transform);
            e->collider->SetConfig(config);
            e->collider->SetModelSurface(ReadObj(obj["modelName"]));
            entries.push_back(std::move(e));
        }

        for (auto& e:entries) for(auto& parent:entries)
            if(e->data.value("parentGuid","")==parent->data.value("guid","?"))
                e->transform.parent=&parent->transform;
        for(int pass=0;pass<3;++pass)
            for(auto& e:entries)
                e->transform.UpdateMatrix();
        const auto pairsStart=std::chrono::steady_clock::now();
        size_t pairs=0, repetitions=0;

        for(size_t a=0;a<entries.size();++a)for(size_t b=a+1;b<entries.size();++b){
            auto& first=*entries[a];
            auto& second=*entries[b];
            if(first.data.value("isStatic",false)&&second.data.value("isStatic",false))
                continue;
            const auto maskA=first.data.value("collisionMask",0u), maskB=second.data.value("collisionMask",0u);
            const auto attrA=first.data.value("collisionAttribute",0u), attrB=second.data.value("collisionAttribute",0u);
            if (!(maskA&attrB) || !(maskB&attrA))
                continue;
            const auto ba=first.collider->GetAabb(),bb=second.collider->GetAabb();
            int cells=1;

            for(int axis=0;axis<3;++axis){
                const float amin=axis==0?ba.min.x:axis==1?ba.min.y:ba.min.z;
                const float amax=axis==0?ba.max.x:axis==1?ba.max.y:ba.max.z;
                const float bmin=axis==0?bb.min.x:axis==1?bb.min.y:bb.min.z;
                const float bmax=axis==0?bb.max.x:axis==1?bb.max.y:bb.max.z;
                cells*=std::max(0,int(std::floor(std::min(amax,bmax)/5))-int(std::floor(std::max(amin,bmin)/5))+1);
            }

            if(!cells)
                continue;
            if(!first.data.value("isStatic",false)&&!second.data.value("isStatic",false))
                cells=1;
            const auto pairStart=std::chrono::steady_clock::now();

            for(int repeat=0;repeat<cells;++repeat)
                first.collider->CheckCollision(second.collider.get());
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pairStart).count();
            if(ms>5)
                std::cout<<"slow pair "<<first.data["name"]<<" / "<<second.data["name"]<<" repeats="<<cells<<" ms="<<ms<<std::endl;
            ++pairs;
            repetitions+=cells;
        }

        std::cout<<"scene pairs="<<pairs<<" repetitions="<<repetitions<<" ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pairsStart).count()<<std::endl;
        Require(pairs==0,"terrain pairs must be filtered before narrow phase");

        for(const auto& entry:entries){
            const auto mask=entry->data.value("collisionMask",0u);
            Require((mask&4u)==0,"model volume floors must not collide with other ground");
        }

        // 凸包同士の精密判定自体は維持し、地形以外にも使えることを確認する。
        Transform solidTransform;
        Collider solid(&solidTransform);
        solid.SetConfig(config);
        solid.SetModelSurface(ReadObj("Stages/magma_fortress_foundation"));
        ground.SetConfig(config);
        ground.SetModelSurface(ReadObj("Stages/magma_fortress_foundation"));
        solidTransform.translate={0,-4,0};
        solidTransform.UpdateMatrix();
        hit=solid.CheckCollision(&ground);
        Require(hit.isColliding && hit.normal.y<-.99f && std::abs(hit.penetration-.8f)<.01f,"hull pair bottom contact");
        solidTransform.translate={0,-5,0};
        solidTransform.UpdateMatrix();
        Require(!solid.CheckCollision(&ground).isColliding,"hull pair separated");

        // 門の中央には判定を置かず、左右の柱だけに接触することを子Colliderで確認する。
        bool touchedPost=false;
        size_t gateParts=0;
        actorConfig.type=ColliderType::Aabb;
        actor.SetConfig(actorConfig);

        for(const auto& obj:scene["objects"]){
            if(obj.value("parentName","")!="Stage2_GatehouseGate")
                continue;
            Transform partTransform;
            Collider part(&partTransform);
            ColliderConfig partConfig;
            partConfig.type=ColliderType::Obb;
            const auto& col=obj.at("collider");
            partConfig.center={col["center"][0],col["center"][1],col["center"][2]};
            partConfig.size={col["size"][0],col["size"][1],col["size"][2]};
            part.SetConfig(partConfig);
            actorTransform.translate={0,3,0};
            actorTransform.UpdateMatrix();
            Require(!actor.CheckCollision(&part).isColliding,"gate opening blocked");
            actorTransform.translate={5.4f,3,0};
            actorTransform.UpdateMatrix();
            touchedPost=touchedPost||actor.CheckCollision(&part).isColliding;
            ++gateParts;
        }

        if (gateParts > 0)
            Require(touchedPost&&gateParts==13,"gate parts missing");
        ground.SetConfig(config);
        ground.SetModelSurface(ReadObj("Stages/magma_fortress_foundation"));
        const auto performanceStart=std::chrono::steady_clock::now();

        for(int i=0;i<1000;++i)
            Require(contact({0,.25f,0}).isColliding,"repeated top contact");
        std::cout<<"1000 contacts ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-performanceStart).count()<<'\n';
        std::cout<<"PASS top/bottom/side, chamfer, sphere/box, parent scale, slope; terrain="<<count
                 <<" audit_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<'\n';
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';
    return 1;
    }
}
