//================================================================
// ModelCommon.cpp (新規作成)
//================================================================
#include "ModelCommon.h"
#include <cassert>

void ModelCommon::Initialize(DirectXCommon* dxCommon) {
    // 引数のNULLチェック
    assert(dxCommon);
    // メンバー変数に設定
    dxCommon_ = dxCommon;
}