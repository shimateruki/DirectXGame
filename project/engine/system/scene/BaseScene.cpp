#include "BaseScene.h"
#include "Object3d.h"
void BaseScene::TriggerEvent(int targetID) {
    // IDが -1 (設定なし) なら何もしない
    if (targetID == -1) return;



    // シーン内の全オブジェクトを取得 
    auto& objects = GetObjects();

    for (auto& obj : objects) {
        // 「俺の受信ID、呼ばれた番号と同じだ！」
        if (obj->GetEventID() == targetID) {
            // アクションを実行！
            obj->OnTrigger();
        }
    }
}

void BaseScene::SetEventActive(int targetID, bool active) {
    if (targetID == -1) return;

    auto& objects = GetObjects();
    for (auto& obj : objects) {
        if (obj->GetEventID() == targetID) {
            obj->OnSwitchEvent(active);
        }
    }
}

Object3d* BaseScene::FindObjectByEventID(int eventID) {
    // IDなしなら無視
    if (eventID == -1) return nullptr;

    auto& objects = GetObjects();
    for (auto& obj : objects) {
        if (obj->GetEventID() == eventID) {
            return obj.get(); // 見つけたオブジェクトそのものを返す！
        }
    }
    return nullptr;
}

// 名前からスプライトを取得する
Sprite* BaseScene::GetSpriteByName(const std::string& name) {
    auto& sprites = GetSprites();
    for (auto& sprite : sprites) {
        if (sprite && sprite->GetName() == name) {
            return sprite.get();
        }
    }
    return nullptr; // 見つからなかった場合
}
