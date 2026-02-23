#include "ImGuiManager.h"
#include "SRVManager.h"

ImGuiManager* ImGuiManager::GetInstance() {
    static ImGuiManager instance;
    return &instance;
}

void ImGuiManager::Initialize(WinApp* winApp, DirectXCommon* dxCommon) {
    (void)winApp;
    (void)dxCommon;
#ifdef USE_IMGUI
    dxCommon_ = dxCommon;

    // 1. ImGuiのコンテキストを生成
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // ドッキング有効化 (ウィンドウ同士をくっつける)
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // マルチビューポート有効化 (ウィンドウ外に出す)

    // ImGuiのスタイルを設定
    ImGui::StyleColorsDark();


    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;              // ウィンドウの角丸をなくす
        style.Colors[ImGuiCol_WindowBg].w = 1.0f; // 背景を完全に不透明にする
    }
    // --- Unity Dark風のスタイル調整 ---

    // 角丸をなくしてフラットにする
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.PopupRounding = 0.0f;
    style.TabRounding = 2.0f;

    // 枠線の太さ
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    // Unity Dark風の配色 (グレー基調)
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f); // 背景
    style.Colors[ImGuiCol_Header] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f); // 選択項目
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f); // タイトルバー
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f); // 入力項目背景
    style.Colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f); // タブ
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.30f, 0.50f, 0.80f, 0.70f); // ドッキング時の青い影

    // フォント読み込み（パスは環境に合わせて確認してください）
    io.Fonts->AddFontFromFileTTF(
        "Resources/sprite/meiryo.ttc",
        18.0f,
        nullptr,
        io.Fonts->GetGlyphRangesJapanese()
    );

    // プラットフォームとレンダラーのバックエンドを初期化
    ImGui_ImplWin32_Init(winApp->GetHwnd());

    // --- (以下、SRV周りの設定は元のまま変更なしでOK) ---
    ID3D12DescriptorHeap* srvDescriptorHeap = SRVManager::GetInstance()->GetDescriptorHeap();
    UINT incrementSize = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

    cpuHandle.ptr += incrementSize;
    gpuHandle.ptr += incrementSize;

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = dxCommon_->GetDevice();
    initInfo.CommandQueue = dxCommon_->GetCommandQueue();
    initInfo.NumFramesInFlight = 2;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    initInfo.SrvDescriptorHeap = srvDescriptorHeap;
    initInfo.LegacySingleSrvCpuDescriptor = cpuHandle;
    initInfo.LegacySingleSrvGpuDescriptor = gpuHandle;
    initInfo.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    initInfo.UserData = dxCommon_->GetDevice();
    initInfo.SrvDescriptorAllocFn = nullptr;
    initInfo.SrvDescriptorFreeFn = nullptr;

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