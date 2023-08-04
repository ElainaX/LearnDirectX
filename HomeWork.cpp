#include <Windows.h>
#include <windowsx.h> // for GET_X_LPARAM & GET_Y_LPARAM 在消息处理函数中
#include <stdexcept>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <memory>

#include <dxgi.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl.h> // for wrl::ComPtr
#include <DirectXColors.h>

#include "d3dx12.h"

#include "GameTimer.h"
#include "GlobalUtil.h"
#include "VertexData.h"
#include "UploadBuffer.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dCompiler.lib")


#ifndef ThrowIfFailed
#define ThrowIfFailed(x) \
{ \
	HRESULT hr__ = (x);\
	std::wstring wfn = AnsiToWString(__FILE__);\
	if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); }\
\
}
#endif

using Microsoft::WRL::ComPtr;
using namespace DirectX;


/*===================== global varable begin ===============================*/

HINSTANCE mhInstance = 0;
HWND mhMainWnd = 0;
std::wstring mMainWndName = L"Elaina Game";

UINT mClientWidth = 1280;
UINT mClientHeight = 720;
float mAspectRatio = (float)mClientWidth / (float)mClientHeight;

bool mMinimized = false;
bool mMaximized = false;
bool mResizing = false;

DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
DXGI_FORMAT mDepthStencilBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
UINT m4xMsaaQualityLevels = 0;
bool m4xMsaaStatus = false;
const int mSwapChainCount = 2;
int mCurrBackBuffer = 0;

size_t mRtvDescriptorSize = 0;
size_t mDsvDescriptorSize = 0;
size_t mCbvSrvUavDescriptorSize = 0;

UINT mCurrentFence = 0;

bool mAppPaused = false;
GameTimer mTimer;

ComPtr<IDXGIFactory4> m_Factory;
ComPtr<IDXGIAdapter> m_Adapter;
ComPtr<ID3D12Device> m_Device;

ComPtr<ID3D12GraphicsCommandList> mCmdList;
ComPtr<ID3D12CommandAllocator> mCmdAlloc;
ComPtr<ID3D12CommandQueue> mCmdQueue;

ComPtr<ID3D12Fence> mFence;
ComPtr<IDXGISwapChain> mSwapChain;
ComPtr<ID3D12Resource> mSwapChainBuffer[mSwapChainCount];
ComPtr<ID3D12Resource> mDepthStencilBuffer;
ComPtr<ID3D12RootSignature> mRootSignature;
ComPtr<ID3D12PipelineState> mPSO;

ComPtr<ID3D12DescriptorHeap> mRtvHeap; // 装SwapChainBuffer描述符
ComPtr<ID3D12DescriptorHeap> mDsvHeap; // 装深度/模板描述符
ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr; // constBufferHeap

// 编译代码
ComPtr<ID3DBlob> mvsByteCode = nullptr;
ComPtr<ID3DBlob> mpsByteCode = nullptr;

// 裁剪矩形和视口
D3D12_RECT mScissorRect;
D3D12_VIEWPORT mScreenViewport;

// Vertex Objects
std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
//std::shared_ptr<MeshGeometry> mBoxGeo;

std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;

XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
XMFLOAT4X4 mView = MathHelper::Identity4x4();
XMFLOAT4X4 mProj = MathHelper::Identity4x4();

float mTheta = 1.5f * XM_PI;
float mPhi = XM_PIDIV4;
float mRadius = 5.0f;

POINT mLastMousePos;

// Util
D3D_FEATURE_LEVEL featureLevels[3] =
{
	D3D_FEATURE_LEVEL_12_1,
	D3D_FEATURE_LEVEL_12_0,
	D3D_FEATURE_LEVEL_11_1
};

/*===================== global varable end ===============================*/


/*...................... 作业变量和函数 ...................................*/
struct VPosData
{
	XMFLOAT3 Pos;
};

struct VColorData
{
	XMFLOAT4 Color;
};


std::shared_ptr<MeshGeometry> mBoxGeoPos;
std::shared_ptr<MeshGeometry> mBoxGeoColor;

