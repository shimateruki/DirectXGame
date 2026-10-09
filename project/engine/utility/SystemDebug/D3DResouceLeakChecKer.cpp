#include "D3DResouceLeakChecKer.h"
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
D3DResourceLeakChecKer::~D3DResourceLeakChecKer()
{
	// DXGI のデバッグインターフェースが利用できる環境だけ、終了時点の生存リソースを報告する。
	Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
	{
		// ReportLiveObjects を呼び出し、リークしているオブジェクトをデバッグウィンドウに出力
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
	}
}
