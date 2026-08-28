#include "GameplayEventTrace.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
std::string ToLowerAscii(std::string value) {
    for (char& character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x80) {
            character = static_cast<char>(std::tolower(byte));
        }
    }
    return value;
}

bool MatchesFilter(const GameplayEventTraceEntry& entry, const std::string& filter) {
    if (filter.empty()) return true;
    const std::string haystack = ToLowerAscii(
        entry.category + "\n" + entry.source + "\n" + entry.asset + "\n" + entry.detail);
    return haystack.find(ToLowerAscii(filter)) != std::string::npos;
}
}

GameplayEventTrace* GameplayEventTrace::GetInstance() {
    static GameplayEventTrace instance;
    return &instance;
}

GameplayEventTrace::GameplayEventTrace()
    : startTime_(std::chrono::steady_clock::now()) {
}

void GameplayEventTrace::Record(
    GameplayEventTracePhase phase,
    std::string category,
    std::string source,
    std::string asset,
    std::string detail) {
    if (!captureEnabled_.load()) {
        return;
    }

    std::scoped_lock lock(mutex_);
    GameplayEventTraceEntry entry;
    entry.sequence = nextSequence_++;
    entry.timestampSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
    entry.phase = phase;
    entry.category = std::move(category);
    entry.source = std::move(source);
    entry.asset = std::move(asset);
    entry.detail = std::move(detail);

    entries_.push_back(std::move(entry));
    while (entries_.size() > kMaxEntries) {
        entries_.pop_front();
    }
}

void GameplayEventTrace::Clear() {
    std::scoped_lock lock(mutex_);
    entries_.clear();
    selectedSequence_ = 0;
    startTime_ = std::chrono::steady_clock::now();
}

const char* GameplayEventTrace::GetPhaseName(GameplayEventTracePhase phase) {
    switch (phase) {
    case GameplayEventTracePhase::Requested: return "Request";
    case GameplayEventTracePhase::Started: return "Start";
    case GameplayEventTracePhase::Fired: return "Fire";
    case GameplayEventTracePhase::Completed: return "Complete";
    case GameplayEventTracePhase::Cancelled: return "Cancel";
    case GameplayEventTracePhase::Warning: return "Warning";
    case GameplayEventTracePhase::Failed: return "Failed";
    default: return "Unknown";
    }
}

void GameplayEventTrace::DrawImGui() {
#ifdef USE_IMGUI
    if (!isOpen_) {
        return;
    }
    if (!ImGui::Begin("Gameplay Event Trace", &isOpen_)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) {
        Clear();
    }
    ImGui::SameLine();
    bool captureEnabled = captureEnabled_.load();
    if (ImGui::Checkbox("Capture", &captureEnabled)) {
        captureEnabled_.store(captureEnabled);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto Scroll", &autoScroll_);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##EventTraceFilter", "source / asset / eventで検索", filter_, sizeof(filter_));

    std::vector<GameplayEventTraceEntry> snapshot;
    {
        std::scoped_lock lock(mutex_);
        snapshot.assign(entries_.begin(), entries_.end());
    }
    const std::string filter = filter_;

    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("GameplayEventTraceTable", 6, flags, ImVec2(0.0f, -105.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 68.0f);
        ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 112.0f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 0.75f);
        ImGui::TableSetupColumn("Asset / Event", ImGuiTableColumnFlags_WidthStretch, 1.25f);
        ImGui::TableHeadersRow();

        for (const GameplayEventTraceEntry& entry : snapshot) {
            if (!MatchesFilter(entry, filter)) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool selected = selectedSequence_ == entry.sequence;
            const std::string rowId = std::to_string(entry.sequence) + "##TraceRow";
            if (ImGui::Selectable(rowId.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                selectedSequence_ = entry.sequence;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", entry.timestampSeconds);
            ImGui::TableSetColumnIndex(2);
            ImVec4 phaseColor = { 0.72f, 0.82f, 1.0f, 1.0f };
            if (entry.phase == GameplayEventTracePhase::Warning) phaseColor = { 1.0f, 0.72f, 0.25f, 1.0f };
            if (entry.phase == GameplayEventTracePhase::Failed) phaseColor = { 1.0f, 0.35f, 0.3f, 1.0f };
            if (entry.phase == GameplayEventTracePhase::Completed) phaseColor = { 0.45f, 1.0f, 0.58f, 1.0f };
            ImGui::TextColored(phaseColor, "%s", GetPhaseName(entry.phase));
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(entry.category.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(entry.source.c_str());
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(entry.asset.c_str());
        }
        if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 6.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndTable();
    }

    const auto selected = std::find_if(
        snapshot.begin(),
        snapshot.end(),
        [this](const GameplayEventTraceEntry& entry) {
            return entry.sequence == selectedSequence_;
        });
    if (selected != snapshot.end()) {
        ImGui::SeparatorText("Selected Event");
        ImGui::TextWrapped(
            "%s / %s / %s",
            selected->category.c_str(),
            selected->source.c_str(),
            selected->asset.c_str());
        ImGui::TextWrapped("%s", selected->detail.empty() ? "(detailなし)" : selected->detail.c_str());
    } else {
        ImGui::TextDisabled("行を選ぶと詳細を表示します。最大%zu件を保持します。", kMaxEntries);
    }

    ImGui::End();
#endif
}
