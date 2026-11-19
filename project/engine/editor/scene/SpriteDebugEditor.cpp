#define NOMINMAX
#include "SpriteDebugEditor.h"
#include "WinApp.h"
#include "Sprite.h"
#include "imgui.h"
#include "json.hpp"
#include <fstream>
#include <string>
#include "SpriteCommon.h" 
#include "SceneManager.h"  
#include "BaseScene.h"    
#include "InputManager.h"
#include <filesystem>
#include "IconsFontAwesome5.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include "game/ui/SpriteLayoutFactory.h"
#include "engine/graphics/2d/SpriteLayoutAnimation.h"
#include "engine/graphics/2d/SpriteLayoutVideo.h"
#include <chrono>
#include"WinApp.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "DirectXCommon.h"
namespace fs = std::filesystem;

namespace {
constexpr float kSpriteAspectWarningThreshold = 0.02f;

std::string NormalizeSpritePath(std::string path) {
	// Windows形式とスラッシュ形式を同じ参照として扱い、文字画像のフォルダ名を照合する。
	std::replace(path.begin(), path.end(), '\\', '/');
	std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
		});
	return path;
}
}


// 元のJSONと各Spriteの元データを保持し、配置以外の項目も保存時に引き継ぎます。
struct SpriteDebugEditor::LayoutDocument {
    SpriteList sprites;
    nlohmann::json source;
    std::unordered_map<const engine::Sprite*, nlohmann::json> originalSprites;
    fs::path path;
    engine::Vector2 displayRatio{1.0f, 1.0f}; // 設計ピクセルから編集中の画面ピクセルへの倍率。
    std::unique_ptr<engine::Sprite> background;
    bool isNew = false;
    bool previewPlaying = false;
    std::chrono::steady_clock::time_point previewStart;
    std::chrono::steady_clock::time_point videoTick;
    std::unique_ptr<VideoSpritePlayer> videoPreview;
    engine::Sprite* videoSprite = nullptr;
    uint32_t videoPoster = 0;

    // 動画テクスチャは共有なので、停止時に静止画へ戻してGPUの利用権を解放します。
    void StopVideo() {
        if (videoSprite)
            SpriteLayoutVideo::SetTexture(*videoSprite, videoPoster);
        videoSprite = nullptr;
        if (videoPreview)
            videoPreview->Close();
    }

    // 選択したSpriteを映像へ切り替え、復帰に使う元画像と再生時刻を保持します。
    bool StartVideo(engine::Sprite& sprite, const std::string& path) {
        StopVideo();
        const auto file = SpriteLayoutVideo::Resolve(path);
        if (file.empty())
            return false;
        if (!videoPreview)
            videoPreview = std::make_unique<VideoSpritePlayer>();
        videoSprite = &sprite;
        videoPoster = sprite.GetTextureHandle();
        videoTick = std::chrono::steady_clock::now();
        videoPreview->RequestOpen(file);
        return true;
    }
};

SpriteDebugEditor::SpriteDebugEditor() = default;
SpriteDebugEditor::~SpriteDebugEditor() = default;

const SpriteDebugEditor::SpriteList& SpriteDebugEditor::GetEditingSprites(const BaseScene& scene) const {
    return layoutDocument_ ? layoutDocument_->sprites : scene.GetSprites();
}

engine::Sprite* SpriteDebugEditor::AddEditingSprite(BaseScene& scene, std::unique_ptr<engine::Sprite> sprite) {
    if (!layoutDocument_)
        return scene.AddSprite(std::move(sprite));
    const std::string baseName = sprite->GetName();
    for (int index = 2; std::any_of(layoutDocument_->sprites.begin(), layoutDocument_->sprites.end(),
         [&sprite](const auto& item) { return item->GetName() == sprite->GetName(); }); ++index) {
        sprite->SetName(baseName + "_" + std::to_string(index));
    }
    auto* result = sprite.get();
    layoutDocument_->sprites.push_back(std::move(sprite));
    return result;
}

bool SpriteDebugEditor::DeleteEditingSprite(BaseScene& scene, engine::Sprite* sprite) {
    if (!layoutDocument_)
        return scene.DestroySprite(sprite);
    auto& sprites = layoutDocument_->sprites;
    const auto position = std::find_if(sprites.begin(), sprites.end(),
        [sprite](const auto& item) { return item.get() == sprite; });
    if (position == sprites.end())
        return false;

    // 親を消しても、残る子はキャンバス上の位置を維持します。
    const auto children = sprite->GetChildren();
    for (auto* child : children)
        child->SetParent(nullptr);
    if (layoutDocument_->videoSprite == sprite)
        layoutDocument_->StopVideo();
    layoutDocument_->originalSprites.erase(sprite);
    sprites.erase(position);
    return true;
}

void SpriteDebugEditor::MoveEditingSprite(BaseScene& scene, engine::Sprite* sprite, int offset) {
    if (!layoutDocument_) {
        scene.MoveSprite(sprite, offset);
        return;
    }
    auto& sprites = layoutDocument_->sprites;
    const auto position = std::find_if(sprites.begin(), sprites.end(),
        [sprite](const auto& item) { return item.get() == sprite; });
    const auto next = std::distance(sprites.begin(), position) + offset;
    if (position != sprites.end() && next >= 0 && next < static_cast<std::ptrdiff_t>(sprites.size()))
        std::iter_swap(position, sprites.begin() + next);
}

void SpriteDebugEditor::SetSelectedSprite(engine::Sprite* sprite) {
    // 他のツールがシーンにスプライトを追加しても、編集中のレイアウトには混ぜません。
    if (!layoutDocument_)
        selectedSprite_ = sprite;
}

void SpriteDebugEditor::Initialize(SceneManager* sceneManager, engine::InputManager* inputManager) {
    // Sceneと入力を借用し、前SceneのSprite選択とギズモ操作を初期化します。
	sceneManager_ = sceneManager;
	inputManager_ = inputManager;
	selectedSprite_ = nullptr;
	isMovingX_ = false;
	isMovingY_ = false;


	gizmoArrowX_ = nullptr;
	gizmoArrowY_ = nullptr;
	gizmoTextureHandle_ = 0;
	initializedSpriteCommon_ = nullptr;
}


void SpriteDebugEditor::Finalize() {
	ClearSceneSelection();
	layoutDocument_.reset();
}

void SpriteDebugEditor::ClearSceneSelection() {
    if (layoutDocument_)
        layoutDocument_->StopVideo();
    // Scene切り替えでSpriteと描画基盤の参照が失効するため、ギズモも解放します。
	selectedSprite_ = nullptr;
	isMovingX_ = false;
	isMovingY_ = false;
	lastUpdatedScene_ = nullptr;
	initializedSpriteCommon_ = nullptr;
	gizmoArrowX_.reset();
	gizmoArrowY_.reset();
}

