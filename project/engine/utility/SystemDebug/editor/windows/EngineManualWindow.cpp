#include "EngineManualWindow.h"

#include "IconsFontAwesome5.h"
#include "imgui.h"

#include <cctype>
#include <string>
#include <vector>

#ifdef USE_IMGUI
namespace {

enum class ManualSpecial {
    None,
    Shortcuts,
    Materials,
};

struct ManualPage {
    const char* category;
    const char* title;
    const char* aliases;
    const char* purpose;
    const char* openGuide;
    std::vector<const char*> steps;
    const char* saveGuide;
    std::vector<const char*> checks;
    const char* caution;
    ManualSpecial special = ManualSpecial::None;
};

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

void ManualStep(int index, const char* text) {
    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.28f, 1.0f), "%d.", index);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text);
}

void ManualNote(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.28f, 0.22f, 0.08f, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.88f, 0.45f, 1.0f));
    ImGui::BeginChild("ManualCaution", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::TextWrapped(ICON_FA_EXCLAMATION_TRIANGLE " 注意: %s", text);
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
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

    ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("内容");
    ImGui::TableHeadersRow();

    AddShortcutRow("F", "選択中の3DオブジェクトへEditorカメラをフォーカスします。");
    AddShortcutRow("T / R / S", "移動、回転、スケールのImGuizmoを切り替えます。");
    AddShortcutRow("Delete", "選択中の3Dオブジェクトを削除します。ロック中の対象は先にロックを解除します。");
    AddShortcutRow("Ctrl + C", "選択中の3Dオブジェクトを複製します。");
    AddShortcutRow("Ctrl + Z / Ctrl + Y", "Inspector、ImGuizmo、Ghost Recorderを含むEditor操作を共通履歴でUndo / Redoします。保存済みJSONそのものは巻き戻しません。");
    AddShortcutRow("End", "選択中のオブジェクトを床へ落とします。コリジョン形状と接地位置を確認してください。");
    AddShortcutRow("Tab", "Game View上の作成パレットを開閉します。");
    AddShortcutRow("左クリック", "配置プレビュー、ブラシ配置、Game View上の選択を確定します。");
    AddShortcutRow("Shift + 左クリック", "Hierarchyなど対応箇所で複数選択を追加・解除します。");
    AddShortcutRow("右クリック / E", "配置プレビューやブラシ配置をキャンセルします。");
    AddShortcutRow("右ドラッグ + WASD", "Editorカメラを自由移動します。Q/Eで上下、Shiftで高速移動します。");
    AddShortcutRow("F10", "ポートフォリオ撮影モードを切り替えます。");
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
    ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableSetupColumn("用途");
    ImGui::TableHeadersRow();

    AddMaterialRow("0", "通常 (Standard)", "標準のPBR描画です。床などはPBR繰り返し設定でテクスチャの伸びを抑えます。");
    AddMaterialRow("1", "ガラス (Glass)", "透明、反射、ひび割れ表現に使います。");
    AddMaterialRow("2", "氷・宝石 (Ice/Crystal)", "氷床や透明感のある結晶系の見た目に使います。");
    AddMaterialRow("3", "ホログラム (Hologram)", "半透明の演出、ガイド、仮表示に使います。");
    AddMaterialRow("4", "消滅 (Dissolve)", "敵撃破やオブジェクト消滅などの演出に使います。");
    AddMaterialRow("5", "旧マグマ (Emissive)", "旧式の発光マグマです。互換用として残しています。");
    AddMaterialRow("6", "トゥーン調 (Cel Shaded)", "アウトラインや階調を強めたいモデルに使います。");
    AddMaterialRow("7", "ローカルフォグ (Local Fog)", "限定範囲の霧、靄、空間演出に使います。");
    AddMaterialRow("8", "水 (Water)", "水面用の専用描画です。波、流れ、透明感を調整できます。");
    AddMaterialRow("9", "新マグマ (Magma)", "アニメ調のマグマ面に使います。");
    AddMaterialRow("10", "分厚い氷 (Ice)", "厚みのある氷ブロックや氷壁に使います。");
    AddMaterialRow("11", "炎 (Fire)", "炎の形や炎の球をタイプ切り替えで確認できます。");
    AddMaterialRow("12", "レーザー (Laser)", "レーザー接続ノードやビーム表現に使います。");
    AddMaterialRow("13", "スライムジェル (Slime Gel)", "スライムらしい柔らかい質感の確認用です。");
    AddMaterialRow("14", "地面衝撃波 (Shockwave)", "叩きつけ、爆発、着地などの円形衝撃波に使います。");
    AddMaterialRow("15", "水/マグマ接触", "泡、蒸気など液体接触の演出に使います。");
    AddMaterialRow("16", "ダメージ亀裂", "爆弾で壊せるブロックやガラスのひび割れに使います。");
    AddMaterialRow("17", "上昇気流", "風柱、渦リング、横風スラッシュなどの演出に使います。");
    AddMaterialRow("18", "スタン拘束", "スタン中の軽い放電や拘束リングに使います。");
    AddMaterialRow("19", "王冠解放", "ステージ解放、ゲート解放、王冠演出に使います。");
    AddMaterialRow("20", "毒胞子", "キノコ敵や毒霧、胞子雲に使います。");
    AddMaterialRow("21", "雲 (Cloud)", "雲、柔らかい煙、ふわっとした床演出に使います。");
    AddMaterialRow("22", "ゲートポータル", "ステージゲートの渦、入口、転送表現に使います。");
    AddMaterialRow("23", "アニメ調地形", "地形をPBR寄りではなく、色を塗ったアニメ調に寄せます。");
    AddMaterialRow("24", "ダッシュパネル", "流れるラインを持つダッシュパネル専用の見た目です。");

    ImGui::EndTable();
}

