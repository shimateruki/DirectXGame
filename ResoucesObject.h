#pragma once  
#include <d3d12.h>
#include <wrl.h>
class ResoucesObject  
{  
public:  
	ResoucesObject(Microsoft::WRL::ComPtr<ID3D12Resource> resources)
		:resouces_(resources)
	{}
	~ResoucesObject()
	{
		if (resouces_)
		{
			resouces_->Release();
		}
	}
private:
	Microsoft::WRL::ComPtr<ID3D12Resource> resouces_ = nullptr;
};
