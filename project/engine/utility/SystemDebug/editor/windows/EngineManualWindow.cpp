#include "EngineManualWindow.h"

#include "IconsFontAwesome5.h"
#include "imgui.h"

#ifdef USE_IMGUI
namespace {

void SectionTitle(const char* title) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.42f, 0.82f, 1.0f, 1.0f), "%s", title);
    ImGui::Separator();
}

void Paragraph(const char* text) {
    ImGui::TextWrapped("%s", text);
}

void ManualBullet(const char* text) {
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text);
}

void ManualNote(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.88f, 0.45f, 1.0f));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void AddShortcutRow(const char* key, const char* action) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.28f, 1.0f), "%s", key);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s", action);
}

void DrawShortcutTable() {
    if (!ImGui::BeginTable("EngineManualShortcutTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        return;
    }

    ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("内容");
    ImGui::TableHeadersRow();

    AddShortcutRow("F", "選択中の3Dオブジェクトへカメラをフォーカスします。");
    AddShortcutRow("T / R / S", "移動、回転、スケールのギズモを切り替えます。");
    AddShortcutRow("Delete", "選択中のオブジェクトを削除します。");
    AddShortcutRow("Ctrl + C", "選択中のオブジェクトを複製します。");
    AddShortcutRow("Ctrl + Z / Ctrl + Y", "Undo / Redoを実行します。");
    AddShortcutRow("End", "選択中のオブジェクトを床へ落とします。");
    AddShortcutRow("Tab", "Game View上の作成パレットを開きます。");
    AddShortcutRow("左クリック", "配置プレビューやブラシ配置を確定します。");
    AddShortcutRow("右クリック / E", "配置プレビューやブラシ配置をキャンセルします。");
    AddShortcutRow("ドラッグ&ドロップ", "Project、Preset、ParticleなどをGame Viewへ配置します。");

    ImGui::EndTable();
}

void AddMaterialRow(const char* type, const char* name, const char* use) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(type);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s", name);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextWrapped("%s", use);
}

void DrawMaterialTable() {
    if (!ImGui::BeginTable("EngineManualMaterialTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        return;
    }

    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthFixed, 170.0f);
    ImGui::TableSetupColumn("用途");
    ImGui::TableHeadersRow();

    AddMaterialRow("0", "通常 (Standard)", "標準のPBR描画です。PBR繰り返し設定で大きい床の伸びを抑えられます。");
    AddMaterialRow("1", "ガラス (Glass)", "透明、反射、ひび割れ表現の確認に使います。");
    AddMaterialRow("2", "氷・宝石 (Ice/Crystal)", "氷床や透明感のある結晶系の見た目に使います。");
    AddMaterialRow("3", "ホログラム (Hologram)", "半透明の演出、ガイド、仮表示に使います。");
    AddMaterialRow("4", "消滅 (Dissolve)", "敵撃破やオブジェクト消滅などの演出に使います。");
    AddMaterialRow("5", "旧マグマ (Emissive)", "旧式の発光マグマです。互換用として残しています。");
    AddMaterialRow("6", "トゥーン調 (Cel Shaded)", "アウトラインや階調を強めたいモデルに使います。");
    AddMaterialRow("7", "ローカルフォグ (Local Fog)", "限定範囲の霧、靄、空間演出に使います。");
    AddMaterialRow("8", "水 (Water)", "水面用の専用描画です。波、流れ、透明感を調整できます。");
    AddMaterialRow("9", "新マグマ (Magma)", "アニメ調のマグマ面に使います。");
    AddMaterialRow("10", "分厚い氷 (Ice)", "厚みのある氷ブロックや氷壁に使います。");
    AddMaterialRow("11", "炎 (Fire)", "炎の形、炎の球をタイプ切り替えで確認できます。");
    AddMaterialRow("12", "レーザー (Laser)", "レーザー接続ノードやビーム表現に使います。");
    AddMaterialRow("13", "スライムジェル (Slime Gel)", "スライムらしい柔らかい質感の確認用です。");
    AddMaterialRow("14", "地面衝撃波 (Shockwave)", "叩きつけ、爆発、着地などの円形衝撃波に使います。");
    AddMaterialRow("15", "水/マグマ接触", "泡、蒸気など液体接触の演出に使います。");
    AddMaterialRow("16", "ダメージ亀裂", "爆弾で壊せるブロックやガラスのひび割れに使います。");
    AddMaterialRow("17", "上昇気流", "風柱、渦リング、横風スラッシュなどの演出に使います。");
    AddMaterialRow("18", "スタン拘束", "スタン中の軽いビリビリや拘束リングに使います。");
    AddMaterialRow("19", "王冠解放", "ステージ解放、ゲート解放、王冠演出に使います。");
    AddMaterialRow("20", "毒胞子", "キノコ敵や毒霧、胞子雲に使います。");
    AddMaterialRow("21", "雲 (Cloud)", "雲、柔らかい煙、ふわっとした床演出に使います。");
    AddMaterialRow("22", "ゲートポータル", "ステージゲートの渦、入口、転送表現に使います。");
    AddMaterialRow("23", "アニメ調地形", "地形をPBR寄りではなく、色を塗ったアニメ調に寄せるためのシェーダーです。");
    AddMaterialRow("24", "ダッシュパネル", "流れるラインを持つダッシュパネル専用の見た目です。");

    ImGui::EndTable();
}

} // namespace
#endif