const std::vector<ManualPage>& GetManualPages() {
    static const std::vector<ManualPage> pages = {
        {
            "00 基本",
            "はじめに / 安全な作業手順",
            "Engine Manual 説明書 初心者 保存",
            "この説明書は、現在のGE3 Editorに実装されている各画面を、実作業で迷わない粒度で確認するためのものです。名称だけでなく、操作順、保存先、確認項目、壊しやすい点をまとめています。",
            "上部メニューの「ヘルプ」から「エンジン説明書」を選びます。左の検索欄では日本語名、英語名、用途、保存先の一部で絞り込めます。",
            {
                "HierarchyのScene Asset欄でActive Sceneを確認し、今から編集するステージと一致しているか確認します。",
                "目的のEditorをHierarchy最上部のシステム一覧から開き、この説明書の同名ページを横に置いて作業します。",
                "変更後はEditor内のプレビュー、Game View、実行中のゲームの順で確認します。見た目だけでなく当たり判定、追従、ループ終了も確認します。",
                "保存ボタンを押し、Hierarchyの未保存表示が消えたことを確認します。必要ならSave PreviewとScene Validatorも実行します。",
            },
            "シーンはResources/json/3Dobject、専用Editorの設定は各ページ記載のResources/json配下へ保存されます。Editor上で見えているだけでは保存完了とは限りません。",
            {
                "編集対象のシーン名と保存カテゴリが正しい。",
                "再読み込み後も同じ見た目と挙動になる。",
                "Debug ConsoleにSave failed、Load failed、参照切れが出ていない。",
            },
            "All保存や全ステージ反映は広い範囲を書き換えます。まず個別保存か現在シーンへの反映で確認し、意図した差分だと分かってから広い操作を使ってください。",
        },
        {
            "00 基本",
            "Game View / 3D編集",
            "ゲームビュー ImGuizmo 配置 選択 カメラ",
            "3Dシーンの見た目を確認しながら、選択、配置、移動、回転、拡縮を行う中心画面です。Project、Preset、Particleのドラッグ&ドロップ先でもあります。",
            "通常のEditor画面中央がGame Viewです。上部メニューの「表示」からHierarchy / Inspectorも表示して併用します。",
            {
                "HierarchyまたはGame View上で対象を選択します。Shiftを併用すると対応箇所で複数選択できます。",
                "T、R、SでImGuizmoの移動、回転、拡縮を切り替え、軸または面ハンドルをドラッグします。",
                "ProjectのモデルやPresetをGame Viewへドラッグし、配置プレビューを左クリックで確定します。右クリックまたはEで中止します。",
                "Fで選択対象へフォーカスし、右ドラッグ+WASD、Q/E、Shiftで確認しやすい視点へ移動します。",
                "変更後は正面だけでなく側面とゲーム本番カメラから、埋まり、浮き、スケール、前後関係を確認します。",
            },
            "Game View自体は保存しません。3Dオブジェクトの変更はHierarchyのScene Fileから対象カテゴリまたはAllを保存します。Editorカメラ位置は専用状態として別保存されます。",
            {
                "ギズモのモードが意図したT/R/Sになっている。",
                "複数選択時に意図しない対象まで動いていない。",
                "配置確定後に保存カテゴリとLayerが正しい。",
            },
            "プレビュー中の見た目とシーンへ確定したオブジェクトは別です。左クリックで確定し、Hierarchyに追加されたことを確認してから保存してください。",
        },
        {
            "00 基本",
            "ショートカット一覧",
            "キー操作 keyboard shortcut Undo Redo",
            "頻繁に使うEditor操作をキーから実行し、配置と調整を効率化します。",
            "Game Viewまたは対応するEditorを操作中に使います。文字入力欄へフォーカスしている間は、入力欄の操作が優先される場合があります。",
            {
                "最初にT/R/S、F、Ctrl+Z/Y、Deleteの動作を覚えます。",
                "配置作業ではTab、左クリック、右クリック/Eを使います。",
                "Editorカメラ操作は右ドラッグ中にWASD、Q/E、Shiftを組み合わせます。",
            },
            "ショートカット操作で変更した3Dオブジェクトも自動保存ではありません。Scene Fileから保存します。",
            {
                "テキスト入力中やタイムライン操作中にショートカットが誤発火していない。",
                "Undo後の状態が意図どおりかGame ViewとInspectorの両方で確認する。",
            },
            "Ctrl+Z/YはEditor内の操作履歴です。保存後のJSONをバージョン単位で復元する機能ではありません。",
            ManualSpecial::Shortcuts,
        },
        {
            "01 シーン編集",
            "Hierarchy",
            "階層 親子関係 visibility lock category layer object",
            "シーン内オブジェクトの一覧、親子関係、表示、編集ロック、検索、カテゴリ、Layerを管理します。上部には全専用Editor、下部にはScene Asset管理があります。",
            "左ペインの「Hierarchy」タブを開きます。システム設定一覧の下にScene Asset、検索、分類、Layer、オブジェクトツリーが並びます。",
            {
                "検索欄または分類・Layerフィルターで対象を絞り、名前をクリックして選択します。",
                "目アイコンで表示、鍵アイコンで編集ロックを切り替えます。ロックは誤操作防止に使います。",
                "親子関係を変更する場合は子を目的の親へドラッグし、ワールド位置が維持されているか確認します。",
                "右クリックの作成メニュー、またはモデル・Presetのドラッグで新規オブジェクトを作成します。",
                "Camera ObjectはCameraカテゴリとして扱い、通常Objectとは別の保存ボタンを使います。",
            },
            "Hierarchy下部のScene AssetからPlayer、Enemy、Object、CameraまたはActive Scene全体を保存します。親子関係、表示、Transformなどは対象カテゴリJSONへ入ります。",
            {
                "名前が重複して追従先・イベント先の解決を曖昧にしていない。",
                "親と子の保存カテゴリが意図どおりである。",
                "EditorOnlyの補助物をゲーム用カテゴリへ混ぜていない。",
            },
            "親を削除すると子や参照先へ影響します。削除前に子階層とEvent Link Graph、追従設定を確認してください。",
        },
        {
            "01 シーン編集",
            "Scene Asset / 新規・切り替え",
            "Unity Scene Asset 新規 開く 複製 名前変更 削除 Active Scene runtime SceneFactory SceneLoadContext Controller BGM Light Camera Skybox",
            "登録済みC++ Sceneクラスと、Object・Camera・Sprite・BGM・Light・Camera設定・Skybox・Controllerを組み合わせたScene Assetを追加、切り替えます。コードの挙動とステージデータを分離して再利用できます。",
            "Hierarchyの「Scene Asset」を開くか、上部メニューの「ファイル」から「新規Scene Asset」を選びます。作成済みAssetは上部の「シーン切り替え」にも自動表示されます。",
            {
                "新規では半角英数字、_、-でScene IDを入力し、日本語を使える表示名を設定します。",
                "実行クラスでGAMEPLAY、TITLE、PREVIEW、SCENE_EDITORなど、SceneFactoryへ登録済みのC++ Sceneを選びます。",
                "空のScene、または現在のSceneを複製するテンプレートを選びます。現在Sceneの複製はメモリ上のObjectも含めます。",
                "Scene一覧から対象を選び、「開く」またはダブルクリックで、選択した実行クラスとAssetのJSONを同時に読み込みます。",
                "既存Assetの実行クラスはScene Asset欄のコンボから変更できます。変更後はAssetを開き直すと反映されます。",
                "実行設定ではScene Controller、BGM、Light JSON、Camera JSON、Skyboxを指定します。空欄はC++ Sceneクラスの既定値を使用します。",
                "検証ボタンでGAMEPLAYに必要なPlayer、Camera、goal、重複Object名、参照ファイルを確認します。エラーがあるGAMEPLAY Assetは実行前に停止します。",
                "未保存変更がある場合は、保存して開く、保存せず開く、キャンセルから選びます。保存を選ぶとSave Preview確認後に切り替わります。",
                "複製、名前変更、削除はScene一覧で対象を選んで実行します。Active Sceneは別Sceneを開くまで削除できません。",
                "新しいC++ Sceneクラスを追加した場合はSceneFactoryのRegisterSceneへ登録します。登録名は実行クラス候補と上部メニューへ自動反映されます。",
                "GAMEPLAYを共有して固有処理だけ追加する場合はISceneControllerを継承し、SceneControllerFactoryへ登録します。OnInitialize、OnUpdate、OnFinalizeだけを実装します。",
            },
            "ObjectはResources/json/3Dobject/<id>.jsonとカテゴリ別JSON、SpriteはResources/json/sprite/<id>_sprite.jsonへ保存されます。実行クラスとControllerは_sceneAsset、各リソースは_sceneAsset.resourcesへ保存されます。",
            {
                "Active表示とGame Viewの内容が一致している。",
                "実行クラスが目的のゲームロジックと一致している。",
                "GAMEPLAYの場合、Controllerが登録済みでPlayerが存在する。",
                "BGM、Light、Camera、Skyboxのパスが存在する。",
                "名前変更後の保存先が新しいScene IDへ切り替わっている。",
                "削除対象がActive Sceneや他ステージから参照されるSceneではない。",
                "実行中やシーン遷移中に切り替え操作をしていない。",
            },
            "SceneLoadContextはScene初期化前に注入されます。ControllerはGAMEPLAYでのみ動作し、通常のゲームループを置き換えません。HUDなどC++ Sceneが追加読込する専用データやゲーム進行条件は、そのScene側の仕様が継続します。",
        },
        {
            "01 シーン編集",
            "Scene File / シーン保存",
            "Player Enemy Object Camera All JSON dirty 個別保存",
            "現在シーンをカテゴリ別JSONへ保存します。競合を避ける個別保存と、シーン一式を揃えるAll保存を使い分けます。",
            "Hierarchyの「Scene Asset」を開き、Active Scene名と未保存カテゴリを確認します。",
            {
                "Active Sceneが現在作業中のステージになっているか確認します。_player等の分割ファイルを直接選ぶ画面ではありません。",
                "未保存表示でPlayer、Enemy、Object、CameraのどこがDirtyか確認します。",
                "担当範囲だけなら個別保存を押します。Camera ObjectはCameraのみ保存を使います。",
                "全カテゴリを一式で確定するときだけ「Active Sceneを保存」を使います。",
                "保存後に表示される保存先とDirty表示を確認し、必要ならJSONを再読み込みして再現性を確認します。",
            },
            "基本パスはResources/json/3Dobject/<scene>.jsonです。カテゴリ別データは同名を基準に_player、_enemy、_object、_cameraへ分割されます。",
            {
                "保存ボタンと編集したカテゴリが一致している。",
                "保存後に未保存表示が解消した。",
                "別ブランチや別担当者のカテゴリをAll保存で意図せず上書きしていない。",
            },
            "All保存は便利ですが変更範囲が広くなります。競合作業中は個別保存を基本にし、All保存前はSave Previewで削除・変更・追加を確認してください。",
        },
        {
            "01 シーン編集",
            "Save Preview / クラッシュ復元",
            "保存差分 crash recovery backup 復元 autosave",
            "Scene保存前の追加・変更・削除を確認し、異常終了時には未保存ドラフトから作業を救出します。",
            "Scene保存操作の前にSave Previewが開く場合は差分一覧を確認します。クラッシュ復元候補がある場合は次回起動時に専用ダイアログが表示されます。",
            {
                "Save Previewで保存対象ファイルとカテゴリを確認します。",
                "追加、変更、削除の件数とオブジェクト名を確認し、意図しない削除があれば保存を中止します。",
                "クラッシュ復元が出た場合は候補のシーン名、時刻、Dirty概要を読み、復元するか破棄するか決めます。",
                "復元後は必ずGame ViewとHierarchyを確認し、通常のScene保存で正式JSONへ確定します。",
            },
            "クラッシュ用ドラフトはResources/.backup/crash_recoveryへ約3秒間隔で作られます。正式なシーンJSONを直接上書きする自動保存ではありません。",
            {
                "復元対象と現在のブランチ・シーンが一致している。",
                "復元後に正式保存したファイルだけが意図した差分になっている。",
                "正常終了後に古い復元候補を誤って採用していない。",
            },
            "クラッシュ復元は安全網であり、通常保存の代替ではありません。復元ドラフトを採用する前に、現在の正式JSONより新しく必要な内容か確認してください。",
        },
        {
            "01 シーン編集",
            "Inspector / Material",
            "Transform Collision PBR Material Gimmick Enemy Item Camera LOD",
            "選択中オブジェクトの名前、クラス、Transform、描画、当たり判定、イベントID、ゲーム固有パラメータを編集します。Camera Object選択時はCamera専用Inspectorへ切り替わります。",
            "HierarchyまたはGame Viewでオブジェクトを選択し、右ペインの「Inspector」を開きます。",
            {
                "最上部で名前、クラス、保存カテゴリ、Tag、Layerを確認します。",
                "Transformで位置・回転・スケールを調整し、モデル変更時は原点と実寸を確認します。",
                "Collisionで形状、サイズ、中心、Trigger、Maskを設定し、見た目と判定のズレを確認します。",
                "Renderer / MaterialでColor、Emissive、Blend、PBRテクスチャ、Material Typeを調整します。",
                "下部のEnemy、Gimmick、Item、Spawner、Particle、Ghost Recorder、LODなどクラス固有項目を設定します。",
            },
            "Inspectorの変更はScene Fileの対象カテゴリへ保存します。テクスチャやモデルそのものを書き換える画面ではありません。",
            {
                "モデル変更後にCollisionとスケールを再確認した。",
                "透明物のBlend、影、描画順が本番カメラで正しい。",
                "Event IDとTarget IDに重複・未接続がない。",
                "LODが有効な場合、距離切り替え時に欠落や急なサイズ変化がない。",
            },
            "クラスや保存カテゴリを誤るとゲーム側の生成・更新対象から外れます。見た目だけで判断せず、Scene Validatorと実行確認を行ってください。",
            ManualSpecial::Materials,
        },
        {
            "01 シーン編集",
            "Project / Asset Browser",
            "3DModel Sprite Effects Preset drag drop thumbnail",
            "Resources配下のモデル、Sprite、Preset、Effectをフォルダとサムネイルから探し、シーンへ配置するアセットブラウザです。",
            "画面下部の「Project (Assets)」または「Sprite Assets」タブを開きます。",
            {
                "Models、Effects、Preset、Spriteなど目的のルートを選び、フォルダをたどるか検索します。",
                "サムネイルとファイル名を確認し、モデルまたはPresetをGame Viewへドラッグします。",
                "配置プレビューで接地、向き、サイズを確認し、左クリックで確定します。",
                "Effect JSONは対応EditorまたはEffect Previewで先に内容を確認してから配置します。",
            },
            "Projectはアセット一覧なので保存操作はありません。配置したObjectはScene File、Preset編集はPreset Editor、SpriteレイアウトはSprite Editorから保存します。",
            {
                "同名の別フォルダアセットを取り違えていない。",
                "LODバッジ付きモデルを単体モデルとして誤配置していない。",
                "配置確定後の保存カテゴリとクラスが正しい。",
            },
            "Projectから見えることはゲーム中に参照されることを保証しません。配置後はScene保存と実行時ロードを確認してください。",
        },
        {
            "01 シーン編集",
            "Sprite Editor",
            "Sprite Hierarchy Inspector Assets UI layout anchor JSON",
            "ゲームUIをSprite Hierarchy、Sprite Inspector、Sprite Assetsで編集し、位置、サイズ、色、アンカー、親子関係をJSONレイアウトとして管理します。",
            "上部の「編集モード」からSprite編集へ切り替えるか、Sprite Hierarchy / Sprite Inspector / Sprite Assetsを表示します。",
            {
                "Sprite Assetsから画像を選び、対象レイアウトへ追加します。",
                "Hierarchyで名前、表示、ロック、親子関係、描画順を整理します。",
                "Inspectorで位置、サイズ、スケール、回転、色、アンカー、スナップを調整します。必要なら実画像サイズ・比率補正を使います。",
                "基準解像度と実ゲーム画面の両方で、画面端、重なり、文字の可読性を確認します。",
                "現在のレイアウト名を確認して明示的に保存します。",
            },
            "SpriteレイアウトはResources/json/sprite/<layout>.jsonへ保存され、画像はResources/sprite配下を参照します。",
            {
                "親を動かした時に子UIが意図どおり追従する。",
                "アスペクト比変更時にアンカーと画面端余白が崩れない。",
                "透明PNGの背景や余白が意図せず表示領域を広げていない。",
            },
            "現状のレイアウト読込はシーン側の同期が中心で、Editor内の独立したLoad操作は限定的です。別レイアウトを直接上書きせず、現在名を確認して保存してください。",
        },
        {
            "02 描画・入力",
            "カメラ設定 (Camera)",
            "Camera Editor Main Cinematic Camera Object preview follow easing FOV",
            "Editor自由カメラとゲーム用カメラ設定を扱い、シーン内のCamera Objectは演出カメラとして配置・プレビュー・追従・補間できます。",
            "Hierarchy最上部の「カメラ設定 (Camera)」を選びます。演出用カメラは作成メニューからCamera Objectを生成し、そのObjectを選択して専用Inspectorを使います。",
            {
                "Editor自由カメラは右ドラッグ+WASD、Q/E、Shiftで移動し、必要な確認視点を作ります。",
                "Camera Objectを作成し、Transform、FOV、Near/Far Clip、Blend In/Out、切替Easingを設定します。",
                "Eye / FollowとTarget / Followで追従対象、完全追従か補間追従か、オフセットを設定します。",
                "Camera Object選択中のImGuiプレビューで構図を確認し、「Cameraをテスト再生」で切替と追従を確認します。",
                "Camera Editorのプレビュー更新頻度と解像度は、通常15 FPS・50%を基準にします。別タブや折り畳み中のプレビューは自動停止します。",
                "ムービーではGhost DirectorへCamera Objectをトラックとして追加し、複数オブジェクトと同期します。",
            },
            "カメラ設定はResources/json/camera/<name>.json、Editor自由カメラ状態はResources/json/camera/editor_camera_state.json、Camera ObjectはScene FileのCameraカテゴリへ保存します。",
            {
                "被写体が画面端で切れず、床や背景の不要部分が映っていない。",
                "Blend開始・終了時にMain Cameraとの位置差で急旋回しない。",
                "追従補間で遅れすぎず、停止時に細かく揺れ続けない。",
            },
            "旧Camera Editorの演出カメラ設定とCamera Objectは役割が異なります。新しいムービーはCamera Objectを基準にし、SceneのCameraカテゴリとGhost Directorを保存してください。",
        },
        {
            "02 描画・入力",
            "ライティング設定 (Lighting)",
            "Light Directional Point Spot Fog Skybox IBL Environment",
            "Directional、Point、Spot、Fog、Skybox、環境光を調整し、シーン全体と局所演出の明暗を作ります。",
            "Hierarchy最上部の「ライティング設定 (Lighting)」を選びます。",
            {
                "既存のLight JSONを選択してLoadし、現在シーンの基準状態を読み込みます。",
                "シャドウ解像度は通常2048、負荷優先は1024を使います。影を作る範囲は必要なプレイ領域だけを覆う値にします。",
                "Directional Lightの方向、色、強度と影を調整します。必要なら「明るい影プリセット」を出発点にします。",
                "Point / Spot Lightを追加し、位置、範囲、減衰、色、強度、追従対象を設定します。",
                "ステータスの有効ライト数を確認します。視錐台外・強度0・範囲0のライトは自動的にGPU転送から除外されます。",
                "Fog、Skybox、Environment Map、IBLを調整し、遠景とモデル材質の見え方を確認します。",
                "Game Viewの昼夜、屋内外、特殊マテリアルを確認してからSaveします。",
            },
            "Resources/json/light/<name>.jsonへ保存します。画面上部に現在の完全パスと読込成否が表示されます。",
            {
                "影が黒つぶれせず、発光物も白飛びしていない。",
                "Point / Spotの個数と範囲が必要最小限である。",
                "別カメラ角度でもライトが急に消えたりモデル裏面だけ暗くならない。",
            },
            "Lightの全削除や別JSONのLoadは現在状態を大きく変えます。Save前にファイル名を確認し、必要なら別名で試してください。",
        },
        {
            "02 描画・入力",
            "ポストエフェクト (Post Effect)",
            "Bloom Tone Mapping Blur Outline Iris Fade Slime Fade",
            "画面全体のBloom、Tone Mapping、Blur、Outline、Fadeなどを調整し、通常画面と画面遷移の最終的な見え方を作ります。",
            "Hierarchy最上部の「ポストエフェクト (Post Effect)」を選びます。",
            {
                "Tone MappingをOFF、Real、Animeから選び、シーンの基準となる明るさと色を決めます。",
                "BloomのON / OFF、品質、閾値、強度を調整します。低4・中6・高10 passesで、無効またはIntensity 0なら最終Composite 1 passだけになります。",
                "Blur、Outline、Dissolveなど必要な効果だけ有効化し、通常プレイへの負荷と視認性を確認します。",
                "Iris Fade、Slime Fadeなど時間変化する項目はテスト操作で開始・終了まで確認します。",
                "複数シーンで使う場合は同じJSONをLoadして差を比較してからSaveします。",
            },
            "標準設定はResources/json/post_effect.jsonへSave JSON / Load JSONします。",
            {
                "通常プレイ時に一時演出用のFadeやBlurが残っていない。",
                "BloomでUIや空が白飛びしていない。",
                "演出終了時にパラメータが基準値へ戻る。",
            },
            "派手さをBloomだけで作ると画面全体の情報量が失われます。発光元のMaterial、Particle、Lightと役割を分けて調整してください。",
        },
        {
            "02 描画・入力",
            "キーコンフィグ (Key Config)",
            "Input Action keyboard mouse binding",
            "ゲーム内Action名とキーボード・マウス入力の対応を編集します。",
            "Hierarchy最上部の「キーコンフィグ (Key Config)」を選びます。",
            {
                "既存Action一覧から編集対象を選ぶか、新しいAction名を追加します。",
                "入力追加を開始し、割り当てたいキーまたはマウスボタンを押します。",
                "不要なBindingまたはActionを削除し、重複や操作不能になる割り当てがないか確認します。",
                "Save後にゲームを実行し、押下・長押し・同時押しを確認します。",
            },
            "Resources/json/key/keyconfig.jsonへSaveし、Loadでディスク上の設定へ戻します。",
            {
                "決定とキャンセル、移動とカメラなど必須Actionが未割当になっていない。",
                "同じキーの重複がゲーム仕様上問題ない。",
                "Editor操作中ではなく実ゲーム入力として確認した。",
            },
            "Loadは未保存の変更を破棄します。必須Actionを削除すると操作不能になるため、変更前の設定を残してから試してください。",
        },
        {
            "03 VFX",
            "通常パーティクル (Particle)",
            "CPU Particle emitter shape physics size graph texture",
            "比較的軽量で少数の粒子表現を作り、Shape、物理、色、寿命、Size GraphをJSONとして調整します。",
            "Hierarchy最上部の「通常パーティクル (Particle)」を選びます。",
            {
                "TextureとRender設定を選び、加算・通常など背景に合うBlendを決めます。",
                "Emitter Shape、発生数、間隔、寿命、初速、重力を設定します。",
                "色、Alpha、回転、開始・終了サイズを調整します。",
                "Size Graphをマウスで編集し、発生から消滅までの大きさ変化を整えます。",
                "単発とループを確認し、画面外や終了後に粒子が残らない状態で保存します。",
            },
            "Resources/json/particle/<name>.jsonへ保存します。保存したJSONはProjectや対応するObject設定から参照します。",
            {
                "寿命と発生間隔から同時粒子数が増えすぎていない。",
                "Alphaが0へ戻り、ループ停止後に消える。",
                "ゲーム本番のカメラ距離と背景色でも形が読める。",
            },
            "大量発生や広範囲演出を通常Particleで無理に作らず、GPU Particleとの使い分けを検討してください。",
        },
        {
            "03 VFX",
            "GPUパーティクル (GPU Particle)",
            "GPU Particle burst aura fire smoke collision spritesheet quick preset",
            "大量粒子、爆発、火花、煙、オーラ、収集演出をGPUで描画し、発生、衝突、環境影響、Spritesheetを調整します。",
            "Hierarchy最上部の「GPUパーティクル (GPU Particle)」を選びます。",
            {
                "Quick Presetから近い用途を選ぶか、新規設定でSystemとEmit Shapeを決めます。",
                "発生数、Burst / Loop、寿命、速度、重力、Drag、Collisionを調整します。",
                "最大同時数は通常0 (Auto)にし、表示されるSystem実行容量を確認します。重い演出だけ手動容量を増やします。",
                "Color、Alpha、Size、回転、Blend、Spritesheetを調整します。",
                "Preview Environmentで背景、床、カメラ距離を変え、単発とループの両方を再生します。",
                "ゲーム内の発生位置・スケールで負荷と見え方を確認して保存します。",
            },
            "Resources/json/gpu_particles/<name>.jsonへ保存します。VFX Sequencer、Trail Emitter、GPUParticle Objectから参照できます。",
            {
                "Burstが一度だけ発生し、Loopは停止後に収束する。",
                "Collisionや床バウンドで粒子が無限に残らない。",
                "発生数、寿命、間隔に対してSystem実行容量が過剰でも不足でもない。",
                "同時に複数個出した時も過剰な発光やフレーム低下がない。",
            },
            "Editorの隔離プレビューだけで完成扱いにせず、実ゲームの背景、カメラ、Post Effectを含めて確認してください。",
        },
        {
            "03 VFX",
            "VFXシーケンサー (VFX Sequencer)",
            "timeline GPU Particle Mesh Effect SE camera shake light sequence",
            "GPU Particle、移動Trail、Mesh Effect、SE、Camera Shake、Lightを同じ時間軸で同期し、一つの演出JSONとして再生します。",
            "Hierarchy最上部の「VFXシーケンサー (VFX Sequencer)」を選びます。",
            {
                "新規シーケンスを作り、演出全体の長さとLoop有無を決めます。",
                "トラックへGPU Particle、Trail、Mesh Effect、SE、Camera Shake、Lightを追加します。",
                "各クリップの開始時刻、長さ、Transform、参照JSON、強度を設定します。",
                "1/4、1/2、通常速度でタイムラインを再生し、発生順と重なりを詰めます。",
                "Preview Environmentとゲーム内ターゲット位置の両方で確認して保存します。",
            },
            "Resources/json/vfx_sequence/<name>.jsonへ保存します。Ghost DirectorのVFXトラックからも参照できます。",
            {
                "SE、光、揺れが視覚効果と同じ瞬間に始まる。",
                "シーケンス終了後にLight、Shake、Particleが残らない。",
                "参照しているParticle / MeshEffect JSONの名称変更で切れていない。",
            },
            "素材側JSONとSequence側JSONの両方が必要です。片方だけ保存・移動すると参照切れになるため、最終的に実ゲームから再生してください。",
        },
        {
            "03 VFX",
            "メッシュエフェクト (Mesh Effect)",
            "procedural model shader reveal distortion collision OBJ",
            "メッシュ形状と専用Materialを使い、衝撃波、炎、ゲート、レーザーなど輪郭を持つ演出を作ります。",
            "Hierarchy最上部の「メッシュエフェクト (Mesh Effect)」を選びます。",
            {
                "Procedural Shapeまたはモデルを選び、基準Transformと向きを決めます。",
                "Reveal、Scale、Rotation、Color、Emissive、Shader Parameter、Distortionを調整します。",
                "必要な場合だけCollisionを有効化し、見た目と判定の持続時間を合わせます。",
                "Effect Previewで単発、Loop、カメラ距離、背景を変えて確認します。",
                "必要なら形状をOBJ Exportし、JSONを保存してゲーム内で再生します。",
            },
            "Resources/json/effect/<name>.jsonへ保存します。ExportしたOBJは指定したモデル出力先へ生成されます。",
            {
                "面の向きやCullで特定角度から消えない。",
                "Revealと寿命の最後で急に消えず、AlphaやScaleが自然に収束する。",
                "Collisionを使う場合、視覚効果より長く判定が残らない。",
            },
            "大きな面を加算発光させると画面を覆いやすいです。Particleと役割を分け、本番カメラで占有率を確認してください。",
        },
        {
            "03 VFX",
            "3D破片エフェクト (Debris Effect)",
            "Debris rock wood fragments pseudo physics bounce",
            "岩、木片、瓦礫などの3D破片を複数飛ばし、擬似物理、回転、地面反射、寿命を調整します。",
            "Hierarchy最上部の「3D破片エフェクト (Debris Effect)」を選びます。",
            {
                "Rock、Wood、Pebblesなど近いQuick Presetを選ぶか、使用モデルを複数登録します。",
                "Spawn範囲、個数、初速、方向のランダム幅、回転を設定します。",
                "重力、Drag、Ground Bounce、摩擦を調整します。",
                "Color、Scale、寿命、Fadeを設定し、単発とLoop Previewを確認します。",
                "ゲーム中に複数回連続発生させ、破片が蓄積しないことを確認して保存します。",
            },
            "Resources/json/debris/<name>.jsonへ保存します。",
            {
                "床高さとBounce基準がステージの床に合う。",
                "破片が大きすぎず、プレイヤーや重要物を隠さない。",
                "寿命後に全破片が破棄される。",
            },
            "本格的な剛体物理ではなく演出用の擬似物理です。複雑な地形への正確な衝突を前提にせず、見える範囲と寿命を制限してください。",
        },
        {
            "03 VFX",
            "トレイルエミッター (Trail Emitter)",
            "Trail follow movement distance MeshEffect GPU particle orient",
            "対象Objectの移動距離に応じてMesh EffectやGPU Particleを発生し、残像、足跡、軌跡を作ります。",
            "Hierarchy最上部の「トレイルエミッター (Trail Emitter)」を選びます。",
            {
                "追従対象Objectを選ぶか、Dummy Playbackでテスト用の移動を作ります。",
                "発生素材としてMesh Effect、GPU Particle、または両方のJSONを選びます。",
                "発生距離、位置・回転オフセット、Auto Orient、寿命を設定します。",
                "Follow開始後に低速・高速・停止・方向転換を試し、密度と向きを確認します。",
                "Follow停止後に残留演出が消えることを確認して保存します。",
            },
            "Resources/json/trail_emitter/<name>.jsonへ保存し、参照素材はResources/json/effectとgpu_particlesに置きます。",
            {
                "フレームレートではなく移動距離に応じて均一な間隔になる。",
                "停止中に同じ場所へ無限発生しない。",
                "急旋回時にAuto Orientが反転しない。",
            },
            "対象Object名に依存する設定は改名で切れる可能性があります。実シーンでターゲットを再選択し、Scene保存後に再読み込みしてください。",
        },
        {
            "03 VFX",
            "エフェクト確認ステージ (Effect Preview)",
            "isolated preview studio viewport background grid axis lighting camera speed loop",
            "ゲームシーンから離れたBlender Studio Viewport風の隔離空間で、ParticleとMesh Effectを背景、床、1mグリッド、軸、照明、カメラ、速度を揃えて比較します。",
            "Hierarchy最上部の「エフェクト確認ステージ (Effect Preview)」を選びます。",
            {
                "Previewを有効化し、通常確認はStudio Dark、黒煙や暗色EffectはStudio Light、発光・Bloom・AlphaはEmission Blackを選びます。",
                "床、1mグリッド、XYZ軸を表示し、原点、接地、高さ、横幅を確認します。標準空間は直径70mで旧Previewの約3.5倍です。外周の5m Major Lineで大きいEffectの規模も判断できます。",
                "プレビュー中心高さは標準1mです。中心原点のMesh EffectやParticleが床へ埋まる場合に使い、足元から出す接地型Effectだけ0mへ下げます。Material PreviewはモデルAABBの最下端から自動接地します。",
                "Studio Lightingを有効にすると、Directional Light、Ambient、Fog、SkyboxがPreview中だけ中立設定へ切り替わります。必要ならKey Lightの色、方向、強度を調整します。",
                "カメラ距離、高さ、方位角、注視点高さを設定します。標準では斜め上からの3/4 Viewになり、「カメラをプレビューへ移動」でいつでも標準構図へ戻せます。",
                "確認するEffectを選び、One ShotまたはLoopで再生します。",
                "正面、斜め、遠距離、近距離から輪郭、Alpha、発光を確認します。",
                "比較するEffectは同じPreset、グリッド間隔、空間半径、距離、速度に揃えて差を確認します。",
                "調整後はPreviewを終了し、ゲームシーンの本番位置でも確認します。",
            },
            "Preview Stage自体は保存対象ではありません。調整した各Particle / Mesh Effect Editor側でJSONを保存します。床、グリッド、軸などの補助ObjectはEditorOnlyです。",
            {
                "Studio DarkとStudio Lightの両方で輪郭、色、Alphaが読める。",
                "原点と床の関係が正しく、1mグリッドからEffectの実寸を判断できる。",
                "中心原点のモデルやEffectが床へ埋まらず、接地型Effectも不自然に浮いていない。",
                "再生速度1.0でタイミングが成立する。",
                "Preview終了後にEditorカメラ、Directional Light、Fog、Skybox、発生位置が通常状態へ戻る。",
            },
            "Studio Lightingは評価条件を揃えるための一時設定で、本番のPost Effect、Light、遮蔽物を再現するものではありません。ここだけで完成判定せず、最後は実ステージでも確認してください。",
        },
        {
            "03 VFX",
            "マテリアル確認 (Material Preview)",
            "material board comparison special shader variants",
            "モデル上でMaterial Typeと特殊Shaderの見え方を並べ、同じ条件で比較します。",
            "Hierarchy最上部の「マテリアル確認 (Material Preview)」を選びます。",
            {
                "確認モデルを選ぶかGame Viewからドロップします。",
                "通常一覧、特殊Materialのみ、展開Variantなど表示範囲を選びます。",
                "選択対象の近く、またはEffect Previewの隔離空間へ確認ボードを生成します。",
                "各モデルを選択してInspectorからMaterial Parameterを調整し、光とカメラ角度を変えて比較します。",
                "確認が終わったら生成したPreview Objectを専用ボタンで削除します。",
            },
            "Material Preview自体の保存はありません。採用した値は実際のObjectのInspectorとScene Fileへ反映・保存します。",
            {
                "正面だけでなく逆光・斜めから破綻しない。",
                "透明、影、深度、Blendの重なりが正しい。",
                "確認用ObjectがHierarchyに残っていない。",
            },
            "確認用Objectを残したままScene保存しないでください。比較結果は実Objectへ移し、Preview削除後に保存します。",
        },
        {
            "04 データ・検証",
            "プロパティマトリクス (Property Matrix)",
            "multi edit property matrix compare bulk prefab override table",
            "Hierarchyで複数選択したObjectの共通プロパティを横長の表で比較し、セル単位または選択全体へまとめて編集します。",
            "HierarchyでCtrlまたはShiftを使って複数Objectを選択し、最上部の「プロパティマトリクス (Property Matrix)」を選びます。",
            {
                "Object名とProperty名の検索、基本・Transform・描画・Component・Camera・Gameplayなどのカテゴリで表示範囲を絞ります。",
                "行ごとの差を比較し、単独セルを直接編集します。回転は度数表示で入力されます。",
                "同じ値を揃える場合は一括適用欄でPropertyと値を選び、対象件数を確認して適用します。",
                "橙色のセルはPrefab Overrideです。元Prefabへ反映する場合はObjectをInspectorで開き、ApplyまたはRevertを選びます。",
                "編集後はCtrl+Z / Ctrl+Yでセル編集または一括適用が1操作として戻ることを確認します。",
            },
            "編集結果は現在シーンのObjectへ即時反映されます。確定後はScene Fileから該当カテゴリまたはAllを保存します。",
            {
                "複数Objectの位置、回転、描画値を一画面で比較できる。",
                "一括適用でロック中のObjectが意図せず変更されていない。",
                "Prefab Overrideと通常のScene差分を混同していない。",
                "Undo / Redo後も対象Objectと値が正しく復元される。",
            },
            "Camera固有値とGimmick / ItemのGameplay値にも対応します。Player / Enemyの共通StatusはStatus Tuningを正としているため、Matrixでは重複編集しません。",
        },
        {
            "04 データ・検証",
            "プリセットエディタ (Preset Editor)",
            "Preset prefab palette brush enemy gimmick item template",
            "よく使うObject、Enemy、Gimmick、Itemと親子構造を配置テンプレートとして管理し、パレットやブラシから再利用します。",
            "Hierarchy最上部の「プリセットエディタ (Preset Editor)」を選びます。",
            {
                "検索またはEnemy / Gimmick / Item / Modelフィルターで既存Presetを選びます。",
                "既存ObjectからPreset化するか、空Presetを作成し、モデル、Transform、Material、Collision、クラス固有値を設定します。",
                "再利用するだけならPreset、配置後も元データと接続したい場合はProjectのPrefabs (v3)へ登録します。Presetの右クリックからPrefabへ明示変換もできます。",
                "Prefab Instanceを選ぶとInspector上部へOverride一覧が表示されます。項目単位または全体でApply / Revertできます。",
                "Prefab Assetをダブルクリックするか右クリックしてPrefab Modeを開き、隔離表示された階層を通常のInspector / ImGuizmoで編集します。",
                "Prefab Modeでは子Objectの追加・削除・親変更も編集できます。Hierarchy上部のPrefabを保存、または破棄して戻るで終了します。",
                "Variantを作る場合はPrefab Assetを右クリックしてVariantを作成し、Prefab Modeで基底との差分だけを編集します。",
                "Scene固有にしたい場合はUnpackします。Prefabルートの位置・回転はScene配置値として保持されます。",
                "ダブルクリックまたはドラッグで配置し、Brushでは間隔と向きを確認しながら連続配置します。",
                "変更を保存し、新規シーンへ一度配置して再現性を確認します。",
            },
            "PresetはResources/json/preset/presets.json、PrefabはResources/json/prefab/prefabs.jsonへ保存されます。変更時の自動保存に加えて明示Saveも使えます。",
            {
                "Event IDなど配置ごとに一意であるべき値を固定していない。",
                "親子構造、保存カテゴリ、クラスが配置後も正しい。",
                "Presetはコピー配置、Prefab v3はリンク配置という違いを確認した。",
                "Variantの追加・削除・親変更がStructure差分として表示されている。",
            },
            "Presetの既配置Objectは独立データです。Prefab v3の保存やApplyはローカルOverrideのない別Instanceへ伝播します。Prefab Mode中に通常Scene保存を実行するとPrefab保存へ切り替わり、一時編集ObjectがScene JSONへ混ざらないよう保護されます。",
        },
        {
            "04 データ・検証",
            "シーン検証 (Scene Validator)",
            "validate duplicate Event ID missing target model collision texture",
            "現在シーンの重複ID、参照切れ、モデル欠落、不正なMaterial / Blend、Collision、Texture Pathなどを読み取り専用で検査します。",
            "Hierarchy最上部の「シーン検証 (Scene Validator)」を選びます。",
            {
                "Manual Scanを実行するか、必要な間だけAuto Scanを有効にします。",
                "Error、Warning、Infoのフィルターで重大度を絞ります。",
                "一覧の対象を選択し、HierarchyとInspectorで原因箇所を開きます。",
                "Inspectorまたは対応Editorで修正し、再Scanして件数が減ったことを確認します。",
                "シーン保存前にErrorが0であることを目標に最終確認します。",
            },
            "Scene ValidatorはJSONを保存・修正しません。修正後はScene Fileから対象カテゴリを保存します。",
            {
                "Duplicate Event IDとMissing Targetを優先して解消した。",
                "Missing Model / Textureが実行時の動的生成用ではないか確認した。",
                "修正後に再Scanと実ゲームロードを行った。",
            },
            "自動修正ツールではありません。警告の意図を確認せず値を消すと別の挙動を壊すため、参照元とクラス仕様を確認して手作業で直してください。",
        },
        {
            "04 データ・検証",
            "イベントリンク図 (Event Link Graph)",
            "Event ID Target ID graph link missing duplicate",
            "Event IDとTarget IDの接続を図と一覧で可視化し、未接続、重複、意図しない多重接続を確認します。",
            "Hierarchy最上部の「イベントリンク図 (Event Link Graph)」を選びます。",
            {
                "現在シーンをScanし、MissingとDuplicateの件数を確認します。",
                "Linked Onlyなどの表示条件を使い、確認したい接続だけに絞ります。",
                "ノードまたは一覧を選択して対象ObjectをHierarchy / Inspectorへ同期します。",
                "InspectorのEvent ID / Target IDを修正し、Graphを更新します。",
                "ゲーム内でイベント発火順と対象の反応を確認します。",
            },
            "Event Link Graphは読み取り専用です。ID修正後は各Objectの保存カテゴリをScene Fileから保存します。",
            {
                "同じEvent IDを複数対象で意図せず共有していない。",
                "Target IDが存在するObjectへ到達している。",
                "一方向・双方向の接続が仕様どおりである。",
            },
            "線が表示されることはイベント処理の成功を保証しません。発火条件、クラス側の受信処理、保存カテゴリも実行時に確認してください。",
        },
        {
            "04 データ・検証",
            "演出ノード (Effect Sequence Graph)",
            "Node Graph cinematic event dry run template gate entry",
            "演出処理を型付きノードとリンクで組み、順序、待機、条件、Effect実行を可視化します。",
            "Hierarchy最上部の「演出ノード (Effect Sequence Graph)」を選びます。大きい編集画面を開いて作業します。",
            {
                "BlankまたはGate Entryなど目的に近いTemplateからGraphを作ります。",
                "ボタンから必要なNodeを追加し、型が一致するPin同士を接続します。",
                "右側Propertiesで対象名、待機時間、Effect名などを設定します。",
                "Validationを実行して未接続・型不一致を直し、Dry Runで実ゲームへ影響を出さず順序を確認します。",
                "Execution Previewで実際の演出を確認し、名前を付けて保存します。",
            },
            "標準例はResources/json/nodegraph/gate_entry_effect_sequence.jsonです。任意名のNode Graph JSONとしてResources/json/nodegraphへ保存します。",
            {
                "開始Nodeから全実行Nodeへ到達できる。",
                "待機や分岐で永続ループが発生しない。",
                "参照Effect、Object、Event名が実シーンに存在する。",
            },
            "現状は右クリック追加よりTemplate / 追加ボタン中心の操作です。Dry Runはゲーム状態を変えない確認であり、最終的な実行結果はExecution Previewと実ゲームで確認してください。",
        },
        {
            "05 アニメーション",
            "アニメーション制作 (Animation Workbench)",
            "Animation bone pose keyframe event marker enemy preview",
            "ボーン付きモデルのAnimation再生、Pose Key、Bone Transform、Event MarkerをAI挙動から切り離して編集・確認します。",
            "Hierarchy最上部の「アニメーション制作 (Animation Workbench)」を選びます。",
            {
                "PreviewモデルとBase Animationを選び、隔離カメラへ移動します。",
                "TimelineのPlay、停止、Frame移動で確認したい姿勢へ移動します。",
                "Bone Overlayと名前表示を有効にし、対象Boneを選んで移動・回転・拡縮を調整します。",
                "Auto Keyまたは明示追加でPose Keyを作り、キー間の補間を再生確認します。",
                "攻撃判定やSEなどのEvent Markerを配置し、Preview Fireで発火時刻を確認して保存します。",
            },
            "AuthoringデータはResources/json/enemy_animation/<name>.jsonへ保存します。元モデルのAnimationを直接上書きするのではなく、Editor用追加データを管理します。",
            {
                "キー間でBoneが最短方向に補間され、不自然な反転がない。",
                "Loop境界で姿勢が跳ねない。",
                "Event Markerが見た目の接触フレームと一致する。",
            },
            "Animation WorkbenchはBone / Pose編集用です。複数ObjectとCameraを同期するムービーにはGhost Directorを使い、用途を混ぜないでください。",
        },
        {
            "05 アニメーション",
            "Animator Controller",
            "animator state transition parameter trigger crossfade blend controller",
            "Playerや敵のIdle、Run、Jump、AttackなどをStateとして管理し、条件付きTransitionと補間でボーンAnimationまたはプロシージャル演技を滑らかに切り替えます。",
            "Hierarchy最上部の「Animator Controller」を選びます。Object側から開く場合はInspectorのボーンアニメーション欄でControllerを割り当てて「Animator Controller Editorを開く」を押します。",
            {
                "新規Controllerを作成し、StatesでState名、Model内Clip名、速度、Loop、標準Blend時間とEasingを設定します。Playerスライムのようなプロシージャル演技ではClip名を空にできます。",
                "最初に再生するStateをEntryへ設定します。通常はIdleをEntryにします。",
                "ParametersへFloat、Int、Bool、Triggerを追加し、ゲーム側から更新する移動速度、接地、攻撃Triggerなどを定義します。",
                "Transitionsで遷移元、遷移先、Blend時間、Easing、Exit Time、Conditionを設定します。Any Stateは全State共通の被弾や死亡などに限定します。",
                "Preview Targetを選び、ControllerをObjectへ割り当てます。State再生とRuntime Parameter操作で、実際のModelが滑らかに切り替わることを確認します。",
                "保存後、Object Inspectorの現在State表示と実ゲームで遷移を確認します。Player用player_slimeを保存した場合はPlayer側にも再読込されます。",
            },
            "Resources/json/animator/<name>.jsonへ保存します。Objectへの割り当てはScene JSONのanimation.animatorControllerへ保存され、ゲーム読込、複製、リプレイ復元でも維持されます。",
            {
                "同じ条件を満たす固有TransitionとAny Stateが競合していない。固有Transitionが優先されます。",
                "短いBlendで姿勢が飛ばず、長すぎるBlendで操作への反応が遅れていない。",
                "LoopしないAnimationのExit Timeが終端前後に設定され、Transition先が存在する。",
                "Triggerが一度の遷移で消費され、同じ演技を毎フレーム繰り返さない。",
            },
            "旧Object Inspectorのアニメ名欄は単発Clip互換用です。新しいPlayer・敵の状態遷移はAnimator Controllerを使い、複数ObjectとCameraの時系列同期はGhost DirectorからAnimator Stateを指定してください。",
        },
        {
            "05 アニメーション",
            "ゴーストレコーダー (Ghost Recorder)",
            "path record waypoint spline easing camera override relative anchor",
            "単一ObjectまたはCameraの移動経路を録画・ウェイポイント生成し、Spline、Easing、相対基準を持つGhost Pathとして保存します。",
            "動かしたいObjectを選択してからHierarchy最上部の「ゴーストレコーダー (Ghost Recorder)」を選ぶか、Editor内のTarget Comboから選びます。",
            {
                "Targetを選び、必要ならAnchorと相対距離を設定します。",
                "実操作を記録する場合は録画開始から対象を動かして録画停止します。設計する場合はStart、Waypoint、Endを現在位置から登録します。",
                "各区間のEasing、Spline、相対データ化を設定し、Preview線とObject Ghostで経路を確認します。",
                "Generate & AutoSaveを実行し、Cinema Mode、Loop、メモリ再生またはファイル再生で確認します。",
                "停止でCamera乗っ取りを解除し、開始・終了姿勢が意図どおりか確認します。",
            },
            "Resources/json/animation/<name>.jsonへ保存・読込します。",
            {
                "TargetとAnchorが現在シーン内に存在する。",
                "Splineが障害物や床を突き抜けない。",
                "相対再生がAnchor移動後も同じ局所軌道になる。",
                "Camera再生停止後にMain / Editorカメラが戻る。",
            },
            "Ghost Recorderは一つの対象のPath制作に向いています。複数Object、Camera、VFXを同時に動かすムービーはGhost DirectorへPathを割り当てて同期してください。",
        },
        {
            "05 アニメーション",
            "ゴーストディレクター (Ghost Director)",
            "cinematic multi object camera animation VFX audio signal timeline auto key sequence path preview 軌跡",
            "複数Object、Camera Object、Animation、VFX、Audio、Signalを一つのタイムラインで同期し、ムービーやステージクリア演出を制作します。",
            "Hierarchy最上部の「ゴーストディレクター (Ghost Director)」を選びます。必要に応じて「大きいタイムライン」を開きます。",
            {
                "シーン上でObjectまたはCamera Objectを選び、「選択Objectをトラックへ追加」します。",
                "Track名、対象Object、有効、Mute、Lock、相対Transform、終了姿勢保持を設定します。",
                "Timeline Headを時刻へ移動して対象をImGuizmoで動かし、「現在姿勢をキー登録」またはAuto KeyでTransform Keyを作ります。",
                "移動を確認したいTransform Trackを選択し、Game Viewの軌跡、方向矢印、Start / Key / End、黄色のCurrent位置を確認します。表示されるのは選択トラック1本だけです。",
                "各Keyの時刻、Transform、次KeyへのEasingを調整します。既存Ghost Pathを使う場合はLegacy Ghost Pathへ割り当てます。",
                "Camera ShotへCamera Object、開始時刻、長さ、Blend In / Out、Easingを設定します。追従対象や注視方法はCamera Object側で調整します。",
                "Animation Clipへ対象Object、Driver、Animation / State、再生速度、Blend In、Easing、停止時復元を設定します。DriverがAnimator / Model / SkeletalならObjectのAnimator Stateを直接再生し、独自DriverだけをRuntime Callbackへ接続します。",
                "VFX Trackを追加し、VFX Sequence、発火時刻、Targetを設定します。",
                "Audio ClipへAudio ID、開始時刻、音量、Loopを設定します。スクラブ中は発音せず、通常再生で開始時刻を通過した時だけ発音します。",
                "SignalへSignal ID、Payload、時刻、任意Targetを設定し、UI表示やゲーム進行などシーン固有処理をRuntime Callbackへ接続します。",
                "ステージクリア演出はgoal_clear_timelineを編集します。GamePlay Sceneのゴール演出パネルで「Timeline再読込」を押すと、シーンを作り直さず次のプレビューへ反映できます。",
                "Play / Pause / Stop・姿勢復元で全体を確認し、Resources/json/scenarioへ保存します。",
            },
            "Resources/json/scenario/<name>.jsonへシーケンスを保存します。Legacy Pathを参照する場合はResources/json/animation側のJSONも必要です。",
            {
                "全Trackの対象名が現在シーンで一意に解決できる。",
                "Cameraの切替、被写体構図、Object動作、VFXが同じ時刻で成立する。",
                "AudioとSignalがスクラブで重複発火せず、Pause / StopでLoop音とCameraが正しく解除される。",
                "選択トラックの軌跡と実再生位置が一致し、親子関係や相対Transformでも移動先がずれない。",
                "補間に急な位置・回転ジャンプがなく、Stopで元姿勢とカメラへ戻る。",
                "Lock / Mute / 終了姿勢保持が本番仕様に合っている。",
            },
            "Auto Key中は意図しないImGuizmo操作もKeyになります。Timeline Headと選択Trackを確認してから動かし、試験後はStop・姿勢復元を使ってください。",
        },
        {
            "05 アニメーション",
            "リプレイデバッガー (Replay Debugger)",
            "replay rewind pause playback branch timeline time machine デバッグ 巻き戻し",
            "実行中シーンのObject、HP、速度、Player・敵の主要状態、Cameraを短時間記録し、停止、巻き戻し、履歴再生、選択地点からの分岐再開を行います。ゲーム用リプレイ動画ではなく、不具合再現と調査のためのTime Machineです。",
            "ゲームを再生し、上部の「リプレイ」メニューまたは「表示」から下段Replay Editorを開きます。Editorは通常は非表示で、開いている間だけProject、Console、ステータス領域を置き換える固定パネルになります。自動記録が有効ならシーン遷移完了後に記録が始まります。",
            {
                "記録頻度と履歴秒数を決めてからゲームを再生します。既定値は15fps・20秒で、実フレームすべてではなく一定間隔の状態を保持します。",
                "調べたい現象が起きたらMain Menu Barの「一時停止」を押します。Replay Editorが自動で開き、敵AI、物理、Camera、VFXを含むシーン更新が止まります。Editor内の一時停止も同じ用途で使えます。",
                "Frames、Spawn / Remove、HP / Deathの3レーンから変化が起きた時刻を探し、タイムラインをクリックまたはドラッグして発生直前へ戻します。Space、左右キー、Shift+左右キーでも操作できます。",
                "下段のObject一覧を変化のみ・名前・Classで絞り込み、選択ObjectのTransform、HP、速度、前Frame差分を右側Inspectorで確認します。ダブルクリックまたは「Scene上で選択」でGame Viewの対象と対応付けます。",
                "「前の変化」「次の変化」で、選択Objectが変わったフレームへ直接移動します。必要なら「履歴を再生」で記録済み区間を確認します。",
                "その時点から別の入力を試す場合は「この時点からプレイ再開」を押します。選択位置より後の履歴は破棄され、新しい時間軸として記録されます。",
                "Main Menu Barの「リプレイ再開」は、現在選んでいる時点から分岐再開します。下段Editorを閉じるとProject、Console、ステータスは元の表示設定で復帰します。",
                "通常の編集状態へ戻す場合は上部の停止ボタンを使います。シーン再読み込みと同時にリプレイ履歴も破棄されます。",
            },
            "履歴はメモリ上だけに保持し、JSONやScene Fileへは保存しません。",
            {
                "停止中に敵やPlayerの位置、HP、速度、Camera構図が記録時点へ戻る。",
                "撃破済みの敵や途中生成Objectが記録区間内なら表示・非表示を含めて戻る。",
                "分岐再開後に敵、Player、Collisionが動き出し、新しい履歴が増える。",
                "シーン切り替え後に前シーンの履歴が残っていない。",
            },
            "Particle、Bullet、Audioは完全な逆再生をせず、時間軸を切り替える際に破棄します。また敵固有AIの私有状態は今後個別対応が必要な場合があります。見た目の確認だけで決定論的なネットワークリプレイとして扱わないでください。",
        },
        {
            "06 生成・最適化",
            "テキストPNG生成 (Text PNG)",
            "font sprite PNG outline shadow transparent DirectWrite",
            "文字列を透明背景のPNG Spriteとして生成し、Font、太字、輪郭、影、余白、CanvasをEditor内で確認します。",
            "Hierarchy最上部の「テキストPNG生成 (Text PNG)」を選びます。",
            {
                "文字列を入力し、Resources/fontからFontと太字設定を選びます。",
                "文字色、Outline、Shadow、余白を設定し、Auto Canvasまたは手動Canvasサイズを選びます。",
                "PNG自動更新を使うか更新ボタンを押し、PreviewとBoundsで切れ・余白・背景透過を確認します。",
                "自動出力名または任意名を設定し、PNGを生成します。",
                "必要ならそのままSpriteへ追加し、Sprite Editorで実画面サイズに調整します。",
            },
            "生成PNGはResources/sprite/generated/text、Preview一時物はResources/generated/editor/text_previewへ出力されます。",
            {
                "輪郭、影、文字本体がCanvas端で切れていない。",
                "透明背景に不要な色や矩形が残っていない。",
                "実際のUIサイズで文字が潰れず読める。",
            },
            "Fontファイルが存在してもStyle非対応で見た目が変わらない場合があります。Previewだけでなく生成PNGをSpriteとして表示して確認してください。",
        },
        {
            "06 生成・最適化",
            "3Dテキスト生成 (Text 3D)",
            "3D Text OBJ extrusion sampling font GeneratedText",
            "Fontの輪郭から厚みのある3D文字OBJを生成し、看板、ステージ名、空間演出として配置します。",
            "Hierarchy最上部の「3Dテキスト生成 (Text 3D)」を選びます。",
            {
                "文字列とFontを選び、厚み、Sampling幅、中央原点など形状設定を行います。",
                "Game View Previewを有効にし、Camera AttachやRealtime Updateで正面・側面を確認します。",
                "Sampling幅を調整し、曲線品質と頂点数のバランスを取ります。",
                "出力名を設定してOBJ生成を行い、生成モデルをシーンへ配置します。",
                "配置後にMaterial、Scale、Collisionの要否をInspectorで設定してScene保存します。",
            },
            "Resources/3DModel/GeneratedText/<name>/<name>.objへ生成します。配置したObjectはScene Fileの対象カテゴリへ保存します。",
            {
                "文字の表裏、法線、中央原点、厚みが意図どおり。",
                "遠距離で読めるScaleとMaterialになっている。",
                "頂点数が用途に対して過剰でない。",
            },
            "Sampling幅を小さくすると曲線は滑らかになりますが頂点数が増えます。画面占有が小さい文字へ最高品質を使わないでください。",
        },
        {
            "06 生成・最適化",
            "モデル最適化 (Model Optimizer)",
            "LOD Blender decimate static OBJ glTF report distance",
            "静的OBJ / glTFを解析し、LOD用の軽量モデルを生成・比較して、Camera距離による自動切替をObjectへ設定します。",
            "Hierarchy最上部の「モデル最適化 (Model Optimizer)」を選び、Projectから対象モデルを選択またはドロップします。",
            {
                "Analyzeで形式、頂点数、Animation / Skin / Morphの有無と対応可否を確認します。",
                "LODごとの保持率を決め、Blender優先またはFallback生成を実行します。",
                "Effect Previewで元モデルとLODを同じ条件に並べ、輪郭、UV、Normalを比較します。",
                "問題なければ選択ObjectへLOD設定を適用し、切替距離をInspectorで調整します。",
                "ゲームの実カメラで近距離から遠距離まで移動し、切替と負荷を確認してScene保存します。",
            },
            "生成モデルは元モデル配下のLOD用ファイル、最新レポートはResources/.cache/model_lod/latest_report.jsonです。ObjectのLOD設定はScene JSONへ保存します。",
            {
                "LOD切替でScale、原点、Material、影が変わらない。",
                "輪郭の崩れがCamera距離に対して目立たない。",
                "実行時にCamera距離で自動切替される。",
            },
            "Skin、Bone、Morphを持つAnimationモデルとGLBは基本対象外です。無理に軽量化するとAnimationやMaterialが壊れるため、Analyzeの非対応判定に従ってください。",
        },
        {
            "06 生成・最適化",
            "地形生成 (Terrain Builder)",
            "terrain noise heightmap paint OBJ collider GeneratedTerrain",
            "Noise、段差、Heightmap、Paint Mapから静的地形OBJを生成し、Sceneへ配置します。",
            "Hierarchy最上部の「地形生成 (Terrain Builder)」を選びます。",
            {
                "地形名、幅、奥行き、分割数、高さを設定します。",
                "Noise / StepまたはPNG、JPG、BMPのHeightmapを選び、必要ならInvertとPaint Mapを設定します。",
                "低解像度Live Previewで大形状とCamera構図を確認します。",
                "OBJ生成を実行し、レポートと生成結果を確認してSceneへ配置します。",
                "配置後にTerrain Height ColliderまたはAABB Fallback、Material、Transformを設定し、実際に歩いて確認します。",
            },
            "Resources/3DModel/GeneratedTerrain/<name>/<name>.objへ生成し、最新レポートはResources/.cache/terrain_builder/latest_report.jsonです。配置ObjectはScene保存します。",
            {
                "地形端、最大傾斜、段差をPlayerが移動できる。",
                "見た目のHeightとCollisionが一致する。",
                "分割数と頂点数が必要以上に多くない。",
            },
            "Live Previewは軽量な近似で、最終Heightmap形状と完全一致しません。必ず生成OBJと実Collisionで最終確認してください。",
        },
        {
            "06 生成・最適化",
            "アセット監査 (Asset Audit)",
            "unused missing heavy assets trash audit report",
            "Resources内の未使用候補、参照切れ、重いアセットを外部監査ツールで列挙し、Previewや安全な退避を行います。",
            "Hierarchy最上部の「アセット監査 (Asset Audit)」を選びます。",
            {
                "Run Auditを実行し、完了後に最新レポートを読み込みます。",
                "種類、重大度、未使用候補などでFilterし、対象Pathと理由を確認します。",
                "画像Thumbnail、Audio試聴、3D Previewで内容を確認します。",
                "未使用と確定できる場合のみDelete操作でTrashへ移動します。",
                "ゲームを通しで確認した後、必要な場合だけTrashから完全削除します。",
            },
            "最新JSONレポートはResources/.cache/asset_audit/latest_report.json、削除候補の一時退避先はResources/.trash/asset_auditです。",
            {
                "コードで動的に組み立てるPathや外部参照で使われていないか確認した。",
                "Stage、UI、VFX、Audioの全利用箇所を検索した。",
                "Trash移動後にDebug / Development実行が成立する。",
            },
            "未使用候補は削除保証ではありません。動的参照は監査で見つけられない場合があるため、確認せず完全削除しないでください。",
        },
        {
            "07 運用・データ",
            "ステータス管理 (Status Management)",
            "player enemy stats hp speed gravity model realtime source of truth",
            "PlayerとEnemy Typeごとの最大HP、攻撃、速度、重力、Jump、索敵、モデル、Scaleなどを一元管理し、現在シーンへリアルタイム反映します。",
            "Hierarchy最上部の「ステータス管理 (Status Management)」を選びます。",
            {
                "PlayerまたはEnemy Typeのカードを開きます。",
                "数値またはモデルを変更すると、現在シーンの同タイプ対象へその場で反映されます。",
                "ドラッグ操作を離すと設定JSONへ自動保存されます。",
                "実ゲームで移動、戦闘、被弾、Animation、Collisionを確認します。",
            },
            "共通設定はResources/json/gameplay/status_settings.jsonです。Player/EnemyのシーンJSONには個体別ステータスを保存しません。",
            {
                "最大HP変更中も現在HPの割合が維持される。",
                "速度とAnimation速度、ScaleとCollisionが整合する。",
                "InspectorのPlayer/Enemyステータスが表示専用になっている。",
                "GimmickとItemの個別パラメータは従来どおり編集できる。",
            },
            "現在HPは実行中に変化する状態なので管理画面では編集しません。新規出現時は最大HPで開始します。",
        },
        {
            "07 運用・データ",
            "JSONバックアップ (Json Backup)",
            "watcher generations Resources json backup restore report",
            "Resources/json配下の変更を監視し、世代バックアップと最新レポートを作ります。",
            "Hierarchy最上部の「JSONバックアップ (Json Backup)」を選びます。起動時に監視状態が初期化されます。",
            {
                "画面の監視状態、最終実行時刻、レポートを確認します。",
                "作業前の節目で「今すぐバックアップ」を実行します。",
                "監視が止まっている場合は「監視開始」を押します。",
                "レポート再読み込みで作成された世代と対象ファイルを確認します。",
                "復元が必要な場合はBackup内の世代を確認し、対象JSONだけを手動で戻します。",
            },
            "世代バックアップはResources/.backup/json、最新レポートはResources/.cache/json_backup/latest_report.jsonです。",
            {
                "バックアップ時刻が重要な変更より前である。",
                "監視対象がResources/json配下になっている。",
                "復元時に別シーン・別カテゴリのJSONを上書きしない。",
            },
            "現状のEditorはバックアップ作成とレポート確認が中心で、ワンクリック復元は行いません。復元元と復元先を比較して手動で戻してください。",
        },
        {
            "07 運用・データ",
            "音声設定 (Audio Settings)",
            "BGM SE volume loop path preview user config",
            "全体BGM / SE音量と、Audio IDごとの名前、カテゴリ、音量、Loop、File Pathを編集・試聴します。",
            "Hierarchy最上部の「音声設定 (Audio Settings)」を選びます。",
            {
                "Master相当のBGM / SE音量を調整します。",
                "検索でAudio Entryを選び、ID、名前、カテゴリ、個別音量、Loop、File Pathを確認します。",
                "試聴と停止で音量、途切れ、Loop境界を確認します。",
                "BGM停止で現在鳴っているBGMを止め、別Entryとの重なりを避けます。",
                "保存後に再読み込みし、ゲーム内の実際の発音タイミングで確認します。",
            },
            "全体設定はResources/json/user_config.json、Audio EntryはResources/json/audio/audio_settings.jsonへ保存されます。",
            {
                "Audio IDが重複せず、参照先Fileが存在する。",
                "SEを連続再生して音割れ・過大音量にならない。",
                "Loop BGMの終端にクリック音や長い無音がない。",
            },
            "試聴音量とゲーム中の同時発音音量は異なります。保存後に複数SE、BGM、演出音が重なる場面で最終確認してください。",
        },
        {
            "07 運用・データ",
            "実行ファイルセット (Executable Package)",
            "build package Debug Development Release ExecutableSets DDS PNG compact ZIP manifest",
            "選択ConfigurationをBuildし、実行ファイル、DLL、必要なResourcesを提出・配布用フォルダとZIPへまとめます。元Resourcesは変更しません。",
            "Hierarchy最上部の「実行ファイルセット (Executable Package)」を選びます。",
            {
                "Set名とDebug / Development / ReleaseのConfigurationを選びます。",
                "Texture構成を選びます。コンパクト提出は同名DDS/PNGペアのPNGだけ、高速実行はDDSだけ、完全コピーは両方を残します。",
                "DDSのみ、またはPNGのみの素材はどのTexture構成でも保持されます。",
                "必要なら「提出用ZIPも作成」を有効にします。通常の提出ではRelease、コンパクト提出、ZIP有効を使用します。",
                "最新コードも含める場合は「ビルドして作成」、既に通った出力を使う場合は「既存出力から作成」を選びます。",
                "進捗とログを確認し、EXE、DLL、Resources、package_manifest.json、ZIPの作成完了を待ちます。",
                "作成されたSetフォルダからEXEを直接起動し、シーン、Audio、Font、Shader、JSON、Textureを確認します。",
                "提出前はReleaseでEditor非表示と操作・終了を確認します。",
            },
            "通常はプロジェクトと同階層のExecutableSets/<set name>と、必要に応じて<set name>.zipを作成します。.cache、.backup、.trash、tools、generated/editorは除外されます。",
            {
                "作成ログにBuild / Copy Errorがない。",
                "開発環境の作業フォルダに依存せず単体起動できる。",
                "必要な生成Assetと最新JSONが含まれる。",
                "package_manifest.jsonのTexture Mode、コピー件数、除外容量が意図どおりである。",
                "ZIPを展開したフォルダからもEXEが起動する。",
            },
            "Setは.building一時フォルダで完成させてから置き換えます。作成失敗時は以前の完成済みSetを保持しますが、手作業のファイルはSetフォルダへ保存しないでください。",
        },
        {
            "07 運用・データ",
            "キャプチャツール (Capture Tool)",
            "screenshot PNG recording MP4 GameView window desktop F10",
            "Game View、Window、DesktopをPNG ScreenshotまたはMP4動画として記録します。",
            "Hierarchy最上部の「キャプチャツール (Capture Tool)」を選びます。F10でポートフォリオ撮影モードも切り替えられます。",
            {
                "Capture範囲をGame View、Window、Desktopから選びます。",
                "動画の場合は解像度とFPSを確認し、「録画開始」を押します。",
                "演出を再生し、終了後に「録画停止」を押してEncode完了を待ちます。",
                "静止画は表示を整えてから「スクリーンショット」を押します。",
                "出力ファイルを開き、欠け、黒画面、UI表示、音の有無、フレーム落ちを確認します。",
            },
            "画面に表示されるCapture出力先へPNG / MP4を保存します。通常はプロジェクトと同階層のcapturesフォルダです。",
            {
                "目的の範囲だけが写り、Editor補助線や不要UIが入っていない。",
                "録画停止後にファイル書き込みが完了した。",
                "指定FPSと動画長が想定どおり。",
            },
            "録画中に強制終了するとMP4が完成しない場合があります。必ず停止操作を行い、Encode完了後にアプリを閉じてください。",
        },
        {
            "07 運用・データ",
            "内部データ編集 (Game Data)",
            "save slot lives coins crowns stars stage clear tutorial",
            "Save Slot 1～3の残機、Coin、Play Time、Tutorial、Stage解放・クリア・Star・王冠数を直接編集し、進行確認を短縮します。",
            "Hierarchy最上部の「内部データ編集 (Game Data)」を選びます。",
            {
                "編集するSave Slotを1～3から選び、Reloadして現在値を確認します。",
                "Lives、Coin、Play Time、Tutorial、Crownなど必要な値だけ変更します。",
                "Stage一覧からCleared、Seen、Starsを個別または一括で設定します。",
                "Saveを押してTitle、Stage Select、Game Sceneを通し、表示とGate解放を確認します。",
                "試験後はResetまたは対象Save削除で検証前の状態へ戻します。",
            },
            "実際の保存先PathはEditor画面に表示されます。Stage一覧はResources/json/stage_select/stages.jsonから取得します。",
            {
                "編集対象Slotを取り違えていない。",
                "Stars、Clear、Seen、Crownの組み合わせが仕様上成立する。",
                "Titleから読み直しても値が維持される。",
            },
            "Save削除と一括Clear / Not Clearは広い変更です。検証用Slotを決め、通常プレイ用データを誤って消さないでください。",
        },
        {
            "07 運用・データ",
            "ゲーム設定 (Game Settings)",
            "current scene gameplay settings scene-specific parameters",
            "現在Sceneが公開するゲーム固有設定をInspector形式で編集します。表示項目はScene実装によって異なります。",
            "Hierarchy最上部のシステム一覧の末尾にある「ゲーム設定 (Game Settings)」を選びます。",
            {
                "現在シーン名を確認し、表示されるGame Settings項目を読みます。",
                "ゲームルール、Scene固有パラメータ、Debug値など必要な項目だけ変更します。",
                "停止状態と実行状態の両方で反映範囲を確認します。",
                "Scene切替後に値が維持されるか、またはSceneごとに初期化される仕様か確認します。",
                "保存先が表示される項目は専用Save、Scene Objectに属する項目はScene Fileを使います。",
            },
            "保存先は各Scene / Managerの実装によって異なります。画面上のSave表示と現在SceneのJSONを確認してください。",
            {
                "別Sceneの設定を編集していない。",
                "Debug専用値を提出状態へ残していない。",
                "再起動後の初期値が意図どおり。",
            },
            "Game Settingsは全Scene共通の一枚設定ではありません。画面に表示された現在Sceneと保存方法を確認し、曖昧な項目はコード側のScene実装を確認してください。",
        },
        {
            "08 補助",
            "デバッグログ / ステータス / システムプロファイラ",
            "Debug Console FPS CPU GPU profiler time scale logs",
            "Save / Load失敗、実行時Warning、FPS、CPU / GPU時間、時間倍率を確認し、Editor操作後の問題を特定します。",
            "上部メニューの「表示」からデバッグログ、ステータスを開きます。システムプロファイラは「ヘルプ」から開きます。",
            {
                "作業前にDebug ConsoleをClearし、今回の操作で増えたLogだけを追える状態にします。",
                "EditorでSave、Load、Previewを実行し、Info / Warn / Error Filterを切り替えて確認します。",
                "ステータスでFPS、CPU、GPU時間を確認し、時間倍率が1.0になっているか確認します。",
                "負荷調査ではProfilerを開き、単発スパイクと継続負荷を分けて確認します。",
                "問題のEditor、操作、対象名、時刻を控えてから修正します。",
            },
            "ConsoleとProfilerは基本的に診断表示で、シーンデータを保存しません。",
            {
                "ErrorをClearで隠しただけになっていない。",
                "Editor Preview停止後にFPSとGPU時間が元へ戻る。",
                "時間倍率変更のままAnimationを評価していない。",
            },
            "FPSだけでは原因を判断できません。CPU / GPU、同時Particle数、描画Object数、保存・外部Tool実行など、操作条件を揃えて比較してください。",
        },
        {
            "08 補助",
            "外部制作ツール（Editor外）",
            "DDS Cache Builder Shader Texture Baker PowerShell tools README",
            "DDS Cache BuilderやShader Texture Bakerなど、Editor内の専用画面ではなくtools配下のScriptとして動く制作補助を区別して扱います。",
            "EditorのHierarchyには表示されません。tools/README.mdで目的、引数、出力、依存Toolを確認し、必要なScriptをプロジェクトルートから実行します。",
            {
                "DDS Cache Builderは元画像からDDS Cacheを作り、次回以降のTexture読込を軽くします。",
                "Shader Texture Bakerは水、炎、Gateなどで使うMask / Noise TextureをPNGとDDSで生成します。",
                "実行前にREADMEの対象Path、除外Path、必要なtexconv等を確認します。",
                "実行後はManifestとLogを確認し、Editorを再起動または対象Assetを再読込して見た目を確認します。",
            },
            "DDS Cacheは元画像付近または指定Cache先、Baked Shader TextureはResources/texture/BakedShaderへ生成されます。詳細はtools/README.mdを正とします。",
            {
                "元画像を消さず、生成物とSourceの役割を区別している。",
                "Editorの一時Preview画像をCache対象にしていない。",
                "生成後に実ゲームのShaderで正しいChannelが使われる。",
            },
            "これらは現時点でEditor画面ではありません。説明書上でSaveボタンがあるように誤解せず、tools/README.mdとScriptの実行結果を確認してください。",
        },
        {
            "08 補助",
            "最終確認チェックリスト",
            "finish checklist shipping validation build",
            "Editor作業を完了扱いにする前に、保存、参照、見た目、挙動、負荷、Buildを一通り確認します。",
            "対象Editorで調整を終えた後、このページを開いて順番に確認します。",
            {
                "専用EditorのJSONと、配置したScene ObjectのカテゴリJSONを両方保存します。",
                "一度Loadまたはアプリ再起動を行い、保存データから同じ状態を再現します。",
                "Scene ValidatorとEvent Link Graphで参照切れ・ID重複を確認します。",
                "Main Camera、Camera Object、実ゲームの順に見た目と補間を確認します。",
                "開始、途中中断、終了、Loop、連続再生、被弾中など境界条件を確認します。",
                "Debug Console、FPS、CPU / GPU時間を確認し、Development Buildを通します。",
            },
            "各Editorの保存先に加え、Scene FileのDirty表示がすべて意図した状態になっていることを確認します。",
            {
                "再読み込みで設定が消えない。",
                "参照Assetを移動・改名していない。",
                "EditorOnly補助物がSceneへ保存されていない。",
                "本番Camera、解像度、Post Effect、Lightで成立する。",
                "Buildと単体起動が成功する。",
            },
            "Editor Previewで一度見えただけでは完了ではありません。再読込、実ゲーム、終了処理、Buildまで通った状態を完成基準にしてください。",
        },
    };
    return pages;
}

