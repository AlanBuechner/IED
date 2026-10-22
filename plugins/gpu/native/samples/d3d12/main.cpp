// fb-sample-d3d12: a small D3D12 program to capture (docs/22): a compute pass that animates a texture, textured cubes
// and a floor drawn into an HDR target with depth, then a tonemap pass to the swap chain; PIX markers, object names, a
// persistently mapped constant buffer and a bindless-style texture index. The GPU debugger's tests capture it.
//   fb-sample-d3d12 [--frames N] [--capture-frame K] [--flashbang path\to\flashbang.dll] [--hidden]
#include "include/flashbang_app.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	const UINT kWidth = 1280, kHeight = 720, kPattern = 256, kChecker = 256;

	const char* kShaders = R"(
cbuffer Frame : register(b0) { float4x4 viewProj; float time; float3 pad; };
cbuffer Object : register(b1) { float4x4 world; float3 tint; uint textureIndex; };
Texture2D textures[2] : register(t0);
RWTexture2D<float4> pattern : register(u0);
SamplerState linearWrap : register(s0);

struct VSIn { float3 pos : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD; };
struct VSOut { float4 pos : SV_Position; float3 normal : NORMAL; float2 uv : TEXCOORD; };

VSOut VSMain(VSIn i) {
	VSOut o;
	float4 w = mul(world, float4(i.pos, 1));
	o.pos = mul(viewProj, w);
	o.normal = mul((float3x3)world, i.normal);
	o.uv = i.uv;
	return o;
}

float4 PSMain(VSOut i) : SV_Target {
	float3 n = normalize(i.normal);
	float light = saturate(dot(n, normalize(float3(0.4, 0.8, -0.5)))) * 0.8 + 0.2;
	float3 albedo = textures[NonUniformResourceIndex(textureIndex)].Sample(linearWrap, i.uv).rgb;
	return float4(albedo * tint * light * 2.0, 1);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
	float2 uv = id.xy / 256.0;
	float wave = 0.5 + 0.5 * sin(time * 2.0 + uv.x * 12.0);
	pattern[id.xy] = float4(uv.x, wave, uv.y, 1);
}

struct Full { float4 pos : SV_Position; float2 uv : TEXCOORD; };
Full VSFull(uint id : SV_VertexID) {
	Full o;
	o.uv = float2((id << 1) & 2, id & 2);
	o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
	return o;
}