void SpriteDebugEditor::Update(const engine::Vector2& localMousePos, bool isHovered) {
#ifdef USE_IMGUI
	if (sceneManager_ == nullptr)
		return;

	BaseScene* currentScene = sceneManager_->GetCurrentScene();

	if (currentScene == nullptr) {
		selectedSprite_ = nullptr;
		return;
	}

	// シーン切り替え時のリセット
	if (lastUpdatedScene_ != currentScene) {
		selectedSprite_ = nullptr;
		isMovingX_ = false;
		isMovingY_ = false;
		strcpy_s(currentSpriteFilename_, sizeof(currentSpriteFilename_), "Please_Select_Or_Set.json");
		lastUpdatedScene_ = currentScene;
	}

	auto& currentSprites = GetEditingSprites(*currentScene);
	// 選択済みのポインタは現シーンの一覧に残っているか比較し、削除後の実体を参照しない。
	if (selectedSprite_ &&
		std::none_of(currentSprites.begin(), currentSprites.end(), [this](const std::unique_ptr<engine::Sprite>& spritePtr) {
			return spritePtr && spritePtr.get() == selectedSprite_;
		})) {
		selectedSprite_ = nullptr;
		isMovingX_ = false;
		isMovingY_ = false;
	}

	static std::string lastSyncedSpriteFilename = "";
	std::string currentLoadedName = currentScene->GetLoadedSpriteFilename();

	// 読み込み名が変わった瞬間だけ、エディタにセットする！
	if (!layoutDocument_ && !currentLoadedName.empty() && lastSyncedSpriteFilename != currentLoadedName) {
		SetSpriteFilename(currentLoadedName);
		lastSyncedSpriteFilename = currentLoadedName;
	}

	if (selectedSprite_ != nullptr) {
		// 文字入力中（名前の変更中など）でなければショートカットを許可
		if (!ImGui::GetIO().WantCaptureKeyboard) {

			// --- 削除 (Delete) ---
			if (inputManager_->IsKeyTriggered(DIK_DELETE)) {
				if (DeleteEditingSprite(*currentScene, selectedSprite_)) {
					selectedSprite_ = nullptr;
				}
			}

			// 削除された場合はここで抜ける
			if (selectedSprite_ == nullptr)
				goto SKIP_SHORTCUTS;

			// --- 複製 (Ctrl + D または Ctrl + C) ---
			// ※ InputManager の「キーを押しっぱなし」を判定する関数名（IsKeyPressed など）に合わせてください
			bool isCtrlDown = inputManager_->IsKeyPressed(DIK_LCONTROL) || inputManager_->IsKeyPressed(DIK_RCONTROL);

			// Ctrl + C (コピー) または Ctrl + D (複製)
			if (isCtrlDown && (inputManager_->IsKeyTriggered(DIK_C) || inputManager_->IsKeyTriggered(DIK_D))) {

				auto newSprite = std::make_unique<engine::Sprite>();

				// 元のスプライトからテクスチャを取得して初期化
				uint32_t handle = selectedSprite_->GetTextureHandle();
				newSprite->Initialize(currentScene->GetSpriteCommon(), handle);

				// 元のスプライトのパラメータを丸コピー
				newSprite->SetName(selectedSprite_->GetName() + "_Copy");

				// ※完全に重なると見えないので、少しズラして配置する
				engine::Vector2 selectedWorldPos = selectedSprite_->GetWorldPosition();
				newSprite->SetPosition({ selectedWorldPos.x + 20.0f, selectedWorldPos.y + 20.0f });
				newSprite->SetSize(selectedSprite_->GetSize());
				newSprite->SetAnchorPoint(selectedSprite_->GetAnchorPoint());
				newSprite->SetColor(selectedSprite_->GetColor());
                newSprite->SetTextureName(selectedSprite_->GetTextureName());
                newSprite->SetEmissive(selectedSprite_->GetEmissive());
                newSprite->SetRotation(selectedSprite_->GetRotation());

				// (もしフリップや回転などがあれば、ここに追加でコピー処理を書きます)

				// シーンのリストに追加
				// 複製したスプライトを即座に「選択状態」にする（続けて移動などができるように）
				selectedSprite_ = AddEditingSprite(*currentScene, std::move(newSprite));
			}

			if (!selectedSprite_->IsLocked()) {

				// IsKeyPressed を使うと「押しっぱなし」で滑らかに動きます。
				// Shiftキーを押している間は 5.0 ピクセル/フレームの高速移動、
				// 通常時は 0.5 ピクセル/フレームの微細な移動にします。
				float nudgeStep = (inputManager_->IsKeyPressed(DIK_LSHIFT) || inputManager_->IsKeyPressed(DIK_RSHIFT)) ? 5.0f : 0.5f;

				engine::Vector2 pos = selectedSprite_->GetPosition();
				bool isNudged = false;

				if (inputManager_->IsKeyPressed(DIK_UP)) {
					pos.y -= nudgeStep;
					isNudged = true;
				}
				if (inputManager_->IsKeyPressed(DIK_DOWN)) {
					pos.y += nudgeStep;
					isNudged = true;
				}
				if (inputManager_->IsKeyPressed(DIK_LEFT)) {
					pos.x -= nudgeStep;
					isNudged = true;
				}
				if (inputManager_->IsKeyPressed(DIK_RIGHT)) {
					pos.x += nudgeStep;
					isNudged = true;
				}

				if (isNudged) {
					selectedSprite_->SetPosition(pos);
				}
			}
		}
	}

SKIP_SHORTCUTS:

	// ドラッグ中は Game View の外へ出ても、ボタンを離すまで操作を継続する。
	if (!isHovered && !isMovingX_ && !isMovingY_)
		return;

	// ギズモ操作 (簡易移動)
	if (selectedSprite_) {
		// ロックされていない時だけドラッグを開始可能
		if (!selectedSprite_->IsLocked()) {
			// マウスが押された瞬間
			if (inputManager_->IsMouseButtonTriggered(0) && IsMouseOver(selectedSprite_, localMousePos)) {
				isMovingX_ = true;
				isMovingY_ = true;
				dragStartMousePos_ = localMousePos;
				dragStartSpritePos_ = selectedSprite_->GetPosition();
			}

			// ドラッグ中
			if (inputManager_->IsMouseButtonPressed(0) && (isMovingX_ || isMovingY_)) {
				engine::Vector2 delta = { localMousePos.x - dragStartMousePos_.x, localMousePos.y - dragStartMousePos_.y };
				engine::Vector2 newPos = { dragStartSpritePos_.x + delta.x, dragStartSpritePos_.y + delta.y };

				// Shiftキーを押している間は10ピクセル単位でスナップ
				if (inputManager_->IsKeyPressed(DIK_LSHIFT) || inputManager_->IsKeyPressed(DIK_RSHIFT)) {
					float snapStep = 10.0f;
					newPos.x = std::round(newPos.x / snapStep) * snapStep;
					newPos.y = std::round(newPos.y / snapStep) * snapStep;
				}

				selectedSprite_->SetPosition(newPos);
			}
		} // <- if(!selectedSprite_->IsLocked()) の閉じカッコ

		if (inputManager_->IsMouseButtonReleased(0)) {
			isMovingX_ = false;
			isMovingY_ = false;
		}
	}

	// --- 2. スプライト選択 ---
	// ドラッグ中でなく、クリックされた瞬間
	// クリックが既存Spriteのドラッグ開始に使われた場合は、別のSpriteに選択を移さない。
	if (!isMovingX_ && !isMovingY_ && inputManager_->IsMouseButtonTriggered(0)) {
		auto& sprites = GetEditingSprites(*currentScene);
		bool hit = false;

		// 重なり順を考慮して逆順（手前）からチェック
		for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
			engine::Sprite* sprite = it->get();

			// ロックされていないスプライトのみ選択可能にする
			if (sprite && (!layoutDocument_ || sprite->IsVisible()) && !sprite->IsLocked() && IsMouseOver(sprite, localMousePos)) {
				selectedSprite_ = sprite;
				hit = true;
				break;
			}
		}

		if (!hit)
			selectedSprite_ = nullptr;
	}

#endif
}

