#pragma once
// 担当：破棄時にDirect3D資源の残存を出力する診断用オブジェクト。
// 関連：DirectXCommon（診断対象となるGPU資源）
// 主な入口：~D3DResourceLeakChecKer
class D3DResourceLeakChecKer
{
public:
	/// <summary>スコープ終了時に生存リソースを報告する。</summary>
	~D3DResourceLeakChecKer();
};