float4 PSTonemap(Full i) : SV_Target {
	float3 c = textures[0].Sample(linearWrap, i.uv).rgb;
	c = c / (1 + c);
	return float4(pow(c, 1.0 / 2.2), 1);
}
)";

	struct Float4x4
	{
		float m[4][4];
	};

	Float4x4 Multiply(const Float4x4& a, const Float4x4& b) {
		Float4x4 r = {};
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j)
				for (int k = 0; k < 4; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
		return r;
	}

	Float4x4 Identity() {
		Float4x4 r = {};
		for (int i = 0; i < 4; ++i) r.m[i][i] = 1;
		return r;
	}

	// Column-vector convention: clip = M * v. HLSL reads constant buffers column-major, so matrices go up transposed.
	Float4x4 Transposed(const Float4x4& a) {
		Float4x4 r;
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j) r.m[i][j] = a.m[j][i];
		return r;
	}

	Float4x4 Translation(float x, float y, float z) {
		Float4x4 r = Identity();
		r.m[0][3] = x, r.m[1][3] = y, r.m[2][3] = z;
		return r;
	}

	Float4x4 Scale(float x, float y, float z) {
		Float4x4 r = Identity();
		r.m[0][0] = x, r.m[1][1] = y, r.m[2][2] = z;
		return r;
	}

	Float4x4 RotationY(float angle) {
		Float4x4 r = Identity();
		r.m[0][0] = std::cos(angle), r.m[0][2] = std::sin(angle);
		r.m[2][0] = -std::sin(angle), r.m[2][2] = std::cos(angle);
		return r;
	}

	Float4x4 Perspective(float fovY, float aspect, float zn, float zf) {
		const float y = 1.0f / std::tan(fovY / 2), x = y / aspect;
		Float4x4 r = {};
		r.m[0][0] = x, r.m[1][1] = y;
		r.m[2][2] = zf / (zf - zn), r.m[2][3] = -zn * zf / (zf - zn);
		r.m[3][2] = 1;
		return r;
	}

	Float4x4 View(float eyeX, float eyeY, float eyeZ) {
		// Looking at the origin from the eye, left-handed, y up.
		float fx = -eyeX, fy = -eyeY, fz = -eyeZ;
		const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
		fx /= fl, fy /= fl, fz /= fl;
		float rx = fz, ry = 0, rz = -fx;
		const float rl = std::sqrt(rx * rx + rz * rz);
		rx /= rl, rz /= rl;
		const float ux = fy * rz - fz * ry, uy = fz * rx - fx * rz, uz = fx * ry - fy * rx;
		Float4x4 r = Identity();
		r.m[0][0] = rx, r.m[0][1] = ry, r.m[0][2] = rz, r.m[0][3] = -(rx * eyeX + ry * eyeY + rz * eyeZ);
		r.m[1][0] = ux, r.m[1][1] = uy, r.m[1][2] = uz, r.m[1][3] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
		r.m[2][0] = fx, r.m[2][1] = fy, r.m[2][2] = fz, r.m[2][3] = -(fx * eyeX + fy * eyeY + fz * eyeZ);
		return r;
	}

	struct Vertex
	{
		float pos[3];
		float normal[3];
		float uv[2];
	};

	struct ObjectConstants
	{
		Float4x4 world;
		float tint[3];
		UINT textureIndex;
	};

	struct FrameConstants
	{
		Float4x4 viewProj;
		float time;
		float pad[3];
	};

	void Check(HRESULT hr, const char* what) {
		if (SUCCEEDED(hr)) return;
		fprintf(stderr, "fb-sample-d3d12: %s failed (0x%08lx)\n", what, (unsigned long)hr);
		ExitProcess(2);
	}

	void Marker(ID3D12GraphicsCommandList* list, const wchar_t* name) { list->BeginEvent(0, name, (UINT)((wcslen(name) + 1) * sizeof(wchar_t))); }

	ID3DBlob* Compile(const char* entry, const char* target) {
		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		const HRESULT hr = D3DCompile(kShaders, strlen(kShaders), "sample.hlsl", nullptr, nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
		if (FAILED(hr)) {
			fprintf(stderr, "shader %s: %s\n", entry, errors ? (const char*)errors->GetBufferPointer() : "?");
			ExitProcess(2);
		}
		if (errors) errors->Release();
		return code;
	}

	D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		return barrier;
	}

	LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
		if (message == WM_DESTROY) {
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(window, message, w, l);
	}

	void MakeCube(std::vector<Vertex>& vertices, std::vector<UINT16>& indices) {
		// Each face: its normal and two axes across it.
		const float faces[6][3][3] = {
			{{0, 0, 1}, {-1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
			{{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
		};
		for (int face = 0; face < 6; ++face) {
			const float* nf = faces[face][0];
			const float* u = faces[face][1];
			const float* v = faces[face][2];
			const UINT16 base = (UINT16)vertices.size();
			const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
			for (auto& c : corners) {
				Vertex vertex = {};
				for (int k = 0; k < 3; ++k) {
					vertex.pos[k] = 0.5f * (nf[k] + c[0] * u[k] + c[1] * v[k]);
					vertex.normal[k] = nf[k];
				}
				vertex.uv[0] = 0.5f + 0.5f * c[0];
				vertex.uv[1] = 0.5f - 0.5f * c[1];
				vertices.push_back(vertex);
			}
			const UINT16 quad[6] = {0, 2, 1, 0, 3, 2};
			for (UINT16 q : quad) indices.push_back((UINT16)(base + q));
		}
		// The floor: one quad under the cubes.
		const UINT16 base = (UINT16)vertices.size();
		const float floor[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
		for (auto& c : floor) {
			Vertex vertex = {{c[0], 0, c[1]}, {0, 1, 0}, {0.5f + 0.5f * c[0], 0.5f + 0.5f * c[1]}};
			vertices.push_back(vertex);
		}
		const UINT16 quad[6] = {0, 1, 2, 0, 2, 3};
		for (UINT16 q : quad) indices.push_back((UINT16)(base + q));
	}
}

int wmain(int argc, wchar_t** argv) {
	int frames = 0, captureFrame = -1;
	const wchar_t* libraryPath = nullptr;
	bool hidden = false;
	for (int i = 1; i < argc; ++i) {
		if (!wcscmp(argv[i], L"--frames") && i + 1 < argc) frames = _wtoi(argv[++i]);
		else if (!wcscmp(argv[i], L"--capture-frame") && i + 1 < argc) captureFrame = _wtoi(argv[++i]);
		else if (!wcscmp(argv[i], L"--flashbang") && i + 1 < argc) libraryPath = argv[++i];
		else if (!wcscmp(argv[i], L"--hidden")) hidden = true;
	}

	flashbang::Library capture;
	const bool captureLoaded = flashbang::Load(capture, libraryPath);
	printf("fb-sample-d3d12: capture library %s\n", captureLoaded ? "loaded" : "not loaded");
	fflush(stdout);

	WNDCLASSW windowClass = {};
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = GetModuleHandleW(nullptr);
	windowClass.lpszClassName = L"FlashbangSampleD3D12";
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassW(&windowClass);
	RECT rect = {0, 0, (LONG)kWidth, (LONG)kHeight};
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	HWND window = CreateWindowW(windowClass.lpszClassName, L"Flashbang sample (D3D12)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, windowClass.hInstance, nullptr);
	ShowWindow(window, hidden ? SW_SHOWMINNOACTIVE : SW_SHOW);

	IDXGIFactory4* factory = nullptr;
	Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
	factory = flashbang::WrapFactory(capture, factory);
	IDXGIAdapter1* adapter = nullptr;
	IDXGIFactory6* factory6 = nullptr;
	if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
		factory6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
		factory6->Release();
	}
	ID3D12Device* device = nullptr;
	Check(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice");
	if (adapter) adapter->Release();
	device = flashbang::WrapDevice(capture, device);
	device->SetName(L"Sample device");

	D3D12_COMMAND_QUEUE_DESC queueDesc = {D3D12_COMMAND_LIST_TYPE_DIRECT};
	ID3D12CommandQueue* queue = nullptr;
	Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
	queue->SetName(L"Direct queue");

	DXGI_SWAP_CHAIN_DESC1 chainDesc = {};
	chainDesc.Width = kWidth;
	chainDesc.Height = kHeight;
	chainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	chainDesc.SampleDesc.Count = 1;
	chainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	chainDesc.BufferCount = 2;
	chainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	IDXGISwapChain1* chain1 = nullptr;
	Check(factory->CreateSwapChainForHwnd(queue, window, &chainDesc, nullptr, nullptr, &chain1), "CreateSwapChainForHwnd");
	IDXGISwapChain3* chain = nullptr;
	Check(chain1->QueryInterface(IID_PPV_ARGS(&chain)), "IDXGISwapChain3");
	chain1->Release();

	ID3D12CommandAllocator* allocator = nullptr;
	Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "CreateCommandAllocator");
	ID3D12GraphicsCommandList* list = nullptr;
	Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list)), "CreateCommandList");
	list->SetName(L"Frame list");
	ID3D12Fence* fence = nullptr;
	Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
	UINT64 fenceValue = 0;
	HANDLE fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	auto waitIdle = [&] {
		queue->Signal(fence, ++fenceValue);
		if (fence->GetCompletedValue() < fenceValue) {
			fence->SetEventOnCompletion(fenceValue, fenceEvent);
			WaitForSingleObject(fenceEvent, INFINITE);
		}
	};

	// Descriptor heaps: render targets, depth, and the shader-visible views (0: HDR target, 1: pattern SRV, 2: checker SRV,
	// 3: pattern UAV).
	D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3};
	ID3D12DescriptorHeap* rtvHeap = nullptr;
	Check(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap)), "RTV heap");
	D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1};
	ID3D12DescriptorHeap* dsvHeap = nullptr;
	Check(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap)), "DSV heap");
	D3D12_DESCRIPTOR_HEAP_DESC viewDesc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
	ID3D12DescriptorHeap* viewHeap = nullptr;
	Check(device->CreateDescriptorHeap(&viewDesc, IID_PPV_ARGS(&viewHeap)), "view heap");
	viewHeap->SetName(L"Bindless views");
	const UINT rtvStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const UINT viewStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	auto cpuView = [&](UINT i) { D3D12_CPU_DESCRIPTOR_HANDLE h = viewHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += (SIZE_T)i * viewStep; return h; };
	auto gpuView = [&](UINT i) { D3D12_GPU_DESCRIPTOR_HANDLE h = viewHeap->GetGPUDescriptorHandleForHeapStart(); h.ptr += (UINT64)i * viewStep; return h; };
	auto rtv = [&](UINT i) { D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += (SIZE_T)i * rtvStep; return h; };

	ID3D12Resource* backBuffers[2] = {};
	for (UINT i = 0; i < 2; ++i) {
		Check(chain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i])), "GetBuffer");
		device->CreateRenderTargetView(backBuffers[i], nullptr, rtv(i));
	}

	D3D12_HEAP_PROPERTIES defaultHeap = {D3D12_HEAP_TYPE_DEFAULT};
	D3D12_HEAP_PROPERTIES uploadHeap = {D3D12_HEAP_TYPE_UPLOAD};
	auto texture = [&](UINT w, UINT h, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) {
		D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, w, h, 1, 1, format, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, flags};
		return desc;
	};
	auto buffer = [&](UINT64 size) {
		D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
		return desc;
	};

	ID3D12Resource* hdr = nullptr;
	D3D12_RESOURCE_DESC hdrDesc = texture(kWidth, kHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	D3D12_CLEAR_VALUE hdrClear = {DXGI_FORMAT_R16G16B16A16_FLOAT, {0.05f, 0.07f, 0.1f, 1.0f}};
	Check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &hdrDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &hdrClear, IID_PPV_ARGS(&hdr)), "HDR target");
	hdr->SetName(L"HDR");
	device->CreateRenderTargetView(hdr, nullptr, rtv(2));
	device->CreateShaderResourceView(hdr, nullptr, cpuView(0));

	ID3D12Resource* depth = nullptr;
	D3D12_RESOURCE_DESC depthDesc = texture(kWidth, kHeight, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
	D3D12_CLEAR_VALUE depthClear = {DXGI_FORMAT_D32_FLOAT};
	depthClear.DepthStencil.Depth = 1.0f;
	Check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(&depth)), "depth");
	depth->SetName(L"Depth");
	device->CreateDepthStencilView(depth, nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());

	ID3D12Resource* pattern = nullptr;
	D3D12_RESOURCE_DESC patternDesc = texture(kPattern, kPattern, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	Check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &patternDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&pattern)), "pattern");
	pattern->SetName(L"Pattern");
	device->CreateShaderResourceView(pattern, nullptr, cpuView(1));
	device->CreateUnorderedAccessView(pattern, nullptr, nullptr, cpuView(3));

	ID3D12Resource* checker = nullptr;
	D3D12_RESOURCE_DESC checkerDesc = texture(kChecker, kChecker, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
	Check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &checkerDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&checker)), "checker");
	checker->SetName(L"Checker");
	device->CreateShaderResourceView(checker, nullptr, cpuView(2));

	std::vector<Vertex> vertices;
	std::vector<UINT16> indices;
	MakeCube(vertices, indices);
	const UINT64 vertexBytes = vertices.size() * sizeof(Vertex), indexBytes = indices.size() * sizeof(UINT16);
	ID3D12Resource* geometry = nullptr;
	D3D12_RESOURCE_DESC geometryDesc = buffer(vertexBytes + indexBytes);
	Check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &geometryDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&geometry)), "geometry");
	geometry->SetName(L"Cube geometry");

	// Startup upload: the geometry and the checker texture through one upload buffer.
	const UINT checkerPitch = kChecker * 4;
	ID3D12Resource* staging = nullptr;
	D3D12_RESOURCE_DESC stagingDesc = buffer(vertexBytes + indexBytes + 512 + (UINT64)checkerPitch * kChecker);
	Check(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &stagingDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)), "staging");
	staging->SetName(L"Startup staging");
	UINT8* mapped = nullptr;
	staging->Map(0, nullptr, (void**)&mapped);
	memcpy(mapped, vertices.data(), vertexBytes);
	memcpy(mapped + vertexBytes, indices.data(), indexBytes);
	const UINT64 texelsAt = (vertexBytes + indexBytes + 511) & ~511ull;
	for (UINT y = 0; y < kChecker; ++y)
		for (UINT x = 0; x < kChecker; ++x) {
			UINT8* texel = mapped + texelsAt + (UINT64)y * checkerPitch + x * 4;
			const bool on = ((x / 32) + (y / 32)) & 1;
			texel[0] = on ? 220 : 60, texel[1] = on ? 220 : 60, texel[2] = on ? 210 : 70, texel[3] = 255;
		}
	staging->Unmap(0, nullptr);
	list->CopyBufferRegion(geometry, 0, staging, 0, vertexBytes + indexBytes);
	D3D12_TEXTURE_COPY_LOCATION dst = {checker, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
	dst.SubresourceIndex = 0;
	D3D12_TEXTURE_COPY_LOCATION src = {staging, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
	src.PlacedFootprint.Offset = texelsAt;
	src.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, kChecker, kChecker, 1, checkerPitch};
	list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
	D3D12_RESOURCE_BARRIER uploaded[2] = {Transition(geometry, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER),
		Transition(checker, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
	list->ResourceBarrier(2, uploaded);
	Check(list->Close(), "Close");
	ID3D12CommandList* startup[] = {list};
	queue->ExecuteCommandLists(1, startup);
	waitIdle();
	staging->Release();

	ID3D12Resource* constants = nullptr;
	D3D12_RESOURCE_DESC constantsDesc = buffer(256);
	Check(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &constantsDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constants)), "constants");
	constants->SetName(L"Frame constants");
	FrameConstants* frameConstants = nullptr;
	constants->Map(0, nullptr, (void**)&frameConstants);

	// Root signature: b0 frame constants (root CBV), b1 object constants (20 root constants), t0..t1 textures, u0 pattern.
	D3D12_DESCRIPTOR_RANGE srvRange = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
	D3D12_DESCRIPTOR_RANGE uavRange = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
	D3D12_ROOT_PARAMETER params[4] = {};
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor = {0, 0};
	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	params[1].Constants = {1, 0, 20};
	params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[2].DescriptorTable = {1, &srvRange};
	params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[3].DescriptorTable = {1, &uavRange};
	D3D12_STATIC_SAMPLER_DESC sampler = {D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0, 1, D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 0, D3D12_FLOAT32_MAX, 0, 0, D3D12_SHADER_VISIBILITY_ALL};
	D3D12_ROOT_SIGNATURE_DESC rootDesc = {4, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
	ID3DBlob* rootBlob = nullptr;
	ID3DBlob* rootErrors = nullptr;
	Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &rootErrors), "D3D12SerializeRootSignature");
	ID3D12RootSignature* root = nullptr;
	Check(device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&root)), "CreateRootSignature");
	root->SetName(L"Sample root signature");

	ID3DBlob* vs = Compile("VSMain", "vs_5_1");
	ID3DBlob* ps = Compile("PSMain", "ps_5_1");
	ID3DBlob* cs = Compile("CSMain", "cs_5_1");
	ID3DBlob* fullVs = Compile("VSFull", "vs_5_1");
	ID3DBlob* tonemapPs = Compile("PSTonemap", "ps_5_1");
	D3D12_INPUT_ELEMENT_DESC layout[3] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
	auto graphics = [&](ID3DBlob* v, ID3DBlob* p, DXGI_FORMAT target, bool depthTest, bool withLayout) {
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = root;
		desc.VS = {v->GetBufferPointer(), v->GetBufferSize()};
		desc.PS = {p->GetBufferPointer(), p->GetBufferSize()};
		desc.BlendState.RenderTarget[0].SrcBlend = desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		desc.BlendState.RenderTarget[0].DestBlend = desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		desc.BlendState.RenderTarget[0].BlendOp = desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
		desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState = {D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE, FALSE, 0, 0, 0, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
		desc.DepthStencilState.DepthEnable = depthTest;
		desc.DepthStencilState.DepthWriteMask = depthTest ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		if (withLayout) desc.InputLayout = {layout, 3};
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = target;
		desc.DSVFormat = depthTest ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc = {1, 0};
		ID3D12PipelineState* state = nullptr;
		Check(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&state)), "CreateGraphicsPipelineState");
		return state;
	};
	ID3D12PipelineState* litPipeline = graphics(vs, ps, DXGI_FORMAT_R16G16B16A16_FLOAT, true, true);
	litPipeline->SetName(L"Lit");
	ID3D12PipelineState* tonemapPipeline = graphics(fullVs, tonemapPs, DXGI_FORMAT_R8G8B8A8_UNORM, false, false);
	tonemapPipeline->SetName(L"Tonemap");
	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {root, {cs->GetBufferPointer(), cs->GetBufferSize()}};
	ID3D12PipelineState* computePipeline = nullptr;
	Check(device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&computePipeline)), "CreateComputePipelineState");
	computePipeline->SetName(L"Pattern");

	D3D12_VERTEX_BUFFER_VIEW vertexView = {geometry->GetGPUVirtualAddress(), (UINT)vertexBytes, sizeof(Vertex)};
	D3D12_INDEX_BUFFER_VIEW indexView = {geometry->GetGPUVirtualAddress() + vertexBytes, (UINT)indexBytes, DXGI_FORMAT_R16_UINT};
	const UINT cubeIndices = 36, floorFirstIndex = 36;

	const Float4x4 viewProj = Multiply(Perspective(0.9f, (float)kWidth / kHeight, 0.1f, 50.0f), View(0.0f, 2.2f, -4.5f));
	struct Item
	{
		const wchar_t* name;
		Float4x4 world;
		float tint[3];
		UINT texture;
		bool floor;
	};

	LARGE_INTEGER start, frequency;
	QueryPerformanceCounter(&start);
	QueryPerformanceFrequency(&frequency);
	int frame = 0;
	MSG message = {};
	for (;;) {
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
			if (message.message == WM_QUIT) break;
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		if (message.message == WM_QUIT) break;
		if (frame == captureFrame) flashbang::TriggerCapture(capture, 1);
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		const float time = (float)(now.QuadPart - start.QuadPart) / (float)frequency.QuadPart;

		frameConstants->viewProj = Transposed(viewProj);
		frameConstants->time = time;

		// Hidden sits behind Cube B: every fragment it has fails the depth test (Pixel History's case).
		const Item items[5] = {
			{L"Floor", Scale(3.0f, 1.0f, 3.0f), {1.0f, 1.0f, 1.0f}, 1, true},
			{L"Cube A", Multiply(Translation(-1.4f, 0.5f, 0.0f), RotationY(time * 0.7f)), {1.0f, 0.55f, 0.35f}, 0, false},
			{L"Cube B", Multiply(Translation(0.0f, 0.5f, 0.6f), RotationY(-time * 0.5f)), {0.4f, 0.9f, 0.5f}, 0, false},
			{L"Cube C", Multiply(Translation(1.4f, 0.5f, 0.0f), RotationY(time * 0.3f)), {0.45f, 0.6f, 1.0f}, 1, false},
			{L"Hidden", Multiply(Translation(0.0f, 0.4f, 1.6f), Scale(0.4f, 0.4f, 0.4f)), {1.0f, 0.2f, 0.8f}, 0, false},
		};

		Check(allocator->Reset(), "allocator Reset");
		Check(list->Reset(allocator, nullptr), "list Reset");
		const UINT backIndex = chain->GetCurrentBackBufferIndex();
		ID3D12DescriptorHeap* heaps[] = {viewHeap};
		list->SetDescriptorHeaps(1, heaps);
		list->SetComputeRootSignature(root);
		list->SetGraphicsRootSignature(root);
		Marker(list, L"Frame");

		Marker(list, L"Generate pattern");
		D3D12_RESOURCE_BARRIER toWrite = Transition(pattern, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		list->ResourceBarrier(1, &toWrite);
		list->SetPipelineState(computePipeline);
		list->SetComputeRootConstantBufferView(0, constants->GetGPUVirtualAddress());
		list->SetComputeRootDescriptorTable(3, gpuView(3));
		list->Dispatch(kPattern / 8, kPattern / 8, 1);
		D3D12_RESOURCE_BARRIER toRead = Transition(pattern, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		list->ResourceBarrier(1, &toRead);
		list->EndEvent();

		Marker(list, L"Scene");
		D3D12_RESOURCE_BARRIER hdrTarget = Transition(hdr, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		list->ResourceBarrier(1, &hdrTarget);
		const D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv = rtv(2), dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
		list->ClearRenderTargetView(hdrRtv, hdrClear.Color, 0, nullptr);
		list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		list->OMSetRenderTargets(1, &hdrRtv, FALSE, &dsv);
		D3D12_VIEWPORT viewport = {0, 0, (float)kWidth, (float)kHeight, 0, 1};
		D3D12_RECT scissor = {0, 0, (LONG)kWidth, (LONG)kHeight};
		list->RSSetViewports(1, &viewport);
		list->RSSetScissorRects(1, &scissor);
		list->SetPipelineState(litPipeline);
		list->SetGraphicsRootConstantBufferView(0, constants->GetGPUVirtualAddress());
		list->SetGraphicsRootDescriptorTable(2, gpuView(1));
		list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		list->IASetVertexBuffers(0, 1, &vertexView);
		list->IASetIndexBuffer(&indexView);
		for (const Item& item : items) {
			Marker(list, item.name);
			ObjectConstants object = {Transposed(item.world), {item.tint[0], item.tint[1], item.tint[2]}, item.texture};
			list->SetGraphicsRoot32BitConstants(1, 20, &object, 0);
			if (item.floor) list->DrawIndexedInstanced(6, 1, floorFirstIndex, 0, 0);
			else list->DrawIndexedInstanced(cubeIndices, 1, 0, 0, 0);
			list->EndEvent();
		}
		list->EndEvent();

		Marker(list, L"Tonemap");
		D3D12_RESOURCE_BARRIER toneBarriers[2] = {Transition(hdr, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
			Transition(backBuffers[backIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)};
		list->ResourceBarrier(2, toneBarriers);
		const D3D12_CPU_DESCRIPTOR_HANDLE backRtv = rtv(backIndex);
		list->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);
		list->SetPipelineState(tonemapPipeline);
		list->SetGraphicsRootDescriptorTable(2, gpuView(0));
		list->DrawInstanced(3, 1, 0, 0);
		D3D12_RESOURCE_BARRIER toPresent = Transition(backBuffers[backIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		list->ResourceBarrier(1, &toPresent);
		list->EndEvent();

		list->EndEvent();
		Check(list->Close(), "Close");
		ID3D12CommandList* lists[] = {list};
		queue->ExecuteCommandLists(1, lists);
		Check(chain->Present(1, 0), "Present");
		waitIdle();
		++frame;
		if (frames && frame >= frames) break;
	}

	waitIdle();
	printf("fb-sample-d3d12: %d frames\n", frame);
	return 0;
}