/*++++++++++++++++++++ function declaration begin +++++++++++++++++++++++++++*/



LRESULT CALLBACK
MsgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
HWND InitWindow(HINSTANCE hInstance, int showMode);


void Check4xMsaaQualityLevels();
D3D_FEATURE_LEVEL CheckFeatureLevelSupport();

void CreateRtvAndDsvDescriptorHeaps();
ID3D12Resource* CurrentBackBuffer()
{
	return mSwapChainBuffer[mCurrBackBuffer].Get();
}
D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView();
D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView();

void CreateCommandObjects();
void CreateFences();
void CreateSwapChainObjects();
void BuildShadersAndInputLayout();
void BuildDescriptorHeap();
void BuildRootSignature();
void BuildPSO();
void BuildBoxGeometry();

void FlushCommandQueue();
void OnResize();
void CalculateFrameStats();

// Important
void Update(const GameTimer& gt)
{
	// 更新MVP
	float x = mRadius * sinf(mPhi) * cosf(mTheta);
	float z = mRadius * sinf(mPhi) * sinf(mTheta);
	float y = mRadius * cosf(mPhi);

	// View
	XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);

	XMMATRIX world = XMLoadFloat4x4(&mWorld);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX worldViewProj = world * view * proj;

	ObjectConstants objConstants;
	XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
	mObjectCB->CopyData(0, objConstants);
}

void Draw(const GameTimer& gt)
{
	// 绘制场景前不能忘记重置命令内存
	// 我们会在每帧的最后同步命令队列，因此可以确定命令分配器已经完成
	ThrowIfFailed(mCmdAlloc->Reset());

	ThrowIfFailed(mCmdList->Reset(mCmdAlloc.Get(), mPSO.Get()));

	CD3DX12_RESOURCE_BARRIER CurrBufferFromPresentToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCmdList->ResourceBarrier(1, &CurrBufferFromPresentToRenderTarget);
	
	mCmdList->RSSetViewports(1, &mScreenViewport);
	mCmdList->RSSetScissorRects(1, &mScissorRect);

	// 清除后台缓冲区和深度缓冲区视图?
	mCmdList->ClearRenderTargetView(
		CurrentBackBufferView(),
		DirectX::Colors::LightSteelBlue, 0, nullptr
	);
	mCmdList->ClearDepthStencilView(
		DepthStencilView(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr
	);

	// 指定将要渲染的缓冲区
	auto CBBV = CurrentBackBufferView();
	auto DSV = DepthStencilView();
	mCmdList->OMSetRenderTargets(1, &CBBV, true, &DSV);

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCmdList->SetGraphicsRootSignature(mRootSignature.Get());

	mCmdList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

	// VertexBuffer & IndexBuffer
	D3D12_VERTEX_BUFFER_VIEW vbvPos = mBoxGeoPos->VertexBufferView();
	mCmdList->IASetVertexBuffers(0, 1, &vbvPos);
	D3D12_INDEX_BUFFER_VIEW ibvPos = mBoxGeoPos->IndexBufferView();
	mCmdList->IASetIndexBuffer(&ibvPos);
	mCmdList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D12_VERTEX_BUFFER_VIEW vbvColor = mBoxGeoColor->VertexBufferView();
	mCmdList->IASetVertexBuffers(1, 1, &vbvColor);
	D3D12_INDEX_BUFFER_VIEW ibvColor = mBoxGeoColor->IndexBufferView();
	mCmdList->IASetIndexBuffer(&ibvColor);
	mCmdList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


	mCmdList->DrawIndexedInstanced(mBoxGeoPos->DrawArgs["boxPos"].IndexCount, 1,
		0, 0, 0);
	mCmdList->DrawIndexedInstanced(mBoxGeoPos->DrawArgs["boxColor"].IndexCount, 1,
		0, 0, 0);

	// 将资源从RenderTarget转化成呈现
	CD3DX12_RESOURCE_BARRIER CurrBufferFromRenderTargetToPresent = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	mCmdList->ResourceBarrier(1, &CurrBufferFromRenderTargetToPresent);

	ThrowIfFailed(mCmdList->Close());

	// 加入命令队列
	ID3D12CommandList* cmdsLists[] = { mCmdList.Get() };
	mCmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// 交换后台缓冲区和前台缓冲区
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % mSwapChainCount;

	// 别忘了同步CPU
	FlushCommandQueue();
}

void BuildDescriptorHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.NodeMask = 0;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NumDescriptors = 1;
	ThrowIfFailed(m_Device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));
}

