#pragma once

#include <algorithm>
#include <span>
#include <string_view>

// 敵固有の候補・状態は所有せず、攻撃の対応付けと選択手順だけを共有する。
namespace EnemyAttackSelection {
// 敵内の技の列挙値と、EnemyAttackProfileの保存IDを対応させる一行。
template<class Kind>
struct Entry {
    Kind kind;
    const char* id;
};

template<class Kind>
const char* FindId(std::span<const Entry<Kind>> entries, Kind kind, const char* fallback) {
    for (const auto& entry : entries) {
        if (entry.kind == kind)
            return entry.id;
    }
    return fallback;
}

template<class Kind>
Kind FindKind(std::span<const Entry<Kind>> entries, std::string_view id, Kind fallback) {
    for (const auto& entry : entries) {
        if (id == entry.id)
            return entry.kind;
    }
    return fallback;
}

// 呼び出し元が選んだ有効範囲だけを混ぜる。乱数の所有者と消費順は従来どおり。
template<class Kind, class Random>
void Shuffle(std::span<Kind> cycle, Kind previous, Random& random) {
    std::shuffle(cycle.begin(), cycle.end(), random);
    if (cycle.size() > 1 && cycle.front() == previous)
        std::swap(cycle[0], cycle[1]);
}

// cycleは空でなく、currentが有効な状態で呼ぶ。周期の再構築は敵側が担当する。
// 先頭の連続技を避け、最初に射程が合う未使用技を繰り上げる。全候補が外れても元の技を返す。
template<class Kind, class InRange>
Kind SelectInRange(std::span<Kind> cycle, std::size_t current, Kind previous, InRange&& inRange) {
    for (std::size_t i = current; i < cycle.size(); ++i) {
        if (current == 0 && cycle[i] == previous)
            continue;
        if (inRange(cycle[i])) {
            std::swap(cycle[current], cycle[i]);
            break;
        }
    }
    return cycle[current];
}
} // namespace EnemyAttackSelection
