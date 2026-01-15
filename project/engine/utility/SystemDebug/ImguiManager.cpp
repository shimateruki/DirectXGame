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

    // ドッキング機能を有効にする
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // ドッキング有効

    // ImGuiのスタイルを設定
    ImGui::StyleColorsDark();
    io.Fonts->AddFontFromFileTTF(
        "Resources/sprite/meiryo.ttc",   // フォントファイルのパス
        18.0f,                                // フォントサイズ
        nullptr,
        io.Fonts->GetGlyphRangesJapanese()    // 日本語の範囲（ひらがな・カタカナ・漢字）
    );
    // プラットフォームとレンダラーのバックエンドを初期化
    ImGui_ImplWin32_Init(winApp->GetHwnd());

    // SRVManagerからSRV用のデスクリプタヒープを取得
    ID3D12DescriptorHeap* srvDescriptorHeap = SRVManager::GetInstance()->GetDescriptorHeap();

    // デスクリプタ1個分のサイズ（メモリ上の階段1段分の高さ）を取得
    UINT incrementSize = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 先頭ハンドルを取得
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();



    // ポインタを「1個分」ずらす（SRVの1番目を使う場合）
    cpuHandle.ptr += incrementSize;
    gpuHandle.ptr += incrementSize;

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = dxCommon_->GetDevice();
    initInfo.CommandQueue = dxCommon_->GetCommandQueue();
    initInfo.NumFramesInFlight = 3;
    initInfo.RTVFormat = dxCommon_->GetRTVFormat();
    initInfo.SrvDescriptorHeap = srvDescriptorHeap;

    initInfo.LegacySingleSrvCpuDescriptor = cpuHandle;
    initInfo.LegacySingleSrvGpuDescriptor = gpuHandle;
    initInfo.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    initInfo.UserData = dxCommon_->GetDevice();

    initInfo.SrvDescriptorAllocFn = nullptr;
    initInfo.SrvDescriptorFreeFn = nullptr;

    // 初期化
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
    // 1. まず「描画内容」を確定させる（これで FrameCountEnded が更新されます）
    ImGui::Render();

    // 2. DX12のコマンドリストに記録
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    assert(commandList != nullptr);
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