#include "state/PlayerStateShared.h"

// ========================================================
// 静的変数のリセット関数（シーン切り替えやリトライ時の初期化用）
// ========================================================
void ResetPlayerStateStatics() {
  s_bodyBlendActive = false;
  s_bodyStartY = 0.0f;
  s_bodyTargetY = 0.0f;
  s_bodyStartRotVec = {0, 0, 0};
  s_bodyTargetRotVec = {0, 0, 0};
  s_pendingIdleBlend = {};
}