void BuildConstantBuffers()
{
	mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(m_Device.Get(), 1, true);

	UINT objCBByteSize = CalcConstantBufferByteSize(sizeof(ObjectConstants));
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
	UINT boxCbufIndex = 0;
	cbAddress += boxCbufIndex * objCBByteSize;
	
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbViewDesc;
	cbViewDesc.BufferLocation = cbAddress;
	cbViewDesc.SizeInBytes = CalcConstantBufferByteSize(sizeof(ObjectConstants));
	
	m_Device->CreateConstantBufferView(&cbViewDesc, mCbvHeap->GetCPUDescriptorHandleForHeapStart());

}

void BuildRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[1];

	CD3DX12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

	// 创建根签名和根参数
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// 创建仅含一个槽位的根签名（槽位指向一个仅由单个常量缓冲区组成的描述符区域）
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(m_Device->CreateRootSignature(
		0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)
	));
 
}

int Run();

bool InitDirect3D()
{
	// 启动调试层
#if defined(_DEBUG) || defined(DEBUG)
{
	ComPtr<ID3D12Debug> debugController;
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())));
	debugController->EnableDebugLayer();
}
#endif

	// 创建factory, adapter, device
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(m_Factory.GetAddressOf())));

	for (UINT AdapterIdx = 0; m_Factory->EnumAdapters(AdapterIdx, &m_Adapter) != DXGI_ERROR_NOT_FOUND; ++AdapterIdx)
	{
		DXGI_ADAPTER_DESC AdapterDesc;
		m_Adapter->GetDesc(&AdapterDesc);
		if (SUCCEEDED(D3D12CreateDevice(m_Adapter.Get(), D3D_FEATURE_LEVEL_11_1, IID_PPV_ARGS(&m_Device))))
		{
			OutputDebugString(AdapterDesc.Description + L'\n');
			break;
		}
	}
	CreateFences();
	Check4xMsaaQualityLevels();
	CreateCommandObjects();
	
	// SwapChain资源
	CreateSwapChainObjects();
	
	// Rtv和Dsv的描述符堆
	CreateRtvAndDsvDescriptorHeaps();

	// 刷新一次SwapChain和DepthStencil并创建描述符（视图）
	OnResize();


	/*---------------- 从这里开始是独立程序的代码 ----------------*/ 
	// 创建着色器与输入布局
	ThrowIfFailed(mCmdList->Reset(mCmdAlloc.Get(), nullptr));

	BuildDescriptorHeap();
	BuildConstantBuffers();
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildBoxGeometry();
	BuildPSO();

	ThrowIfFailed(mCmdList->Close());
	ID3D12CommandList* cmdsLists[] = { mCmdList.Get() };
	mCmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();

	/*---------------- 从这里结束独立程序的代码 ------------------*/
	

	return true;
}

/*++++++++++++++++++++ function declaration end +++++++++++++++++++++++++++*/



/*-------------------------- main begin ----------------------------------------------------------*/

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, char* cmdLine, int showMode)
{
	mhInstance = hInstance;
	mhMainWnd = InitWindow(hInstance, showMode);
	if (InitDirect3D())
	{
		if (CheckFeatureLevelSupport() == D3D_FEATURE_LEVEL_12_1)
			OutputDebugString(L"\nSupport D3D_FEATURE_LEVEL_12_1\n");
		return Run();
	}

	system("pause");
}

/*-------------------------- main end ----------------------------------------------------------*/

void OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void OnMouseMove(WPARAM btnState, int x, int y)
{
	// 如果按了左键
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mTheta += dx;
		mPhi += dy;

		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	// 如果按了右键
	else if((btnState & MK_RBUTTON) != 0)
	{
		float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

		mRadius += dx - dy;

		mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;

}

void BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	psoDesc.pRootSignature = mRootSignature.Get();
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
		mvsByteCode->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
		mpsByteCode->GetBufferSize()
	};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = mBackBufferFormat;
	psoDesc.SampleDesc.Count = m4xMsaaStatus ? 4 : 1;
	psoDesc.SampleDesc.Quality = m4xMsaaStatus ? (m4xMsaaQualityLevels - 1) : 0;
	psoDesc.DSVFormat = mDepthStencilBufferFormat;
	ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

void BuildShadersAndInputLayout()
{
	HRESULT hr = S_OK;

	mvsByteCode = CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
	mpsByteCode = CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");

	mInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}

void BuildBoxGeometry()
{
	std::array<VPosData, 8> vertPos =
	{
		VPosData({ XMFLOAT3(-1.0f, -1.0f, -1.0f)}),
		VPosData({ XMFLOAT3(-1.0f, +1.0f, -1.0f)}),
		VPosData({ XMFLOAT3(+1.0f, +1.0f, -1.0f)}),
		VPosData({ XMFLOAT3(+1.0f, -1.0f, -1.0f)}),
		VPosData({ XMFLOAT3(-1.0f, -1.0f, +1.0f)}),
		VPosData({ XMFLOAT3(-1.0f, +1.0f, +1.0f)}),
		VPosData({ XMFLOAT3(+1.0f, +1.0f, +1.0f)}),
		VPosData({ XMFLOAT3(+1.0f, -1.0f, +1.0f)})
	};

	std::array<VColorData, 8> vertColor =
	{
		VColorData{XMFLOAT4(Colors::White) },
		VColorData{XMFLOAT4(Colors::Black) },
		VColorData{XMFLOAT4(Colors::Red)},
		VColorData{XMFLOAT4(Colors::Green)},
		VColorData{XMFLOAT4(Colors::Blue)},
		VColorData{XMFLOAT4(Colors::Yellow)},
		VColorData{XMFLOAT4(Colors::Cyan)},
		VColorData{XMFLOAT4(Colors::Magenta)}
	};

	std::array<std::uint16_t, 36> indices =
	{
		// front face
		0, 1, 2,
		0, 2, 3,

		// back face
		4, 6, 5,
		4, 7, 6,

		// left face
		4, 5, 1,
		4, 1, 0,

		// right face
		3, 2, 6,
		3, 6, 7,

		// top face
		1, 5, 6,
		1, 6, 2,

		// bottom face
		4, 0, 3,
		4, 3, 7
	};

	const UINT vbPosByteSize = (UINT)vertPos.size() * sizeof(VPosData);
	const UINT vbColorByteSize = (UINT)vertColor.size() * sizeof(VColorData);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	mBoxGeoPos = std::make_shared<MeshGeometry>();
	mBoxGeoColor = std::make_shared<MeshGeometry>();
	mBoxGeoPos->Name = "boxGeoPos";
	mBoxGeoColor->Name = "boxGeoColor";

	ThrowIfFailed(D3DCreateBlob(vbPosByteSize, &mBoxGeoPos->VertexBufferCPU));
	CopyMemory(mBoxGeoPos->VertexBufferCPU->GetBufferPointer(), vertPos.data(), vbPosByteSize);
	ThrowIfFailed(D3DCreateBlob(vbColorByteSize, &mBoxGeoColor->VertexBufferCPU));
	CopyMemory(mBoxGeoColor->VertexBufferCPU->GetBufferPointer(), vertColor.data(), vbColorByteSize);


	ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeoPos->IndexBufferCPU));
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeoColor->IndexBufferCPU));
	CopyMemory(mBoxGeoPos->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);
	CopyMemory(mBoxGeoColor->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	mBoxGeoPos->VertexBufferGPU = CreateDefaultBuffer(m_Device.Get(), mCmdList.Get(), vertPos.data(), vbPosByteSize, mBoxGeoPos->VertexBufferUploader);
	mBoxGeoPos->IndexBufferGPU = CreateDefaultBuffer(m_Device.Get(), mCmdList.Get(), indices.data(), ibByteSize, mBoxGeoPos->IndexBufferUploader);
	
	mBoxGeoColor->VertexBufferGPU = CreateDefaultBuffer(m_Device.Get(), mCmdList.Get(), vertColor.data(), vbColorByteSize, mBoxGeoColor->VertexBufferUploader);
	mBoxGeoColor->IndexBufferGPU = CreateDefaultBuffer(m_Device.Get(), mCmdList.Get(), indices.data(), ibByteSize, mBoxGeoColor->IndexBufferUploader);
	
	mBoxGeoPos->VertexBufferByteSize = vbPosByteSize;
	mBoxGeoPos->IndexBufferByteSize = ibByteSize;
	mBoxGeoPos->IndexFormat = DXGI_FORMAT_R16_UINT;
	mBoxGeoPos->VertexByteStride = sizeof(VPosData);

	mBoxGeoColor->VertexBufferByteSize = vbColorByteSize;
	mBoxGeoColor->IndexBufferByteSize = ibByteSize;
	mBoxGeoColor->IndexFormat = DXGI_FORMAT_R16_UINT;
	mBoxGeoColor->VertexByteStride = sizeof(VColorData);

	SubmeshGeometry submesh;
	submesh.IndexCount = indices.size();
	submesh.BaseVertexLocation = 0;
	submesh.StartIndexLocation = 0;

	mBoxGeoPos->DrawArgs["boxPos"] = submesh;
	mBoxGeoColor->DrawArgs["boxColor"] = submesh;
}