std::string ToLowerAscii(std::string value) {
    for (char& c : value) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x80) {
            c = static_cast<char>(std::tolower(byte));
        }
    }
    return value;
}

bool ContainsSearchText(const char* text, const std::string& query) {
    if (!text || query.empty()) {
        return query.empty();
    }
    return ToLowerAscii(text).find(query) != std::string::npos;
}

bool MatchesPage(const ManualPage& page, const std::string& query) {
    if (query.empty()) {
        return true;
    }
    if (ContainsSearchText(page.category, query) ||
        ContainsSearchText(page.title, query) ||
        ContainsSearchText(page.aliases, query) ||
        ContainsSearchText(page.purpose, query) ||
        ContainsSearchText(page.openGuide, query) ||
        ContainsSearchText(page.saveGuide, query) ||
        ContainsSearchText(page.caution, query)) {
        return true;
    }
    for (const char* step : page.steps) {
        if (ContainsSearchText(step, query)) {
            return true;
        }
    }
    for (const char* check : page.checks) {
        if (ContainsSearchText(check, query)) {
            return true;
        }
    }
    return false;
}

void DrawManualPage(const ManualPage& page) {
    ImGui::TextColored(ImVec4(0.35f, 0.9f, 1.0f, 1.0f), "%s", page.title);
    ImGui::SameLine();
    ImGui::TextDisabled("  %s", page.category);
    ImGui::Spacing();
    Paragraph(page.purpose);

    SectionTitle("開き方 / 事前準備");
    Paragraph(page.openGuide);

    SectionTitle("基本手順");
    for (size_t i = 0; i < page.steps.size(); ++i) {
        ManualStep(static_cast<int>(i + 1), page.steps[i]);
    }

    if (page.special == ManualSpecial::Shortcuts) {
        SectionTitle("操作一覧");
        DrawShortcutTable();
    }
    else if (page.special == ManualSpecial::Materials) {
        SectionTitle("Material Type一覧");
        DrawMaterialTable();
    }

    SectionTitle("保存 / 出力");
    Paragraph(page.saveGuide);

    SectionTitle("完了前の確認");
    for (const char* check : page.checks) {
        ManualBullet(check);
    }

    ImGui::Spacing();
    ManualNote(page.caution);
    ImGui::Spacing();
    ImGui::TextDisabled("検索用: %s", page.aliases);
}

} // namespace
#endif