// =========================================================================
// 大元の描画関数（これを呼べば3つのウィンドウがすべて描画される）
// =========================================================================
void SpriteDebugEditor::DrawImGui() {
#ifdef USE_IMGUI
    // Hierarchy・Inspector・Asset一覧をSprite編集用の独立パネルとして描きます。
	DrawHierarchyWindow();
	DrawInspectorWindow();
	DrawProjectWindow();
#endif
}

// =========================================================================
// 1. Hierarchy (左パネル) の描画
// =========================================================================
void SpriteDebugEditor::DrawHierarchyWindow() {
#ifdef USE_IMGUI
	if (!sceneManager_)
		return;
	BaseScene* currentScene = sceneManager_->GetCurrentScene();
	if (!currentScene)
		return;

	engine::SpriteCommon* spriteCommon = currentScene->GetSpriteCommon();
	if (!spriteCommon)
		return;
	ImGui::Begin(ICON_FA_LIST_UL " Sprite Hierarchy");
	DrawLayoutFileControls(*currentScene);
	const auto& sprites = GetEditingSprites(*currentScene);

	// --- ファイル管理 ---
	if (!layoutDocument_ && ImGui::CollapsingHeader(ICON_FA_SAVE " シーンのスプライト保存", ImGuiTreeNodeFlags_DefaultOpen)) {
		std::string directoryPath = "Resources/json/sprite/";

		if (!std::filesystem::exists(directoryPath)) {
			std::filesystem::create_directories(directoryPath);
		}

		// 一覧で選んだファイル名は保存先の候補に入れ、選択した瞬間には書き込まない。
		if (ImGui::BeginCombo(ICON_FA_HISTORY " 既存ファイル", currentSpriteFilename_)) {
			if (std::filesystem::exists(directoryPath)) {
				for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
					if (entry.path().extension() == ".json") {
						std::string fname = entry.path().filename().string();
						bool isSelected = (std::string(currentSpriteFilename_) == fname);

						if (ImGui::Selectable(fname.c_str(), isSelected)) {
							strcpy_s(currentSpriteFilename_, fname.c_str());
						}
					}
				}
			}

			ImGui::EndCombo();
		}

		ImGui::InputText(ICON_FA_FILE_SIGNATURE " 保存名 (.json)", currentSpriteFilename_, sizeof(currentSpriteFilename_));

		if (ImGui::Button(ICON_FA_DOWNLOAD " レイアウト保存 (Save)")) {
			std::string fullPath = directoryPath + std::string(currentSpriteFilename_);
			SaveSpriteLayout(fullPath);
		}
	}

	ImGui::Separator();
	ImGui::Spacing();

	// --- スプライトリスト ---
	ImGui::Text(ICON_FA_IMAGES " Sprite List");
	ImGui::BeginChild("SpriteListRegion", ImVec2(0, 0), true);


	if (sprites.empty()) {
		ImGui::TextDisabled(ICON_FA_INFO_CIRCLE " スプライトがありません");
	}

	for (const auto& spritePtr : sprites) {
		engine::Sprite* sprite = spritePtr.get();
		if (!sprite)
			continue;
		if (sprite->GetParent() != nullptr)
			continue;
		DrawSpriteNode(sprite);
	}

	// ドラッグ＆ドロップで新規作成
	// 画像のドロップは新規生成し、ツリー項目のドロップは既存 Sprite の親を外す。
	ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));

	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE_FILE")) {
			const char* droppedFilename = (const char*)payload->Data;
			auto newSprite = std::make_unique<engine::Sprite>();
			uint32_t handle = engine::Sprite::LoadTexture(droppedFilename);
			newSprite->Initialize(spriteCommon, handle);
			newSprite->SetName(droppedFilename);
			newSprite->SetTextureName(droppedFilename);
			selectedSprite_ = AddEditingSprite(*currentScene, std::move(newSprite));
		}

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE_NODE")) {
			engine::Sprite* sourceSprite = *(engine::Sprite**)payload->Data;

			if (sourceSprite) {
				sourceSprite->SetParent(nullptr);
			}
		}

		ImGui::EndDragDropTarget();
	}

	ImGui::EndChild();
	ImGui::End();
#endif
}

// =========================================================================
// 2. Inspector (右パネル) の描画
// =========================================================================
void SpriteDebugEditor::DrawSpriteNode(engine::Sprite* sprite) {
#ifdef USE_IMGUI
    // Sprite自身をImGui IDに使い、同名Spriteの表示・選択が衝突しないようにします。
	if (!sprite)
		return;

	ImGui::PushID(sprite);

	bool isVisible = sprite->IsVisible();
	const char* eyeIcon = isVisible ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_Text, isVisible ? ImVec4(1, 1, 1, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1));
	if (ImGui::Button(eyeIcon))
		sprite->SetVisible(!isVisible);
	ImGui::PopStyleColor(2);

	ImGui::SameLine();

	bool isLocked = sprite->IsLocked();
	const char* lockIcon = isLocked ? ICON_FA_LOCK : ICON_FA_UNLOCK;
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_Text, isLocked ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
	if (ImGui::Button(lockIcon))
		sprite->SetLocked(!isLocked);
	ImGui::PopStyleColor(2);

	ImGui::SameLine();

	std::string label = sprite->GetName().empty() ? "Sprite" : sprite->GetName();
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (selectedSprite_ == sprite)
		flags |= ImGuiTreeNodeFlags_Selected;
	if (sprite->GetChildren().empty())
		flags |= ImGuiTreeNodeFlags_Leaf;
	if (!isVisible)
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
	bool nodeOpen = ImGui::TreeNodeEx("##SpriteNode", flags, ICON_FA_IMAGE " %s", label.c_str());
	if (!isVisible)
		ImGui::PopStyleColor();

	if (ImGui::IsItemClicked()) {
		selectedSprite_ = sprite;
	}

	if (ImGui::BeginDragDropSource()) {
		engine::Sprite* payloadSprite = sprite;
		ImGui::SetDragDropPayload("SPRITE_NODE", &payloadSprite, sizeof(engine::Sprite*));
		ImGui::Text("Sprite: %s", label.c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE_NODE")) {
			engine::Sprite* sourceSprite = *(engine::Sprite**)payload->Data;

			if (sourceSprite && sourceSprite != sprite) {
				sourceSprite->SetParent(sprite);
			}
		}

		ImGui::EndDragDropTarget();
	}

	// 展開した行の直下に、そのSpriteを現在も親としている子だけを描く。
	if (nodeOpen) {
		for (engine::Sprite* child : sprite->GetChildren()) {
			if (child && child->GetParent() == sprite) {
				DrawSpriteNode(child);
			}
		}

		ImGui::TreePop();
	}

	ImGui::PopID();
#endif
}

bool SpriteDebugEditor::TryGetTexturePixelSize(engine::Sprite* sprite, engine::Vector2& outSize) const {
    // 画像の実ピクセル寸法を取得できないSpriteは縦横比調整から除きます。
	if (!sprite) {
		return false;
	}

	const uint32_t textureHandle = sprite->GetTextureHandle();

	if (textureHandle == 0) {
		return false;
	}

	const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(textureHandle);

	if (metadata.width == 0 || metadata.height == 0) {
		return false;
	}

	outSize = { static_cast<float>(metadata.width), static_cast<float>(metadata.height) };
	return true;
}

bool SpriteDebugEditor::IsGeneratedTextSprite(engine::Sprite* sprite) const {
    // 自動生成した文字PNGだけに一括補正を限定するため、参照パスで判別します。
	if (!sprite) {
		return false;
	}

	const std::string textureName = NormalizeSpritePath(sprite->GetTextureName());
	return textureName.find("generated/text/") != std::string::npos;
}

