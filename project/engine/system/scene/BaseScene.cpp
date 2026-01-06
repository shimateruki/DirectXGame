#include "BaseScene.h"

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