void EngineManualWindow::Draw() {
#ifdef USE_IMGUI
    if (!isOpen_) {
        return;
    }

    const auto& pages = GetManualPages();
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(pages.size())) {
        selectedIndex_ = 0;
    }

    ImGui::SetNextWindowSize(ImVec2(1180.0f, 760.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_BOOK " エンジン説明書 (Engine Manual)", &isOpen_)) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("EngineManualNavigation", ImVec2(340.0f, 0.0f), true);
    ImGui::TextColored(ImVec4(0.42f, 0.82f, 1.0f, 1.0f), ICON_FA_SEARCH " Editorを検索");
    ImGui::SetNextItemWidth(-34.0f);
    ImGui::InputTextWithHint("##EngineManualSearch", "名前・用途・保存先", searchBuffer_, sizeof(searchBuffer_));
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_TIMES "##ClearManualSearch")) {
        searchBuffer_[0] = '\0';
    }

    const std::string query = ToLowerAscii(searchBuffer_);
    int visibleCount = 0;
    for (const ManualPage& page : pages) {
        if (MatchesPage(page, query)) {
            ++visibleCount;
        }
    }
    if (!query.empty() && visibleCount > 0 && !MatchesPage(pages[selectedIndex_], query)) {
        for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
            if (MatchesPage(pages[i], query)) {
                selectedIndex_ = i;
                break;
            }
        }
    }
    ImGui::TextDisabled("%d / %d ページ", visibleCount, static_cast<int>(pages.size()));
    ImGui::Separator();

    std::string lastCategory;
    for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
        const ManualPage& page = pages[i];
        if (!MatchesPage(page, query)) {
            continue;
        }

        if (lastCategory != page.category) {
            if (!lastCategory.empty()) {
                ImGui::Spacing();
            }
            ImGui::TextColored(ImVec4(0.65f, 0.72f, 0.8f, 1.0f), "%s", page.category);
            lastCategory = page.category;
        }

        ImGui::PushID(i);
        if (ImGui::Selectable(page.title, selectedIndex_ == i)) {
            selectedIndex_ = i;
        }
        ImGui::PopID();
    }

    if (visibleCount == 0) {
        ImGui::Spacing();
        ImGui::TextWrapped("該当する説明がありません。日本語名、英語名、用途、保存先の一部で検索してください。");
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("EngineManualBody", ImVec2(0.0f, 0.0f), true);
    DrawManualPage(pages[selectedIndex_]);
    ImGui::EndChild();

    ImGui::End();
#endif
}
