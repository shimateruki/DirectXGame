#include "ImGuiManager.h"
#include "SRVManager.h"
#include "IconsFontAwesome5.h"
ImGuiManager* ImGuiManager::GetInstance() {
    static ImGuiManager instance;
    return &instance;
}

void ImGuiManager::Initialize(WinApp* winApp, DirectXCommon* dxCommon) {
    (void)winApp;
#ifdef USE_IMGUI
    dxCommon_ = dxCommon;

    // 1. ImGuiのコンテキストを生成
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // ドッキング有効化

    // 黒を基調にし、背景・入力・選択の明度差で操作状態を見分けやすくします。
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.89f, 0.92f, 0.96f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.57f, 0.63f, 0.71f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.065f, 0.073f, 0.087f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.080f, 0.089f, 0.106f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.105f, 0.118f, 0.140f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.19f, 0.22f, 0.26f, 0.95f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.24f, 0.29f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.30f, 0.39f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.050f, 0.057f, 0.068f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.095f, 0.113f, 0.139f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.080f, 0.091f, 0.109f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.060f, 0.069f, 0.083f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.26f, 0.31f, 0.37f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.41f, 0.49f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.39f, 0.72f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.65f, 0.93f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.49f, 0.76f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.24f, 0.34f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.37f, 0.54f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.27f, 0.45f, 0.66f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.15f, 0.19f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.33f, 0.44f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.42f, 0.60f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.19f, 0.22f, 0.26f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.085f, 0.105f, 0.132f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.21f, 0.34f, 0.47f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.16f, 0.27f, 0.39f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.075f, 0.086f, 0.103f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.13f, 0.19f, 0.26f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.15f, 0.19f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.11f, 0.13f, 0.16f, 0.75f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.28f, 0.55f, 0.85f, 0.70f);

    // =================================================================
    // 🎨 フォントとアイコンの「合体（Merge）」読み込み
    // =================================================================
    
    // ① まずはメインの日本語フォントを読み込む
    float fontSize = 18.0f;
    io.Fonts->AddFontFromFileTTF(
        "Resources/sprite/meiryo.ttc",
        fontSize,
        nullptr,
        io.Fonts->GetGlyphRangesJapanese()
    );

    // ② 次に読み込むアイコンフォントを「MergeMode」で合体させる設定にする
    ImFontConfig config;
    config.MergeMode = true;       // ★ここが肝！前のフォントに上書き合体する
    config.PixelSnapH = true;
    config.GlyphMinAdvanceX = fontSize; // アイコンの幅をフォントサイズに合わせる

    // ③ アイコンの文字コード範囲を指定（IconsFontAwesome5.hで定義されている範囲）
    static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };

    // ④ アイコンフォントを読み込んで合体！
    io.Fonts->AddFontFromFileTTF(
        "Resources/font/fa-solid-900.ttf",
        fontSize,
        &config,
        icon_ranges
    );


    // プラットフォームのバックエンドを初期化
    ImGui_ImplWin32_Init(winApp->GetHwnd());

    // 2. DX12レンダラーの初期化 (マルチテクスチャ対応版)
    ID3D12DescriptorHeap* srvDescriptorHeap = SRVManager::GetInstance()->GetDescriptorHeap();

    auto AllocSrv = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) {
        uint32_t index = SRVManager::GetInstance()->Allocate();
        *out_cpu_handle = SRVManager::GetInstance()->GetCPUDescriptorHandle(index);
        *out_gpu_handle = SRVManager::GetInstance()->GetGPUDescriptorHandle(index);
    };

    auto FreeSrv = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {
    
    };

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = dxCommon_->GetDevice();
    initInfo.CommandQueue = dxCommon_->GetCommandQueue();
    initInfo.NumFramesInFlight = 2;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    initInfo.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    initInfo.SrvDescriptorHeap = srvDescriptorHeap;

    initInfo.LegacySingleSrvCpuDescriptor = { 0 };
    initInfo.LegacySingleSrvGpuDescriptor = { 0 };
    initInfo.SrvDescriptorAllocFn = AllocSrv;
    initInfo.SrvDescriptorFreeFn = FreeSrv;

    ImGui_ImplDX12_Init(&initInfo);
#endif
}

void ImGuiManager::Finalize() {
#ifdef USE_IMGUI
    // バックエンドをシャットダウン
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    // ImGuiのコンテキストを破棄
    ImGui::DestroyContext();
#endif
}

void ImGuiManager::BeginFrame() {
#ifdef USE_IMGUI
    // フレームの開始をImGuiに伝える
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
#endif
}

void ImGuiManager::Draw() {
#ifdef USE_IMGUI
    // 1. まず「描画内容」を確定させる
    ImGui::Render();

    // 2. DX12のコマンドリストに記録
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    assert(commandList != nullptr);
    SRVManager::GetInstance()->SetDescriptorHeaps(commandList);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif
}

void ImGuiManager::EndFrame() {
#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        // 1. 今のコンテキストをバックアップ
        ImGuiContext* backup_current_context = ImGui::GetCurrentContext();

        // 2. ウィンドウの位置などを更新
        ImGui::UpdatePlatformWindows();

        // 3. サブウィンドウを描画
        // ※最新版では、第1引数と第2引数は nullptr で内部解決させ、
        //   メインのコマンドリストに記録させるのが最も安定します。
        ImGui::RenderPlatformWindowsDefault(nullptr, nullptr);

        // 4. コンテキストを復元
        ImGui::SetCurrentContext(backup_current_context);
    }
#endif
}