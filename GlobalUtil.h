#pragma once
#include <Windows.h>
#include <d3dcompiler.h> // for D3DCOMPILE_DEBUG & D3DCOMPILE_SKIP_OPTIMIZATION
#include <string>
#include <comdef.h> // for _com_error
#include <wrl.h>
#include <d3dx12.h>

using Microsoft::WRL::ComPtr;

class DxException
{
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber)
		: ErrorCode(hr), FunctionName(functionName), Filename(filename), LineNumber(lineNumber) {}
	std::wstring ToString() const
	{
		_com_error err(ErrorCode);
		std::wstring msg = err.ErrorMessage();

		return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
	}

	HRESULT ErrorCode = S_OK;
	std::wstring FunctionName;
	std::wstring Filename;
	int LineNumber = -1;
protected:
private:
};

inline std::wstring AnsiToWString(const std::string& str)
{
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

#ifndef ThrowIfFailed
#define ThrowIfFailed(x) \
{ \
	HRESULT hr__ = (x);\
	std::wstring wfn = AnsiToWString(__FILE__);\
	if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); }\
\
}
#endif

Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const void* data,
	UINT64 byteSize,
	Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
	ComPtr<ID3D12Resource> defaultBuffer;

	// 创建默认缓冲区资源
	CD3DX12_HEAP_PROPERTIES defaultHeapProp(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC defaultBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
	ThrowIfFailed(device->CreateCommittedResource(
		&defaultHeapProp, D3D12_HEAP_FLAG_NONE,
		&defaultBufferDesc, D3D12_RESOURCE_STATE_COMMON,
		nullptr, IID_PPV_ARGS(defaultBuffer.ReleaseAndGetAddressOf())
	));

	// 需要创建上传堆，然后CPU把资源拷贝到上传堆，上传堆再拷贝到默认堆
	// 这里和之前的DepthStencil还有SwapChainBuffer都不一样的是我们需要填充数据，而DS和SCB只需要调用API清理每帧数据
	CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
	ThrowIfFailed(device->CreateCommittedResource(
		&uploadHeapProp, D3D12_HEAP_FLAG_NONE,
		&uploadBufferDesc, D3D12_RESOURCE_STATE_COMMON,
		nullptr, IID_PPV_ARGS(uploadBuffer.ReleaseAndGetAddressOf())
	));

	// 复制前需要描述我们想复制到默认缓冲区的资源（真麻烦啊 :(
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = data;
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = byteSize;

	// 将数据复制到默认缓冲区
	// 先调用 UpdateSubresources 函数从CPU内存拷贝到Upload堆
	// 在调用 CopySubresourceRegion 函数把Upload堆中的东西拷贝到Default堆中  （这一步似乎已经不再需要
	CD3DX12_RESOURCE_BARRIER defaultBufferFromCommonToCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->ResourceBarrier(1, &defaultBufferFromCommonToCopyDest);
	UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(),
		0, 0, 1, &subResourceData);
	CD3DX12_RESOURCE_BARRIER defaultBufferFromCopyDestToRead = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &defaultBufferFromCopyDestToRead);

	return defaultBuffer;

}


UINT CalcConstantBufferByteSize(UINT byteSize)
{
	return (byteSize + 255) & ~255;
}


ComPtr<ID3DBlob> CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	using namespace DirectX;
	// 处于调试模式则启动调试标志
	UINT compileFlags = 0;

#if defined(DEBUG) || defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(filename.c_str(), defines,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0,
		&byteCode, &errors);

	if (errors != nullptr)
	{
		OutputDebugStringA((char*)errors->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	return byteCode;
}