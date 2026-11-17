#define NOMINMAX
#include "engine/editor/assets/JsonAssetBrowser.h"

#include <algorithm>

#ifdef USE_IMGUI
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace {
ImGuiID bottomDockId = 0;

std::string Utf8(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}

fs::path FromUtf8(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

bool ValidName(const std::string& name) {
    return !name.empty() && name.find_first_of("<>:\"/\\|?*") == std::string::npos &&
        name.back() != '.' && name.back() != ' ' &&
        std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; });
}

// ルート外へのリンクや相対パスを経由した読み書きを防ぐ。
bool Within(const fs::path& root, const fs::path& path) {
    for (const auto& part : path.lexically_relative(root)) {
        if (part == "..") return false;
    }
    const auto base = fs::weakly_canonical(root);
    const auto target = fs::weakly_canonical(path);
    const auto relative = target.lexically_relative(base);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

// 既存コード・設定に名前が見つかる場合は安全側に倒して移動を止める。
bool HasReference(const fs::path& target, const fs::path& rootDirectory, const fs::path& archive) {
    const auto filename = Utf8(target.filename());
    auto relative = target.lexically_relative(rootDirectory);
    const auto assetKey = Utf8(relative.replace_extension());
    if (!fs::is_directory("game") || !fs::is_directory("engine")) return true;
    for (const fs::path root : {"game", "engine", "Resources/json"}) {
        for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
            const auto& path = it->path();
            if (it->is_directory() && path.filename() == archive.filename()) {
                it.disable_recursion_pending();
                continue;
            }
            const auto ext = path.extension();
            if (ext != ".json" && ext != ".cpp" && ext != ".h" && ext != ".inl") continue;
            if (fs::equivalent(path, target)) continue;
            std::ifstream file(path, std::ios::binary);
            if (!file) return true;
            const std::string content((std::istreambuf_iterator<char>(file)), {});
            if (content.find(filename) != std::string::npos ||
                content.find('"' + Utf8(target.stem()) + '"') != std::string::npos ||
                content.find('"' + assetKey + '"') != std::string::npos) return true;
        }
    }
    return false;
}
}
#endif

std::vector<std::string> JsonAssetBrowser::ListKeys(const Path& root) {
    std::vector<std::string> keys;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || it->path().extension() != ".json") continue;
        auto relative = it->path().lexically_relative(root);
        const auto key = relative.replace_extension().generic_u8string();
        keys.emplace_back(key.begin(), key.end());
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

void JsonAssetBrowser::RequestSaveAs(const std::string& suggestedName) {
    if (suggestedName.size() < sizeof(name_)) {
        suggestedName.copy(name_, suggestedName.size());
        name_[suggestedName.size()] = '\0';
    } else {
        name_[0] = '\0';
    }
    archive_ = false;
    saveAsRequested_ = true;
}

void JsonAssetBrowser::SetBottomDock(unsigned int dockId) {
#ifdef USE_IMGUI
    bottomDockId = dockId;
#else
    (void)dockId;
#endif
}

void JsonAssetBrowser::SetDocument(const Path& relative) {
    opened_ = relative;
    if (relative.empty()) saveAsRequested_ = false;
    if (!relative.empty()) {
        folder_ = relative.parent_path();
        selected_ = relative;
        archive_ = false;
    }
}