float SpriteDebugEditor::CalcAspectError(engine::Sprite* sprite, const engine::Vector2& textureSize) const {
    // 画像とSpriteの比率差を測り、補正対象を判定します。
	if (!sprite || textureSize.x <= 0.0f || textureSize.y <= 0.0f) {
		return 0.0f;
	}

	const engine::Vector2 size = sprite->GetSize();

	if (size.x <= 0.0f || size.y <= 0.0f) {
		return 0.0f;
	}

	const float textureAspect = textureSize.x / textureSize.y;
	const float spriteAspect = size.x / size.y;

	if (textureAspect <= 0.0f) {
		return 0.0f;
	}

	// 画像の縦横比で割った相対誤差を返す。たとえば0.1は本来の比率から10%ずれている。
	return std::fabs((spriteAspect / textureAspect) - 1.0f);
}

engine::Vector2 SpriteDebugEditor::MakeAspectFixedSize(engine::Sprite* sprite, const engine::Vector2& textureSize, bool keepWidth) const {
    // 幅か高さの選択軸を維持して、もう一方だけ画像比率へ合わせます。
	if (!sprite || textureSize.x <= 0.0f || textureSize.y <= 0.0f) {
		return { 0.0f, 0.0f };
	}

	const engine::Vector2 currentSize = sprite->GetSize();
	const float textureAspect = textureSize.x / textureSize.y;

	if (keepWidth) {
		return { currentSize.x, currentSize.x / textureAspect };
	}

	return { currentSize.y * textureAspect, currentSize.y };
}

engine::Vector2 SpriteDebugEditor::MakeNearestIntegerScaleSize(engine::Sprite* sprite, const engine::Vector2& textureSize) const {
    // 元画像との倍率を四分の一刻みへ丸め、細かすぎる縮小を避けます。
	if (!sprite || textureSize.x <= 0.0f || textureSize.y <= 0.0f) {
		return { 0.0f, 0.0f };
	}

	const engine::Vector2 currentSize = sprite->GetSize();
	// 横倍率と縦倍率が違う場合はその平均から、両軸に共通の倍率を求める。
	float scale = ((currentSize.x / textureSize.x) + (currentSize.y / textureSize.y)) * 0.5f;
	scale = std::round(scale * 4.0f) / 4.0f;
	scale = std::max(0.25f, scale);
	return { textureSize.x * scale, textureSize.y * scale };
}

int SpriteDebugEditor::FixSpriteAspectInCurrentScene(bool textOnly, bool keepWidth) {
    // 現行SceneのSpriteだけを走査し、文字PNG限定にも切り替えられるようにします。
	if (!sceneManager_) {
		return 0;
	}

	BaseScene* currentScene = sceneManager_->GetCurrentScene();

	if (!currentScene) {
		return 0;
	}

	int fixedCount = 0;

	for (const std::unique_ptr<engine::Sprite>& spritePtr : GetEditingSprites(*currentScene)) {
		engine::Sprite* sprite = spritePtr.get();

		if (!sprite) {
			continue;
		}

		if (textOnly && !IsGeneratedTextSprite(sprite)) {
			continue;
		}

		engine::Vector2 textureSize{};

		if (!TryGetTexturePixelSize(sprite, textureSize)) {
			continue;
		}

		// 画像比率との差が2%未満なら、既に合わせたレイアウトとしてサイズを維持する。
		if (CalcAspectError(sprite, textureSize) < kSpriteAspectWarningThreshold) {
			continue;
		}

		sprite->SetSize(MakeAspectFixedSize(sprite, textureSize, keepWidth));
		++fixedCount;
	}

	spriteQualityStatus_ = "比率補正: " + std::to_string(fixedCount) + "件";
	return fixedCount;
}