int Run()
{
	MSG msg = {};
	mTimer.Reset();

	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			// Tick() the game
			mTimer.Tick();
			if (!mAppPaused)
			{
				// update Data and Draw Frame
				CalculateFrameStats();
				Update(mTimer);
				Draw(mTimer);
			}
			else
			{
				Sleep(100);
			}
		}
	}
	FlushCommandQueue();

	return msg.wParam;
}
void OnResize()
{
	// 刷新队列，重置命令
	FlushCommandQueue();
	ThrowIfFailed(mCmdList->Reset(mCmdAlloc.Get(), nullptr));

	// Clear Buffers
	// Release the previous resources we will be recreating.
	for (int i = 0; i < mSwapChainCount; ++i)
		mSwapChainBuffer[i].Reset();
	mDepthStencilBuffer.Reset();

	// Resize Buffer
	ThrowIfFailed(mSwapChain->ResizeBuffers(2, mClientWidth, mClientHeight, mBackBufferFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	mCurrBackBuffer = 0;

	// Render Target View
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < mSwapChainCount; ++i)
	{
		// 获得交换链内的第i个缓冲区
		ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));

		// 为此缓冲区创建一个RTV
		m_Device->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);

		// 移动到下一个描述符堆缓冲区
		rtvHeapHandle.Offset(1, mRtvDescriptorSize);
	}

	// Depth_Stencil Resource & View

	// 创建Depth_Stencil资源
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = mClientWidth;
	depthStencilDesc.Height = mClientHeight;
	depthStencilDesc.MipLevels = 0;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	depthStencilDesc.Format = mDepthStencilBufferFormat;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.SampleDesc.Count = m4xMsaaStatus ? 4 : 1;
	depthStencilDesc.SampleDesc.Quality = m4xMsaaStatus ? (m4xMsaaQualityLevels - 1) : 0;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = mDepthStencilBufferFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	CD3DX12_HEAP_PROPERTIES DepthStencilHeapProp(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(m_Device->CreateCommittedResource(
		&DepthStencilHeapProp, D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc, D3D12_RESOURCE_STATE_COMMON,
		&optClear, IID_PPV_ARGS(mDepthStencilBuffer.ReleaseAndGetAddressOf())
	));


	// 创建Depth_Stencil视图到描述符堆
	// 第二个参数表示资源格式，由于我们创建资源的时候已经指定了格式
	// depthStencilDesc.Format = mDepthStencilBufferFormat;
	// 所以填nullptr表示以创建时的格式为第一个mipmap层级创建视图
	m_Device->CreateDepthStencilView(
		mDepthStencilBuffer.Get(), nullptr, DepthStencilView()
	);

	// 转换资源状态
	CD3DX12_RESOURCE_BARRIER InitDepthStencilFromCommonToWrite =
		CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	mCmdList->ResourceBarrier(1,
		&InitDepthStencilFromCommonToWrite
	);

	ThrowIfFailed(mCmdList->Close());
	ID3D12CommandList* cmdsLists[] = { mCmdList.Get() };
	mCmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();

	// 设置视口
	mScreenViewport.TopLeftX = 0.0f;
	mScreenViewport.TopLeftY = 0.0f;
	mScreenViewport.Width = mClientWidth;
	mScreenViewport.Height = mClientHeight;
	mScreenViewport.MinDepth = 0.0f;
	mScreenViewport.MaxDepth = 1.0f;
	//mCmdList->RSSetViewports(1, &mScreenViewport);

	// 设置裁剪矩形
	mScissorRect.left = 0;
	mScissorRect.top = 0;
	mScissorRect.right = mClientWidth;
	mScissorRect.bottom = mClientHeight;
	//mCmdList->RSSetScissorRects(1, &mScissorRect);


	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, mAspectRatio, 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}
LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
	case WM_ACTIVATE:
		if (LOWORD(wparam) == WA_INACTIVE)
		{
			mAppPaused = true;
			mTimer.Stop();
		}
		else
		{
			mAppPaused = false;
			mTimer.Start();
		}
		return 0;

	case WM_SIZE:
		mClientWidth = LOWORD(lparam);
		mClientHeight = HIWORD(lparam);
		if (m_Device)
		{
			if (wparam == SIZE_MINIMIZED)
			{
				mAppPaused = true;
				mMinimized = true;
				mMaximized = false;
			}
			else if (wparam == SIZE_MAXIMIZED)
			{
				mAppPaused = false;
				mMinimized = false;
				mMaximized = true;
				OnResize();
			}
			else if (wparam == SIZE_RESTORED)
			{
				if (mMinimized)
				{
					mAppPaused = false;
					mMinimized = false;
					OnResize();
				}
				else if (mMaximized)
				{
					mAppPaused = false;
					mMaximized = false;
					OnResize();
				}
				else if (mResizing)
				{

				}
				else
				{
					OnResize();
				}
			}
		}
		return 0;

	case WM_ENTERSIZEMOVE:
		mAppPaused = true;
		mResizing = true;
		mTimer.Stop();
		return 0;

	case WM_EXITSIZEMOVE:
		mAppPaused = false;
		mResizing = false;
		mTimer.Start();
		OnResize();
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lparam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lparam)->ptMinTrackSize.y = 200;
		return 0;

	// The WM_MENUCHAR message is sent when a menu is active and the user presses 
	// a key that does not correspond to any mnemonic or accelerator key. 
	case WM_MENUCHAR:
		// Don't beep when we alt-enter.
		return MAKELRESULT(0, MNC_CLOSE);

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		OnMouseDown(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		OnMouseUp(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_MOUSEMOVE:
		OnMouseMove(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
		return 0;

	case WM_KEYUP:
		if (wparam == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
		else if ((int)wparam == VK_F2)
			m4xMsaaStatus = !m4xMsaaStatus;
		return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}
HWND InitWindow(HINSTANCE hInstance, int showMode)
{
	WNDCLASS wc = {};
	wc.lpfnWndProc = MsgProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"努力做游戏";

	if (FAILED(RegisterClass(&wc)))
	{
		OutputDebugString(L"Register Window Failed!\n");
		exit(-1);
	}

	HWND hwnd = CreateWindow(L"努力做游戏", mMainWndName.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		mClientWidth, mClientHeight, 0, 0, hInstance, 0);

	RECT R = { 0, 0, mClientWidth, mClientHeight };
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width = R.right - R.left;
	int height = R.bottom - R.top;

	if (hwnd < 0)
	{
		OutputDebugString(L"Create Window Failed!\n");
		exit(-1);
	}

	ShowWindow(hwnd, showMode);
	UpdateWindow(hwnd);
	return hwnd;
}

void FlushCommandQueue()
{
	mCurrentFence++;
	ThrowIfFailed(mCmdQueue->Signal(mFence.Get(), mCurrentFence));

	if (mFence->GetCompletedValue() < mCurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, 0, false, EVENT_ALL_ACCESS);
		assert(eventHandle != NULL);

		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

		// 0xFFFFFFFF如果出现失败
		ThrowIfFailed(WaitForSingleObject(eventHandle, INFINITE));
		CloseHandle(eventHandle);
	}
}

void CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NodeMask = 0;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NumDescriptors = mSwapChainCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	ThrowIfFailed(m_Device->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.ReleaseAndGetAddressOf())
	));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NodeMask = 0;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	ThrowIfFailed(m_Device->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.ReleaseAndGetAddressOf())
	));
}
D3D_FEATURE_LEVEL CheckFeatureLevelSupport()
{
	D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevelsInfo;
	featureLevelsInfo.NumFeatureLevels = 3;
	featureLevelsInfo.pFeatureLevelsRequested = featureLevels;

	ThrowIfFailed(m_Device->CheckFeatureSupport(
		D3D12_FEATURE_FEATURE_LEVELS,
		&featureLevelsInfo,
		sizeof(featureLevelsInfo)
	));

	return featureLevelsInfo.MaxSupportedFeatureLevel;
}
void Check4xMsaaQualityLevels()
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	msQualityLevels.SampleCount = 4; // 只检查Msaa4x
	msQualityLevels.NumQualityLevels = 0; // 待填充
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE; // 不需要判断平铺资源！

	ThrowIfFailed(m_Device->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)
	));

	m4xMsaaQualityLevels = msQualityLevels.NumQualityLevels; // （0 ~ NumQualityLevels-1）
	assert(m4xMsaaQualityLevels > 0 && "Unexpected MSAA quality level.");
}

void CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCmdQueue)));

	ThrowIfFailed(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mCmdAlloc)));

	ThrowIfFailed(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCmdAlloc.Get(), nullptr, IID_PPV_ARGS(&mCmdList)));

	mCmdList->Close();
}
void CreateFences()
{
	ThrowIfFailed(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mFence.GetAddressOf())));
	mRtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvUavDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}
void CreateSwapChainObjects()
{
	// 释放之前的SwapChain
	mSwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC sd = {};
	sd.SampleDesc.Quality = m4xMsaaStatus ? (m4xMsaaQualityLevels - 1) : 0;
	sd.SampleDesc.Count = m4xMsaaStatus ? 4 : 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = mSwapChainCount;
	sd.OutputWindow = mhMainWnd;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.BufferDesc.Width = mClientWidth;
	sd.BufferDesc.Height = mClientHeight;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = mBackBufferFormat;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// 交换链每帧都需要用命令队列刷新！
	ThrowIfFailed(m_Factory->CreateSwapChain(mCmdQueue.Get(), &sd, mSwapChain.GetAddressOf()));
}

D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView()
{
	// d3dx12.h的辅助构造函数
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mCurrBackBuffer, mRtvDescriptorSize);
}
D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()
{
	return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void CalculateFrameStats()
{
	static int frameCnt = 0;
	static float timeElapsed = 0.0f;

	frameCnt++;

	if (mTimer.TotalTime() - timeElapsed >= 1.0f)
	{
		float fps = (float)frameCnt;
		float mspf = 1000.0f / fps;
		std::wstring fpsStr = std::to_wstring(fps);
		std::wstring mspfStr = std::to_wstring(mspf);

		std::wstring windowText = mMainWndName +
			L"  fps: " + fpsStr +
			L"  mspf: " + mspfStr;
		SetWindowText(mhMainWnd, windowText.c_str());

		frameCnt = 0;
		timeElapsed += 1.0f;
	}
}