void JsonAssetBrowser::Draw(const char* title, const Path& root, const Read& read, const Write& write,
    const std::function<void()>& reset, const std::function<void(const Path&)>& onSaved) {
#ifdef USE_IMGUI
    if (bottomDockId == 0) return;
    ImGui::SetNextWindowDockID(bottomDockId, ImGuiCond_Always);
    if (saveAsRequested_) ImGui::SetNextWindowFocus();
    const bool visible = ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    if (visible) {
        if (saveAsRequested_) {
            ImGui::OpenPopup("別名保存");
            saveAsRequested_ = false;
        }
        try {
            const auto archiveRoot = fs::path(".editor-json-trash") / root.filename();
            const auto base = archive_ ? archiveRoot : root;
            if (ImGui::Button(archive_ ? "ファイルに戻る" : ICON_FA_TRASH " ゴミ箱")) {
                archive_ = !archive_;
                folder_.clear();
                selected_.clear();
            } else {
                const auto directory = base / folder_;
                ImGui::SameLine();
                ImGui::TextUnformatted(Utf8(directory).c_str());
                if (!folder_.empty() && ImGui::Button(ICON_FA_ARROW_UP " 上へ")) {
                    folder_ = folder_.parent_path();
                    selected_.clear();
                } else if (Within(base, directory) && fs::is_directory(directory)) {
                    std::vector<fs::directory_entry> items;
                    for (const auto& item : fs::directory_iterator(directory)) {
                        if (item.path().filename() == ".editor-trash") continue;
                        if ((item.is_directory() || (item.is_regular_file() && item.path().extension() == ".json"))
                            && Within(base, item.path())) items.push_back(item);
                    }
                    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
                        if (a.is_directory() != b.is_directory()) return a.is_directory();
                        return a.path().filename() < b.path().filename();
                    });
                    if (ImGui::BeginChild("json-files", ImVec2(0, 155), true)) {
                        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
                        for (const auto& item : items) {
                            const auto relative = item.path().lexically_relative(base);
                            const auto label = Utf8(item.path().filename());
                            ImGui::PushID(label.c_str());
                            ImGui::BeginGroup();
                            const bool clicked = ImGui::Button(item.is_directory() ? ICON_FA_FOLDER : ICON_FA_FILE_CODE,
                                ImVec2(76, 35));
                            const bool open = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
                            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 104.0f);
                            ImGui::TextUnformatted(label.c_str());
                            ImGui::PopTextWrapPos();
                            ImGui::TextDisabled("%s", selected_ == relative ? "選択中" : " ");
                            ImGui::EndGroup();
                            ImGui::PopID();
                            if (clicked) selected_ = relative;
                            if (open && item.is_directory()) {
                                folder_ = relative;
                                selected_.clear();
                                break;
                            }
                            if (open && !archive_) {
                                pending_ = relative;
                                ImGui::OpenPopup("読み込み確認");
                            }
                            if (ImGui::GetItemRectMax().x + 125 < right)
                                ImGui::SameLine();
                        }
                    }
                    ImGui::EndChild();
                }

                auto save = [&](bool replace) {
                    const auto target = root / pending_;
                    if (!Within(root, target)) { status_ = "保存先が無効です。"; return; }
                    fs::create_directories(target.parent_path());
                    wchar_t temporary[MAX_PATH]{};
                    if (!GetTempFileNameW(fs::absolute(target.parent_path()).c_str(), L"jsn", 0, temporary)) {
                        status_ = "一時ファイルを作成できません。";
                        return;
                    }
                    const fs::path temp(temporary);
                    bool saved = false;
                    try {
                        saved = write(temp, Utf8(target.stem())) && MoveFileExW(temp.c_str(), target.c_str(),
                            replace ? MOVEFILE_REPLACE_EXISTING : 0);
                    } catch (const std::exception&) {
                        saved = false;
                    }
                    std::error_code error;
                    fs::remove(temp, error);
                    if (saved) {
                        opened_ = selected_ = pending_;
                        if (onSaved) onSaved(opened_);
                    }
                    status_ = saved ? "保存しました。" : "保存できませんでした。";
                };

                const auto selected = base / selected_;
                const bool hasFile = !selected_.empty() && Within(base, selected) && fs::is_regular_file(selected);
                if (archive_) {
                    if (hasFile && ImGui::Button("選択したJSONを復元")) {
                        const auto target = root / selected_;
                        if (!Within(root, target) || fs::exists(target)) status_ = "復元先に同名ファイルがあります。";
                        else {
                            fs::create_directories(target.parent_path());
                            fs::rename(selected, target);
                            selected_.clear();
                            status_ = "復元しました。";
                        }
                    }
                } else {
                    ImGui::Text("編集中: %s", opened_.empty() ? "新規" : Utf8(opened_).c_str());
                    if (ImGui::Button(ICON_FA_PLUS " 新規")) ImGui::OpenPopup("新規確認");
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_SAVE " 保存")) {
                        if (opened_.empty()) ImGui::OpenPopup("別名保存");
                        else {
                            pending_ = opened_;
                            save(true);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("別名保存")) ImGui::OpenPopup("別名保存");
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_FOLDER " フォルダー作成")) ImGui::OpenPopup("新規フォルダー");
                    if (hasFile && ImGui::Button("開く")) {
                        pending_ = selected_;
                        ImGui::OpenPopup("読み込み確認");
                    }
                    if (hasFile) {
                        ImGui::SameLine();
                        if (ImGui::Button("ゴミ箱へ移動")) {
                            pending_ = selected_;
                            ImGui::OpenPopup("削除確認");
                        }
                    }
                }

                auto modal = [&](const char* label, auto content) {
                    if (ImGui::BeginPopupModal(label, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        try { content(); }
                        catch (const std::exception&) { status_ = "ファイル操作に失敗しました。"; }
                        if (ImGui::Button("閉じる")) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                };
                modal("新規確認", [&] {
                    ImGui::TextUnformatted("編集中の内容を新規データで置き換えます。");
                    if (ImGui::Button("新規作成")) { reset(); opened_.clear(); name_[0] = 0; ImGui::CloseCurrentPopup(); }
                });
                modal("読み込み確認", [&] {
                    ImGui::TextWrapped("%s を開き、編集中の内容を置き換えます。", Utf8(pending_).c_str());
                    if (ImGui::Button("読み込む")) {
                        if (Within(root, root / pending_) && read(root / pending_)) opened_ = pending_;
                        else status_ = "読み込めませんでした。";
                        ImGui::CloseCurrentPopup();
                    }
                });
                modal("新規フォルダー", [&] {
                    ImGui::InputText("名前", folderName_, sizeof(folderName_));
                    if (ImGui::Button("作成") && ValidName(folderName_)) {
                        const auto target = directory / FromUtf8(folderName_);
                        if (Within(root, target) && fs::create_directories(target)) {
                            folder_ = target.lexically_relative(root);
                            selected_.clear();
                            ImGui::CloseCurrentPopup();
                        } else status_ = "フォルダーを作成できませんでした。";
                    }
                });
                modal("別名保存", [&] {
                    ImGui::Text("保存先: %s", Utf8(directory).c_str());
                    ImGui::InputText("名前（拡張子なし）", name_, sizeof(name_));
                    if (ValidName(name_)) {
                        pending_ = folder_ / FromUtf8(std::string(name_) + ".json");
                        const bool exists = fs::exists(root / pending_);
                        if (ImGui::Button(exists ? "同名ファイルを上書き" : "保存する")) {
                            save(exists);
                            ImGui::CloseCurrentPopup();
                        }
                    }
                });
                modal("削除確認", [&] {
                    ImGui::TextWrapped("%s をゴミ箱に移します。", Utf8(pending_).c_str());
                    if (ImGui::Button("移動する")) {
                        const auto source = root / pending_;
                        const auto target = archiveRoot / pending_;
                        if (!Within(root, source) || !Within(archiveRoot, target) || fs::exists(target) || HasReference(source, root, archiveRoot))
                            status_ = "参照されているか、同名のゴミ箱ファイルがあるため移動できません。";
                        else {
                            fs::create_directories(target.parent_path());
                            fs::rename(source, target);
                            if (opened_ == pending_) opened_.clear();
                            selected_.clear();
                            status_ = "ゴミ箱に移しました。";
                        }
                        ImGui::CloseCurrentPopup();
                    }
                });
            }
        } catch (const std::exception&) {
            status_ = "フォルダーを表示できませんでした。";
        }
        if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
    }
    ImGui::End();
#endif
}