void SpriteDebugEditor::DrawInspectorWindow() {
#ifdef USE_IMGUI
	if (!sceneManager_)
		return;
	BaseScene* currentScene = sceneManager_->GetCurrentScene();
	if (!currentScene)
		return;

	ImGui::Begin(ICON_FA_INFO_CIRCLE " Sprite Inspector");

	if (ImGui::CollapsingHeader("Sprite比率クリーンアップ", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextDisabled("画像本来の縦横比に合わせて、引き伸ばしを補正します。");

		if (ImGui::Button("文字Spriteを横幅維持で一括補正", ImVec2(-1.0f, 0.0f))) {
			FixSpriteAspectInCurrentScene(true, true);
		}

		if (ImGui::Button("全Spriteを横幅維持で一括補正", ImVec2(-1.0f, 0.0f))) {
			FixSpriteAspectInCurrentScene(false, true);
		}

		if (ImGui::Button("全Spriteを高さ維持で一括補正", ImVec2(-1.0f, 0.0f))) {
			FixSpriteAspectInCurrentScene(false, false);
		}

		if (!spriteQualityStatus_.empty()) {
			ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.8f, 1.0f), "%s", spriteQualityStatus_.c_str());
		}

		ImGui::Separator();
	}

	if (selectedSprite_) {
		ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), ICON_FA_EDIT " 編集: %s", selectedSprite_->GetName().c_str());

        if (layoutDocument_ && ImGui::CollapsingHeader("動画レイヤー", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& document = *layoutDocument_;
            auto& row = document.originalSprites[selectedSprite_];
            const auto stored = SpriteLayoutScaler::ReadString(row, "video", "");
            char path[512]{};
            strncpy_s(path, stored.c_str(), _TRUNCATE);
            if (ImGui::InputText("動画 (Resources/video/)", path, sizeof(path))) {
                document.StopVideo();
                if (path[0])
                    row["video"] = path;
                else if (row.is_object())
                    row.erase("video");
            }
            if (ImGui::Button("動画をプレビュー")) {
                layoutFileStatus_ = document.StartVideo(*selectedSprite_, path)
                    ? "" : "Resources/video 内のMP4を指定してください。";
            }
            ImGui::SameLine();
            if (ImGui::Button("動画を停止"))
                document.StopVideo();
            ImGui::TextDisabled("準備中は指定した画像を表示します。");
        }

        if (layoutDocument_ && ImGui::CollapsingHeader("コマアニメーション", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& row = layoutDocument_->originalSprites[selectedSprite_];
            auto settings = SpriteLayoutAnimation::Read(row);
            const int previousFrameCount = settings.frameCount;
            bool changed = ImGui::SliderInt("横一列のコマ数", &settings.frameCount, 1, 128);
            changed |= ImGui::SliderFloat("FPS", &settings.fps, 1.0f, 60.0f, "%.1f");
            changed |= ImGui::Checkbox("ループ", &settings.loop);
            if (changed) {
                auto& animation = row["animation"];
                if (!animation.is_object())
                    animation = nlohmann::json::object();
                animation["frameCount"] = settings.frameCount;
                animation["fps"] = settings.fps;
                animation["loop"] = settings.loop;
                layoutDocument_->previewStart = std::chrono::steady_clock::now();

                // ドロップ直後の原寸サイズは1コマ分に合わせ、手動で調整した寸法は維持します。
                if (selectedSprite_->GetTextureHandle() != 0) {
                    const auto& texture = TextureManager::GetInstance()->GetMetadata(selectedSprite_->GetTextureHandle());
                    const float width = static_cast<float>(texture.width);
                    const float height = static_cast<float>(texture.height);
                    const bool canSplit = texture.width >= static_cast<size_t>(settings.frameCount) &&
                        texture.width % static_cast<size_t>(settings.frameCount) == 0;
                    const auto size = selectedSprite_->GetSize();
                    if (canSplit && std::abs(size.x - width / static_cast<float>(previousFrameCount)) < 0.01f &&
                        std::abs(size.y - height) < 0.01f) {
                        selectedSprite_->SetSize({width / static_cast<float>(settings.frameCount), height});
                    }
                    // 分割解除・設定途中では前に表示したコマのUVを残しません。
                    if (!canSplit || settings.frameCount == 1)
                        selectedSprite_->SetTextureRect({0.0f, 0.0f}, {width, height});
                }
            }
            if (ImGui::Button("先頭からプレビュー")) {
                layoutDocument_->previewStart = std::chrono::steady_clock::now();
                layoutDocument_->previewPlaying = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("停止"))
                layoutDocument_->previewPlaying = false;
        }

		// 描画順（Zオーダー）
		if (ImGui::Button(ICON_FA_ARROW_UP " 前面へ (手前)")) {
			MoveEditingSprite(*currentScene, selectedSprite_, 1);
		}

		ImGui::SameLine();

		if (ImGui::Button(ICON_FA_ARROW_DOWN " 背面へ (奥)")) {
			MoveEditingSprite(*currentScene, selectedSprite_, -1);
		}

		ImGui::Separator();

		// 名前編集
		char nameBuffer[256] = { 0 };
		strcpy_s(nameBuffer, selectedSprite_->GetName().c_str());

		if (ImGui::InputText(ICON_FA_PEN " 名前 (Name)", nameBuffer, sizeof(nameBuffer))) {
			selectedSprite_->SetName(nameBuffer);
		}

		if (selectedSprite_->GetParent()) {
			ImGui::TextDisabled(ICON_FA_SITEMAP " Parent: %s", selectedSprite_->GetParent()->GetName().c_str());

			if (ImGui::Button(ICON_FA_UNLINK " Unparent Sprite")) {
				selectedSprite_->SetParent(nullptr);
			}
		} else {
			ImGui::TextDisabled(ICON_FA_SITEMAP " Parent: None");
		}

		ImGui::Separator();

		// 基本パラメータ
		engine::Vector2 pos = selectedSprite_->GetPosition();
		engine::Vector2 size = selectedSprite_->GetSize();
		engine::Vector2 anchor = selectedSprite_->GetAnchorPoint();
		engine::Vector4 color = selectedSprite_->GetColor();
		float emissive = selectedSprite_->GetEmissive();
		if (ImGui::DragFloat2(ICON_FA_ARROWS_ALT " 座標 (Pos)", &pos.x, 1.0f))
			selectedSprite_->SetPosition(pos);
		if (ImGui::DragFloat2(ICON_FA_EXPAND_ARROWS_ALT " サイズ (Size)", &size.x, 1.0f))
			selectedSprite_->SetSize(size);
		engine::Vector2 texturePixelSize{};

		// 元画像の幅と高さを取得できた場合だけ、倍率と縦横比の補正を提示する。
		if (TryGetTexturePixelSize(selectedSprite_, texturePixelSize)) {
			const engine::Vector2 currentSize = selectedSprite_->GetSize();
			const float scaleX = texturePixelSize.x > 0.0f ? currentSize.x / texturePixelSize.x : 0.0f;
			const float scaleY = texturePixelSize.y > 0.0f ? currentSize.y / texturePixelSize.y : 0.0f;
			const float aspectError = CalcAspectError(selectedSprite_, texturePixelSize);
			ImGui::Text("Texture: %.0f x %.0f px", texturePixelSize.x, texturePixelSize.y);
			ImGui::Text("Display: %.1f x %.1f / Scale %.2f x %.2f", currentSize.x, currentSize.y, scaleX, scaleY);

			if (aspectError >= kSpriteAspectWarningThreshold) {
				ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "縦横比が %.1f%% ずれています", aspectError * 100.0f);
			}

			if (ImGui::Button("実画像サイズに合わせる")) {
				selectedSprite_->SetSize(texturePixelSize);
			}

			ImGui::SameLine();

			if (ImGui::Button("横幅維持で比率補正")) {
				selectedSprite_->SetSize(MakeAspectFixedSize(selectedSprite_, texturePixelSize, true));
			}

			if (ImGui::Button("高さ維持で比率補正")) {
				selectedSprite_->SetSize(MakeAspectFixedSize(selectedSprite_, texturePixelSize, false));
			}

			ImGui::SameLine();

			if (ImGui::Button("近い0.25倍率に補正")) {
				selectedSprite_->SetSize(MakeNearestIntegerScaleSize(selectedSprite_, texturePixelSize));
			}
		}

		if (ImGui::DragFloat2(ICON_FA_ANCHOR " アンカー (Anchor)", &anchor.x, 0.01f))
			selectedSprite_->SetAnchorPoint(anchor);
		if (ImGui::ColorEdit4(ICON_FA_PALETTE " 色 (Color)", &color.x))
			selectedSprite_->SetColor(color);
		if (ImGui::DragFloat(ICON_FA_SUN " Emissive (発光)", &emissive, 0.1f, 1.0f, 50.0f, "%.1f")) {
			selectedSprite_->SetEmissive(emissive);
		}

		ImGui::Separator();

		// クイック配置
		ImGui::Text(ICON_FA_CROSSHAIRS " クイック配置 (Quick Snap)");
		float gw = (float)WinApp::GetClientWidth();
		float gh = (float)WinApp::GetClientHeight();
		ImVec2 btnSz(30.0f, 30.0f);
		// 画面内の基準点と画像のアンカーを同じ割合に揃え、角や中央へ置く。
		auto quickSnapButton = [&](const char* label, float xRatio, float yRatio) {
			if (ImGui::Button(label, btnSz)) {
				selectedSprite_->SetPosition({ gw * xRatio, gh * yRatio });
				selectedSprite_->SetAnchorPoint({ xRatio, yRatio });
			}
			};

		quickSnapButton("↖", 0.0f, 0.0f);
		ImGui::SameLine();
		quickSnapButton("⬆", 0.5f, 0.0f);
		ImGui::SameLine();
		quickSnapButton("↗", 1.0f, 0.0f);

		quickSnapButton("⬅", 0.0f, 0.5f);
		ImGui::SameLine();
		quickSnapButton("・", 0.5f, 0.5f);
		ImGui::SameLine();
		quickSnapButton("➡", 1.0f, 0.5f);

		quickSnapButton("↙", 0.0f, 1.0f);
		ImGui::SameLine();
		quickSnapButton("⬇", 0.5f, 1.0f);
		ImGui::SameLine();
		quickSnapButton("↘", 1.0f, 1.0f);

		ImGui::Separator();

		// 画像差し替え
		ImGui::Text(ICON_FA_IMAGE " テクスチャ変更 (Texture Replace)");
		ImGui::Button("下から画像をここにドロップ！", ImVec2(-1, 30.0f));

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE_FILE")) {
				const char* droppedFilename = (const char*)payload->Data;
                if (layoutDocument_ && layoutDocument_->videoSprite == selectedSprite_)
                    layoutDocument_->StopVideo();
				uint32_t newHandle = engine::Sprite::LoadTexture(droppedFilename);
				// 描画用ハンドルと保存用の参照パスを同じ画像に更新する。
				selectedSprite_->SetTextureHandle(newHandle);
				selectedSprite_->SetTextureName(droppedFilename);
			}

			ImGui::EndDragDropTarget();
		}

		ImGui::Separator();
		ImGui::Dummy(ImVec2(0.0f, 5.0f));
		if (ImGui::Button(ICON_FA_TIMES_CIRCLE " 選択解除 (Deselect)", ImVec2(-1, 0)))
			selectedSprite_ = nullptr;

	}
	else {
		ImGui::TextDisabled(ICON_FA_MOUSE_POINTER " リストからスプライトを選択してください");
	}

	ImGui::End();
