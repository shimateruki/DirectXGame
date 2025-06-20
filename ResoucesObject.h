#pragma once  
#include <d3d12.h>

class ResoucesObject  
{  
public:  
	ResoucesObject(ID3D12Resource* resources) 
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
	ID3D12Resource* resouces_ = nullptr; 
};
