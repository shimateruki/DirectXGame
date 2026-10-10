#pragma once

#include <string>

// 共通攻撃シーケンスの選択と現在段階の表示に対応する敵だけが実装する。
class ISequencePreviewEnemy {
  public:
    virtual ~ISequencePreviewEnemy() = default;
    virtual void SetSequencePreviewAttackId(const std::string& attackId) = 0;
    virtual const char* GetSequenceStateLabel() const = 0;
};