#endif
}

// =========================================================================
// 3. Project (下パネル / アセットブラウザ) の描画 [階層フォルダ対応・完全防弾版]
// =========================================================================
void SpriteDebugEditor::DrawProjectWindow() {
#ifdef USE_IMGUI
	ImGui::Begin(ICON_FA_FOLDER_OPEN " Sprite Assets");

	std::string baseDirPath = "Resources/sprite/";

	if (currentSpriteDirectory_ != baseDirPath) {
		if (ImGui::Button(ICON_FA_ARROW_UP " Back")) {

			fs::path p(currentSpriteDirectory_);
			currentSpriteDirectory_ = p.parent_path().parent_path().generic_string() + "/";

			// ルート（baseDirPath）より上に戻りすぎないように最終ガード
			if (currentSpriteDirectory_.length() < baseDirPath.length() ||
				currentSpriteDirectory_.find(baseDirPath) == std::string::npos) {
				currentSpriteDirectory_ = baseDirPath;
			}
		}

		ImGui::SameLine();
	}

	ImGui::TextDisabled("%s", currentSpriteDirectory_.c_str());
	ImGui::Separator();
	
	if (fs::exists(currentSpriteDirectory_)) {

		float thumbnailSize = 64.0f;
		float padding = 16.0f;
		float cellSize = thumbnailSize + padding;

		float panelWidth = ImGui::GetContentRegionAvail().x;
		// サムネイルと余白を一セルとして列数を決め、幅が狭くても最低一列を表示する。
		int columnCount = std::max(1, (int)(panelWidth / cellSize));

		if (ImGui::BeginTable("SpriteAssetTable", columnCount)) {

			for (const auto& entry : fs::directory_iterator(currentSpriteDirectory_)) {

				// ==========================================================
					// パターンA: フォルダ（ディレクトリ）の場合
					// ==========================================================
				if (entry.is_directory()) {
					ImGui::TableNextColumn();
					std::string folderName = entry.path().filename().string();

					ImGui::PushID(folderName.c_str());
					ImGui::BeginGroup();

					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));

					// 一時的に文字サイズを3倍にする
					ImGui::SetWindowFontScale(3.0f);

					if (ImGui::Button(ICON_FA_FOLDER, ImVec2(thumbnailSize, thumbnailSize))) {
						currentSpriteDirectory_ = entry.path().generic_string() + "/";
					}

					ImGui::SetWindowFontScale(1.0f); // 忘れずに元に戻す
					ImGui::PopStyleColor(4);

					// フォルダ名表示
					ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
					ImGui::TextWrapped("%s", folderName.c_str());
					ImGui::PopTextWrapPos();

					ImGui::EndGroup();
					ImGui::PopID();
				}

				// ==========================================================
				// パターンB: ファイル（.png 画像）の場合
				// ==========================================================
				else if (entry.is_regular_file() && entry.path().extension() == ".png") {
					std::error_code ec;

					if (fs::file_size(entry.path(), ec) == 0 || ec) {
						continue;
					}

					ImGui::TableNextColumn();
					std::string filename = entry.path().filename().string();

					// ドラッグ＆ドロップ（エンジンへの送信）用の相対パス
					std::string relativePath = fs::relative(entry.path(), baseDirPath).generic_string();
					std::string fullPath = entry.path().generic_string();

					ImGui::PushID(filename.c_str());

					uint32_t texHandle = TextureManager::GetInstance()->Load(fullPath);

					if (texHandle == 0) {
						ImGui::PopID();
						continue;
					}

					D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SrvManager::GetInstance()->GetGpuDescriptorHandle(texHandle);

					// --- アスペクト比維持の計算 ---
					const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetadata(texHandle);
					float w = (float)metadata.width;
					float h = (float)metadata.height;
					float drawW = thumbnailSize;
					float drawH = thumbnailSize;

					if (w > h) {
						drawH = thumbnailSize * (h / w);
					}
					else if (h > w) { drawW = thumbnailSize * (w / h); }
					float offsetX = (thumbnailSize - drawW) * 0.5f;
					float offsetY = (thumbnailSize - drawH) * 0.5f;

					ImGui::BeginGroup();

					if (offsetY > 0.0f)
						ImGui::Dummy(ImVec2(1.0f, offsetY));
					if (offsetX > 0.0f)
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);

					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
					ImGui::ImageButton(filename.c_str(), (ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(drawW, drawH));
					ImGui::PopStyleColor();

					// ドラッグ＆ドロップ処理
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
						// =======================================================
						//  裏のデータ（ペイロード）は relativePath に戻してクラッシュ回避！
						// =======================================================
						ImGui::SetDragDropPayload("SPRITE_FILE", relativePath.c_str(), relativePath.size() + 1);

						ImGui::Image((ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(drawW * 0.5f, drawH * 0.5f));
						ImGui::SameLine();
						ImGui::Text("Place: %s", filename.c_str());

						ImGui::EndDragDropSource();
					}

					if (offsetY > 0.0f)
						ImGui::Dummy(ImVec2(1.0f, offsetY));

				
					ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
					ImGui::TextWrapped("%s", filename.c_str());
					ImGui::PopTextWrapPos();

					ImGui::EndGroup();
					ImGui::PopID();
				}
			}

			ImGui::EndTable();
		}
	}
	else {
		// フォルダが見つからなかった時の警告色
		ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Directory Not Found: %s", currentSpriteDirectory_.c_str());
	}

	ImGui::End();
#endif
}