void EngineManualWindow::Draw() {
#ifdef USE_IMGUI
    if (!isOpen_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(980.0f, 680.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(ICON_FA_BOOK " エンジン説明書 (Engine Manual)", &isOpen_)) {
        ImGui::End();
        return;
    }

    const char* topics[] = {
        ICON_FA_INFO_CIRCLE " はじめに",
        ICON_FA_KEYBOARD " 画面構成と基本操作",
        ICON_FA_CUBES " 配置と編集",
        ICON_FA_CUBE " Hierarchy / Scene保存",
        ICON_FA_IMAGE " Inspector",
        ICON_FA_IMAGES " Project / Preset",
        ICON_FA_IMAGES " Sprite Editor",
        ICON_FA_VIDEO " Camera / Light / PostEffect",
        ICON_FA_FIRE " Particle / GPU Particle",
        ICON_FA_MAGIC " Mesh / Debris / Trail / VFX",
        ICON_FA_STAR " Preview / Validator",
        ICON_FA_FILM " Animation / Ghost",
        ICON_FA_CODE " Text / 生成ツール",
        ICON_FA_CUBES " 最適化 / 監査ツール",
        ICON_FA_GAMEPAD " Game Data Debug",
        ICON_FA_BOLT " 実行・保存・注意点"
    };

    ImGui::BeginChild("EngineManualTopicList", ImVec2(250.0f, 0.0f), true);
    for (int i = 0; i < IM_ARRAYSIZE(topics); ++i) {
        if (ImGui::Selectable(topics[i], selectedIndex_ == i)) {
            selectedIndex_ = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("EngineManualBody", ImVec2(0.0f, 0.0f), true);

    switch (selectedIndex_) {
    case 0:
        SectionTitle("この説明書について");
        Paragraph("GE3 Editorでステージ、UI、エフェクト、シェーダー、ゲーム進行データを編集するための説明書です。現在のEditor構成に合わせて、追加されたツールや既存機能の使い方をまとめています。");
        ManualBullet("基本的には左のEditorメニューから機能を開き、Game View、Hierarchy、Inspector、Projectを組み合わせて作業します。");
        ManualBullet("配置やUI調整は、可能な限りJSONに保存されるEditor機能を使うと後から調整しやすくなります。");
        ManualBullet("保存前にScene File、Save Preview、未保存表示を確認してください。特にObject、Enemy、Playerは個別保存と全体保存があります。");
        ManualNote("注意: 作業中のブランチやシーンJSONが違うと、表示や保存先も変わります。修正前に現在のScene File名を必ず確認してください。");
        break;

    case 1:
        SectionTitle("画面構成");
        ManualBullet("Game View: 3D配置、ギズモ操作、ドラッグ&ドロップ配置、プレビュー確認を行う中心画面です。");
        ManualBullet("Hierarchy: 3Dオブジェクトの選択、親子関係、表示、ロック、複製、作成を行います。");
        ManualBullet("Inspector: 選択中オブジェクトのTransform、Collision、Material、Gimmick、Enemy、Particleなどを編集します。");
        ManualBullet("Project: 3DModel、Preset、Sprite、VFXなどのアセットを探してGame Viewへ配置します。");
        SectionTitle("ショートカット");
        DrawShortcutTable();
        break;

    case 2:
        SectionTitle("3Dオブジェクトの配置");
        ManualBullet("Projectのモデル、Preset、GPU Particle、Mesh EffectをGame Viewへドラッグすると配置プレビューが出ます。");
        ManualBullet("Tabキー、またはGame Viewの作成パレットから、ギミック、敵、アイテム、透明ボックス、演出用カメラなどを作成できます。");
        ManualBullet("プレビュー中は接地位置、向き、スナップを確認してから左クリックで確定します。右クリックまたはEでキャンセルできます。");
        ManualBullet("Preset Brushを使うと、コインなど同じプリセットを一定間隔で連続配置できます。");
        SectionTitle("主な作成カテゴリ");
        ManualBullet("基本: Stage Block、Cube、Sphere、Cylinder、Trigger Box、Collision Box、Cinematic Camera。");
        ManualBullet("ギミック: MovingFloor、Trampoline、SinkingFloor、SeesawFloor、DashPanel、IceFloor、TimedSwitch、AppearingFloor、HookAnchor、HookPullBlock、OneWayFloor、LiquidLevel、ChainCollapseFloor、RotatingFloor、RotatingPillar、PhaseFlipFloor、LaserNode、StageGateなど。");
        ManualBullet("敵: Slime、Bomb、Bomber、Mushroom、GiantSlime、Bat、BeamDrone、BossCore。");
        ManualBullet("アイテム: Healなど。");
        break;

    case 3:
        SectionTitle("Hierarchy");
        ManualBullet("オブジェクト名をクリックして選択します。目アイコンで表示、ロックアイコンで編集ロックを切り替えます。");
        ManualBullet("親子関係はHierarchy上のドラッグ&ドロップで変更できます。Preset化する場合も親子構造を含められます。");
        ManualBullet("選択中オブジェクトはInspectorで名前、モデル、保存カテゴリ、Transformなどを編集できます。");
        SectionTitle("Scene File");
        ManualBullet("Object、Enemy、Playerを個別保存できます。全体保存はScene全体の現在状態を保存します。");
        ManualBullet("Save Previewでは保存前に追加、変更、削除の差分を確認できます。");
        ManualBullet("Scene ValidatorではID重複、参照切れ、未設定コリジョンなどを検査できます。");
        ManualNote("未保存表示が出ている時は、現在の変更がJSONへ反映されていません。ブランチ切り替え前や再起動前に保存してください。");
        break;

    case 4:
        SectionTitle("Inspector");
        ManualBullet("名前、クラス、保存カテゴリ、親子関係、モデルアセット、表示、Transform、Collision、Materialを編集できます。");
        ManualBullet("Collisionは形状タイプ、中心オフセット、サイズ、回転を調整できます。不要な当たり判定はなしにして、処理対象から外してください。");
        ManualBullet("Materialではマテリアルタイプ、PBRテクスチャ繰り返し、Normal Map、IBL、Blend Mode、Color、Emissiveを調整できます。");
        ManualBullet("Gimmick、Enemy、Itemごとの専用パラメータは、選択中のクラスに応じて下部に表示されます。");
        SectionTitle("Material Type");
        DrawMaterialTable();
        break;

    case 5:
        SectionTitle("Project");
        ManualBullet("Resources/3DModel以下のフォルダとモデルをサムネイル付きで確認できます。LODモデルはバッジで区別されます。");
        ManualBullet("モデルをGame Viewへドラッグすると、そのモデルを使ったObjectとして配置できます。");
        ManualBullet("GPU ParticleやMesh EffectのJSONもProjectからドラッグして配置、確認できます。");
        SectionTitle("Preset");
        ManualBullet("Presetは配置用テンプレートです。モデル、スケール、ギミック種別、敵種別、親子関係などをまとめて配置できます。");
        ManualBullet("名前に / を入れるとフォルダ分けできます。例: Gimmick/CoinLine。");
        ManualBullet("選択中オブジェクトからPreset作成、サムネイル更新、上書き、削除ができます。");
        ManualBullet("作成パレットとPresetは連携しているため、よく使う配置はPreset化しておくと作業が速くなります。");
        break;

    case 6:
        SectionTitle("Sprite Editor");
        ManualBullet("Sprite HierarchyとSprite Inspectorで、UIをJSONレイアウトとして編集できます。");
        ManualBullet("Sprite Assetsから画像をドラッグして配置し、位置、サイズ、色、アンカー、表示、ロック、親子関係を調整できます。");
        ManualBullet("親子関係を使うと、複数UIをまとめて動かせます。ゲームシーン、タイトル、ゲームオーバー、設定画面などのUI調整に使います。");
        ManualBullet("選択中UIは色変更、スケール変更、点滅などの演出と組み合わせて使えます。");
        ManualNote("UI位置をコード固定にすると後調整が重くなります。可能な画面はSprite EditorのJSON管理へ寄せてください。");
        break;

    case 7:
        SectionTitle("Camera");
        ManualBullet("Camera EditorではEditor用カメラ、ゲーム用カメラ、フォーカス、演出用カメラの確認を行います。");
        ManualBullet("Cinematic Cameraはシーン内に配置でき、ムービーや演出用のカメラワークに使えます。");
        SectionTitle("Light");
        ManualBullet("Directional、Point、Spot、Fog、God Rays、Environment Map、IBLなどを調整できます。");
        ManualBullet("GameOverなど特殊シーンではClear Colorやライトを暗くして、シーンの雰囲気を作れます。");
        SectionTitle("Post Effect");
        ManualBullet("Bloom、Tone Mapping、Blur、Outline、Dissolve、Iris Fade、Slime Fadeなどを調整できます。");
        ManualBullet("落下演出、被弾、ゲームオーバー、ステージ遷移などはPost EffectとSprite Fadeを組み合わせて使います。");
        break;

    case 8:
        SectionTitle("通常Particle");
        ManualBullet("Texture、Shape、Physics、Visual、Size Graphなどを調整して、軽い演出用のパーティクルを作成できます。");
        ManualBullet("保存したJSONはProjectから配置できます。");
        SectionTitle("GPU Particle");
        ManualBullet("大量の粒子や派手な演出向けです。Emission、Collision、Environment、Quick Preset、Preview環境を使って調整します。");
        ManualBullet("爆発、泡、火花、煙、収集演出など、ゲーム中に多く出る演出はGPU Particleの利用を検討してください。");
        break;

    case 9:
        SectionTitle("Mesh Effect");
        ManualBullet("メッシュ形状を使う演出です。炎、衝撃波、ゲート、レーザー、スタンなど、シェーダーと組み合わせる演出に使います。");
        ManualBullet("Effect Preview Stageで隔離空間に出して確認できます。");
        SectionTitle("Debris Effect");
        ManualBullet("爆発時の破片、岩、欠片の飛散など、3D破片を使った演出を作るためのEditorです。");
        SectionTitle("Trail / VFX Sequencer");
        ManualBullet("Trail Emitterは対象オブジェクトの移動に追従する軌跡演出を作ります。");
        ManualBullet("VFX SequencerはParticle、Mesh Effect、SE、カメラ揺れ、画面効果を時間軸でまとめるためのEditorです。");
        break;

    case 10:
        SectionTitle("Effect Preview Stage");
        ManualBullet("エフェクト専用の隔離空間です。背景、床、ライト、カメラ距離、再生速度、ループ確認をまとめて行えます。");
        ManualBullet("ゲームシーン上の確認より、見た目の調整と比較がしやすくなります。");
        SectionTitle("Material Preview Board");
        ManualBullet("Material Typeごとの見た目を並べて確認できます。炎の形、炎の球、ゲート、雲など、タイプ別の確認にも対応しています。");
        SectionTitle("Scene Validator / Event Link Graph");
        ManualBullet("Scene Validatorはシーンの危険な設定を検査します。");
        ManualBullet("Event Link GraphはEvent IDとTarget IDの接続、重複、未接続を確認するためのツールです。");
        break;

    case 11:
        SectionTitle("Animation Workbench");
        ManualBullet("敵やモデルの待機、移動、攻撃、被弾などをAIから切り離して確認するための作業台です。");
        ManualBullet("モデル、アニメーション、タイムライン、ボーン編集、キーフレーム、イベントマーカーを確認できます。");
        ManualBullet("ボーン付きモデルの確認では、ゲーム本体のAI挙動に影響を出さずに動きを詰められます。");
        SectionTitle("Ghost Recorder / Ghost Director");
        ManualBullet("Ghost Recorderはカメラやオブジェクトの動きを記録します。");
        ManualBullet("Ghost Directorは記録した動きや演出を並べて、簡易ムービーやイベント演出を組み立てるために使います。");
        break;

    case 12:
        SectionTitle("Text PNG");
        ManualBullet("任意の文字列をPNGとして生成します。フォント、サイズ、余白、アウトライン、色を調整できます。");
        ManualBullet("生成後は通知が出るため、出力完了を確認できます。操作UI、タイトルロゴ、ゲームオーバー文字などに使います。");
        SectionTitle("Text 3D");
        ManualBullet("Blenderのテキスト機能に近い感覚で、3Dテキストモデルを生成するためのツールです。");
        ManualBullet("看板、ステージ名、タイトル演出など、2Dではなく空間上に置く文字に向いています。");
        SectionTitle("Shader Texture Baker");
        ManualBullet("水、炎、ゲートなどのシェーダーで使うノイズ、マスク、模様テクスチャを外部ツールでDDS生成します。");
        ManualBullet("生成したDDSは対応シェーダーから参照できるため、コードだけでは調整しにくい模様作りを短縮できます。");
        break;

    case 13:
        SectionTitle("DDS Cache Builder");
        ManualBullet("重いPNGをDDSへ変換し、起動時のテクスチャ変換負荷を減らすための外部ツールです。");
        ManualBullet("完了時は画面通知で確認できます。エンジン起動中の作業を止めないことを重視しています。");
        SectionTitle("Model Optimizer");
        ManualBullet("外部ツールでLOD用の軽量モデルを生成し、Editor上で比較してから採用できます。");
        ManualBullet("生成後にプレビューし、問題なければ適用、違和感があれば破棄できます。");
        ManualBullet("カメラ距離に応じたLOD切り替え距離をInspectorから調整できます。");
        ManualNote("ボーンアニメーション付きモデルは自動軽量化で崩れる可能性が高いため、基本的に対象外にしてください。");
        SectionTitle("Asset Audit");
        ManualBullet("重いアセット、未使用アセット、参照切れを調べる監査ツールです。");
        ManualBullet("未使用アセットは確認してから削除または退避できます。削除前に参照先を必ず確認してください。");
        break;

    case 14:
        SectionTitle("Game Data Debug");
        ManualBullet("タイトル、ステージセレクト、ゲーム進行の検証用に、セーブデータを直接編集できます。");
        ManualBullet("スロット1から3、残機、コイン、プレイ時間、チュートリアル完了、ステージ解放、クリア済み、スター取得、王冠数などを確認できます。");
        ManualBullet("ステージセレクトのゲート解放やクリア済み王冠表示の確認に使います。");
        ManualNote("デバッグ用Editorなので、提出用データを壊さないように編集対象スロットを確認してから変更してください。");
        break;

    case 15:
        SectionTitle("実行とシーン切り替え");
        ManualBullet("Debug / DevelopmentではUSE_IMGUI付きのEditor機能を使います。Releaseでは基本的にEditorを表示しません。");
        ManualBullet("シーン切り替え時は現在のScene Fileと保存先JSONを確認してください。Editorで開いているシーンとゲーム開始シーンが違う場合があります。");
        ManualBullet("Preview SceneはUIや挙動確認に使えます。ゲーム中UI、残機、コイン表示も確認対象です。");
        SectionTitle("保存時の注意");
        ManualBullet("Object、Enemy、Playerの個別保存とAll保存を使い分けてください。");
        ManualBullet("差分が不安なときはSave Previewを先に開いて、意図しない削除や上書きがないか確認します。");
        ManualBullet("生成ツールで作った画像やモデルはResources配下に保存し、必要ならPresetやSprite JSONに登録してください。");
        ManualNote("エディタ上で見えていても、JSONに保存されていない変更は再起動やブランチ切り替えで失われます。");
        break;

    default:
        selectedIndex_ = 0;
        break;
    }

    ImGui::EndChild();
    ImGui::End();
#endif
}
