#include "ModelCommon.h"
#include <cassert>

void ModelCommon::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
}