void SpriteDebugEditor::DrawLayoutFileControls(BaseScene& scene) {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("独立レイアウト", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (layoutDocument_) {
        const auto path = layoutDocument_->path.filename().u8string();
        ImGui::TextUnformatted(reinterpret_cast<const char*>(path.c_str()));
        if (ImGui::Button("独立レイアウトを保存"))
            SaveLayoutDocument();
        ImGui::SameLine();
        if (ImGui::Button("閉じる"))
            ImGui::OpenPopup("独立レイアウトを閉じる");
        if (ImGui::BeginPopupModal("独立レイアウトを閉じる", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("保存していない編集は失われます。");
            if (ImGui::Button("閉じてシーン編集へ")) {
                ClearSceneSelection();
                layoutDocument_.reset();
                layoutFileStatus_.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    } else {
        ImGui::InputText("JSONパス (Resources/json/)", layoutPathInput_, sizeof(layoutPathInput_));
        if (ImGui::Button("開く"))
            OpenLayoutDocument(scene.GetSpriteCommon(), false);
        ImGui::SameLine();
        if (ImGui::Button("新規レイアウト"))
            OpenLayoutDocument(scene.GetSpriteCommon(), true);
    }

    if (!layoutFileStatus_.empty())
        ImGui::TextWrapped("%s", layoutFileStatus_.c_str());
#else
    (void)scene;
#endif
}

void SpriteDebugEditor::OpenLayoutDocument(engine::SpriteCommon* common, bool createNew) {
    using nlohmann::json;
    if (!common)
        return;

    try {
        // プロジェクト内のJSONを開き、読み込みが完了してから編集対象を切り替えます。
        const std::u8string input(reinterpret_cast<const char8_t*>(layoutPathInput_));
        const fs::path root = fs::canonical("Resources/json");
        const fs::path relative(input);
        const fs::path target = fs::weakly_canonical(root / relative);
        const auto local = target.lexically_relative(root);
        if (relative.has_root_path() || local.empty() || local.is_absolute() ||
            *local.begin() == L".." || target.extension() != L".json" ||
            !fs::is_directory(target.parent_path())) {
            layoutFileStatus_ = "Resources/json 内の既存フォルダーと .json ファイル名を指定してください。";
            return;
        }

        auto document = std::make_unique<LayoutDocument>();
        document->path = target;
        document->isNew = createNew;
        if (createNew) {
            if (fs::exists(target)) {
                layoutFileStatus_ = "同名のファイルがあります。「開く」か別の名前を指定してください。";
                return;
            }
            document->source = {{"designResolution", {WinApp::GetClientWidth(), WinApp::GetClientHeight()}},
                                {"scaleToWindow", true}, {"sprites", json::array()}};
        } else {
            std::ifstream file(target);
            if (!(file >> document->source) || !document->source.is_object() ||
                !document->source.contains("sprites") || !document->source["sprites"].is_array()) {
                layoutFileStatus_ = "sprites 配列のあるレイアウトJSONを開いてください。";
                return;
            }
        }

        // 表示倍率は開いた時点で確定し、保存時に逆変換して設計ピクセルへ戻します。
        const auto scale = SpriteLayoutScaler::Make(document->source);
        if (scale.enabled && scale.ratio.x > 0.0f && scale.ratio.y > 0.0f)
            document->displayRatio = scale.ratio;
        std::unordered_map<std::string, engine::Sprite*> byName;
        for (const auto& row : document->source["sprites"]) {
            if (!row.is_object() || SpriteLayoutScaler::ReadString(row, "name", "").empty()) {
                layoutFileStatus_ = "名前がないスプライトが含まれているため、開けませんでした。";
                return;
            }
            auto sprite = SpriteLayoutFactory::Create(common, row, scale, "sprite");
            if (!byName.emplace(sprite->GetName(), sprite.get()).second) {
                layoutFileStatus_ = "同名のスプライトが複数あるため、開けませんでした。";
                return;
            }

            // 設定画面で使う切り抜きも未選択時のプレビューに適用します。
            const auto crop = row.find("textureCrop");
            if (crop != row.end() && crop->is_array() && crop->size() == 4 &&
                std::all_of(crop->begin(), crop->end(), [](const auto& value) { return value.is_number(); })) {
                sprite->SetTextureRect({(*crop)[0].get<float>(), (*crop)[1].get<float>()},
                                      {(*crop)[2].get<float>(), (*crop)[3].get<float>()});
            }
            sprite->SetRotation(SpriteLayoutScaler::ReadFloat(row, "rotation", 0.0f));
            sprite->SetVisible(true);
            document->originalSprites.emplace(sprite.get(), row);
            document->sprites.push_back(std::move(sprite));
        }

        // 名前で親子を結び、保存値は親からの相対位置として扱います。
        for (const auto& sprite : document->sprites) {
            const auto& row = document->originalSprites.at(sprite.get());
            const auto parentName = SpriteLayoutScaler::ReadString(row, "parentName", "");
            if (parentName.empty())
                continue;
            const auto parent = byName.find(parentName);
            if (parent == byName.end()) {
                layoutFileStatus_ = "参照先の親スプライトが見つかりません。";
                return;
            }
            sprite->SetParent(parent->second, false);
            if (sprite->GetParent() != parent->second) {
                layoutFileStatus_ = "親子関係が循環しているため、開けませんでした。";
                return;
            }
        }

        document->background = std::make_unique<engine::Sprite>();
        document->background->Initialize(common, engine::Sprite::LoadTexture("common/white.png"));
        document->background->SetColor({0.075f, 0.075f, 0.085f, 1.0f});
        document->background->SetAnchorPoint({0.0f, 0.0f});
        document->background->SetVisible(true);
        ClearSceneSelection();
        layoutDocument_ = std::move(document);
        layoutFileStatus_.clear();
    } catch (const std::exception&) {
        layoutFileStatus_ = "レイアウトJSONを読み込めませんでした。";
    }
}

void SpriteDebugEditor::SaveLayoutDocument() {
    using nlohmann::json;
    auto& document = *layoutDocument_;
    try {
        // 元ファイルが別のエディターから更新された場合は、現在の作業と一緒に保持します。
        if (document.isNew ? fs::exists(document.path) : !fs::is_regular_file(document.path)) {
            layoutFileStatus_ = "保存先の状態が変わりました。ファイルを確認してください。";
            return;
        }
        if (!document.isNew) {
            std::ifstream file(document.path);
            json latest;
            if (!(file >> latest) || latest != document.source) {
                layoutFileStatus_ = "ディスク上のJSONが変わっています。ファイルを確認してください。";
                return;
            }
        }

        // 元の行に編集値を重ね、選択画像など利用側の追加項目を保持します。
        json output = document.source;
        auto& rows = output["sprites"];
        rows = json::array();
        std::unordered_set<std::string> names;
        for (const auto& sprite : document.sprites) {
            if (sprite->GetName().empty() || !names.insert(sprite->GetName()).second) {
                layoutFileStatus_ = "各スプライトに重複しない名前を付けてください。";
                return;
            }
            const auto original = document.originalSprites.find(sprite.get());
            json row = original == document.originalSprites.end() ? json::object() : original->second;
            const auto ratio = document.displayRatio;
            const auto pos = sprite->GetPosition();
            const auto size = sprite->GetSize();
            const auto anchor = sprite->GetAnchorPoint();
            const auto color = sprite->GetColor();
            row["name"] = sprite->GetName();
            row["texture"] = sprite->GetTextureName();
            row["position"] = {pos.x / ratio.x, pos.y / ratio.y};
            row["size"] = {size.x / ratio.x, size.y / ratio.y};
            row["anchor"] = {anchor.x, anchor.y};
            row["color"] = {color.x, color.y, color.z, color.w};
            row["emissive"] = sprite->GetEmissive();
            row["parentName"] = sprite->GetParent() ? sprite->GetParent()->GetName() : "";
            rows.push_back(std::move(row));
        }

        // 完成したJSONだけを同じフォルダー内で置き換え、書き込み途中の内容を公開しません。
        const std::string serialized = output.dump(4) + '\n';
        wchar_t temporaryName[MAX_PATH]{};
        if (!GetTempFileNameW(document.path.parent_path().c_str(), L"spr", 0, temporaryName)) {
            layoutFileStatus_ = "保存用の一時ファイルを作れませんでした。";
            return;
        }
        const fs::path temporary(temporaryName);
        std::ofstream file(temporary, std::ios::binary);
        file << serialized;
        file.close();
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (document.isNew ? 0 : MOVEFILE_REPLACE_EXISTING);
        if (!file || !MoveFileExW(temporary.c_str(), document.path.c_str(), flags)) {
            std::error_code error;
            fs::remove(temporary, error);
            layoutFileStatus_ = "レイアウトJSONを保存できませんでした。";
            return;
        }
        document.source = std::move(output);
        document.originalSprites.clear();
        for (size_t i = 0; i < document.sprites.size(); ++i)
            document.originalSprites.emplace(document.sprites[i].get(), document.source["sprites"][i]);
        document.isNew = false;
        layoutFileStatus_ = "独立レイアウトを保存しました。";
    } catch (const std::exception&) {
        layoutFileStatus_ = "レイアウトJSONを保存できませんでした。";
    }
}

void SpriteDebugEditor::SaveSpriteLayout(const std::string& filename) {
    // 現在のSprite配置をScene別JSONへ書き出し、再読込時の位置と見た目を復元します。
	using json = nlohmann::json;

	if (sceneManager_ == nullptr)
		return;
	BaseScene* currentScene = sceneManager_->GetCurrentScene();
	if (currentScene == nullptr)
		return;

	// 未知の画面参照も残し、Editorで保存しても事前ロードの依存を失わないようにする。
	json root = json::object();
	{
		std::ifstream existing(filename);

		if (existing.is_open()) {
			try {
				existing >> root;
				// 既存ファイルの構造が想定外なら、他の設定を失う上書きを避ける。
				if (!root.is_object())
					return;
			} catch (const json::exception&) {
				return;
			}
		}
	}

	json spriteArray = json::array();

	auto& sprites = currentScene->GetSprites();

	for (const auto& spritePtr : sprites) {
		engine::Sprite* sprite = spritePtr.get();
		if (sprite == nullptr)
			continue;

		json spriteData;

		if (!sprite->GetName().empty()) {
			spriteData["name"] = sprite->GetName();
		}
		else {

			spriteData["name"] = "NoName_" + std::to_string((size_t)sprite);
		}

		engine::Vector2 pos = sprite->GetPosition();
		engine::Vector2 size = sprite->GetSize();
		engine::Vector2 anchor = sprite->GetAnchorPoint();
		engine::Vector4 color = sprite->GetColor();
		spriteData["position"] = { pos.x, pos.y };
		spriteData["size"] = { size.x, size.y };
		spriteData["anchor"] = { anchor.x, anchor.y };
		spriteData["color"] = { color.x, color.y, color.z, color.w };
		spriteData["texture"] = sprite->GetTextureName();
		spriteData["emissive"] = sprite->GetEmissive();
		spriteData["parentName"] = sprite->GetParent() ? sprite->GetParent()->GetName() : "";
		spriteArray.push_back(spriteData);
	}

	// 保存した配置のピクセル基準を併記し、読み込み時の拡縮に使えるようにする。
	root["designResolution"] = {
		static_cast<float>(WinApp::GetClientWidth()),
		static_cast<float>(WinApp::GetClientHeight())
	};

	root["scaleToWindow"] = true;
	root["sprites"] = spriteArray;

	std::ofstream file(filename);

	if (file.is_open()) {
		file << root.dump(4);
		file.close();
	}
}

bool SpriteDebugEditor::IsMouseOver(engine::Sprite* sprite, const engine::Vector2& localMousePos) const {
	if (sprite == nullptr)
		return false;

	engine::Vector2 pos = sprite->GetWorldPosition();
	engine::Vector2 size = sprite->GetSize();
	engine::Vector2 anchor = sprite->GetAnchorPoint();

	// スプライトの描画範囲を計算
	// 左上はワールドの描画位置からアンカーの割合ぶん引く。回転角を使わない軸平行矩形でクリックを判定する。
	float minX = pos.x - (anchor.x * size.x);
	float maxX = minX + size.x;
	float minY = pos.y - (anchor.y * size.y);
	float maxY = minY + size.y;

	return (localMousePos.x >= minX && localMousePos.x <= maxX &&
		localMousePos.y >= minY && localMousePos.y <= maxY);
}

bool SpriteDebugEditor::IsMouseBusy() const {
	// ImGui操作中かどうか
#ifdef USE_IMGUI
	return ImGui::GetIO().WantCaptureMouse;
#else
	return false;
#endif
}

// (Draw の実装)
void SpriteDebugEditor::Draw() {
#ifdef USE_IMGUI
	if (sceneManager_ == nullptr) {
		return;
	}

	BaseScene* currentScene = sceneManager_->GetCurrentScene();

	if (currentScene == nullptr) {
		return;
	}

	engine::SpriteCommon* currentSpriteCommon = currentScene->GetSpriteCommon();

	if (currentSpriteCommon == nullptr) {
		return;
	}

    // 編集用キャンバスはゲームUIの後に描き、実行中のシーンのSpriteリストとは独立させます。
    if (layoutDocument_) {
        auto& document = *layoutDocument_;
        if (document.videoSprite) {
            const auto now = std::chrono::steady_clock::now();
            document.videoPreview->Update(std::chrono::duration<double>(now - document.videoTick).count());
            document.videoTick = now;
            auto* commands = currentSpriteCommon->GetDxCommon()->GetCommandList();
            if (const auto frame = document.videoPreview->PrepareTextureForDraw(*commands)) {
                SpriteLayoutVideo::SetTexture(*document.videoSprite, *frame);
            } else if (document.videoPreview->HasError()) {
                document.StopVideo();
                layoutFileStatus_ = "動画を再生できませんでした。";
            }
        }
        auto& background = *layoutDocument_->background;
        background.SetSize({static_cast<float>(WinApp::GetClientWidth()), static_cast<float>(WinApp::GetClientHeight())});
        background.Update();
        background.Draw();
        const double seconds = layoutDocument_->previewPlaying
            ? std::chrono::duration<double>(std::chrono::steady_clock::now() - layoutDocument_->previewStart).count() : 0.0;
        for (const auto& sprite : layoutDocument_->sprites) {
            // 保存済みの設定も、その場で入力した設定も同じコマ選択処理へ渡します。
            const auto row = layoutDocument_->originalSprites.find(sprite.get());
            if (row != layoutDocument_->originalSprites.end() && sprite.get() != layoutDocument_->videoSprite)
                SpriteLayoutAnimation::Apply(*sprite, SpriteLayoutAnimation::Read(row->second), seconds);
            sprite->Update();
        }
        for (const auto& sprite : layoutDocument_->sprites)
            sprite->Draw();
    }

    if (!selectedSprite_ || ImGui::GetIO().WantCaptureMouse)
        return;

	if (gizmoArrowX_ == nullptr || initializedSpriteCommon_ != currentSpriteCommon) {

		// ギズモを「現在のシーンの Common」で再作成
		gizmoTextureHandle_ = engine::Sprite::LoadTexture("common/white.png");

		gizmoArrowX_ = std::make_unique<engine::Sprite>();
		gizmoArrowX_->Initialize(currentSpriteCommon, gizmoTextureHandle_);
		gizmoArrowX_->SetSize({ 50.0f, 10.0f });
		gizmoArrowX_->SetColor({ 1.0f, 0.0f, 0.0f, 0.8f });
		gizmoArrowX_->SetAnchorPoint({ 0.0f, 0.5f });

		gizmoArrowY_ = std::make_unique<engine::Sprite>();
		gizmoArrowY_->Initialize(currentSpriteCommon, gizmoTextureHandle_);
		gizmoArrowY_->SetSize({ 10.0f, 50.0f });
		gizmoArrowY_->SetColor({ 0.0f, 1.0f, 0.0f, 0.8f });
		gizmoArrowY_->SetAnchorPoint({ 0.5f, 0.0f });

		// 初期化に使用した Common を記憶
		initializedSpriteCommon_ = currentSpriteCommon;
	}

	// --- ギズモの描画 ---
	// 親付き Sprite でも選択マーカーの基準はローカル位置にせず、表示上のワールド位置へ重ねる。
	engine::Vector2 pos = selectedSprite_->GetWorldPosition();
	gizmoArrowX_->SetPosition(pos);
	gizmoArrowY_->SetPosition(pos);

	// 位置変更後に二本のSpriteの行列を確定してから描画する。
	gizmoArrowX_->Update();
	gizmoArrowY_->Update();

	gizmoArrowX_->Draw();
	gizmoArrowY_->Draw();
#endif
}
