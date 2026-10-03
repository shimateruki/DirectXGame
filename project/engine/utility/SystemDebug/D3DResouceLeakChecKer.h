#pragma once
/// <summary>破棄時にDirect3Dの生存リソースを診断するスコープオブジェクト。</summary>
class D3DResourceLeakChecKer
{
public:
	/// <summary>スコープ終了時に生存リソースを報告する。</summary>
	~D3DResourceLeakChecKer();
};

