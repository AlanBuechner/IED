// The DXGI factory and swap chain proxies: swap chains made for a wrapped queue come back as proxies, and their
// Present calls are the frame boundaries of a capture (docs/22 "Capture").
#include "wrappers.h"

namespace fb::capture::d3d12
{
	// {7B1C3D95-2E4F-4A60-8C71-92A3B4C5D6E7}
	const GUID kFactoryProxyIid = {0x7b1c3d95, 0x2e4f, 0x4a60, {0x8c, 0x71, 0x92, 0xa3, 0xb4, 0xc5, 0xd6, 0xe7}};
	// {1E2D3C4B-5A69-4788-96A5-B4C3D2E1F009}
	const GUID kSwapChainProxyIid = {0x1e2d3c4b, 0x5a69, 0x4788, {0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0, 0x09}};

	// --- Factory -------------------------------------------------------------------------------------------------------

	WrappedFactory::WrappedFactory(IDXGIFactory* real) {
		void** slots[] = {(void**)&real0_, (void**)&real1_, (void**)&real2_, (void**)&real3_, (void**)&real4_, (void**)&real5_, (void**)&real6_, (void**)&real7_};
		for (int v = 0; v < kVersions; ++v)
			if (FAILED(real->QueryInterface(VersionIid(v), slots[v]))) *slots[v] = nullptr;
	}

	WrappedFactory::~WrappedFactory() {
		for (int v = kVersions - 1; v >= 0; --v)
			if (IUnknown* versioned = (IUnknown*)RealVersion(v)) versioned->Release();
	}

	ULONG WrappedFactory::Release() {
		const ULONG left = --references_;
		if (left == 0) delete this;
		return left;
	}

	HRESULT WrappedFactory::QueryInterface(REFIID riid, void** out) {
		if (!out) return E_POINTER;
		if (riid == kFactoryProxyIid || riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject)) {
			*out = static_cast<IDXGIFactory7*>(this);
			AddRef();
			return S_OK;
		}
		for (int v = 0; v < kVersions; ++v) {
			if (riid == VersionIid(v)) {
				if (!RealVersion(v)) return E_NOINTERFACE;
				*out = static_cast<IDXGIFactory7*>(this);
				AddRef();
				return S_OK;
			}
		}
		return real0_->QueryInterface(riid, out);
	}

	// The wrapped queue behind `device` (what DXGI calls the device of a D3D12 swap chain), AddRef'd; null otherwise.
	static WrappedQueue* QueueOf(IUnknown* device) {
		WrappedQueue* queue = nullptr;
		if (device && SUCCEEDED(device->QueryInterface(kQueueProxyIid, (void**)&queue))) return queue;
		return nullptr;
	}

	template <class I> static HRESULT HandChain(WrappedQueue* queue, I* real, I** out) {
		WrappedSwapChain* chain = new WrappedSwapChain(queue, real);
		real->Release();
		queue->Release();
		const HRESULT hr = chain->QueryInterface(__uuidof(I), (void**)out);
		chain->Release();
		return hr;
	}

	HRESULT WrappedFactory::CreateSwapChain(IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {
		WrappedQueue* queue = QueueOf(pDevice);
		if (!queue) return real0_->CreateSwapChain(pDevice, pDesc, ppSwapChain);
		IDXGISwapChain* real = nullptr;
		const HRESULT hr = real0_->CreateSwapChain(queue->Real(), pDesc, &real);
		if (FAILED(hr)) {
			queue->Release();
			return hr;
		}
		return HandChain(queue, real, ppSwapChain);
	}

	HRESULT WrappedFactory::CreateSwapChainForHwnd(IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
		WrappedQueue* queue = QueueOf(pDevice);
		if (!queue) return real2_->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
		IDXGISwapChain1* real = nullptr;
		const HRESULT hr = real2_->CreateSwapChainForHwnd(queue->Real(), hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, &real);
		if (FAILED(hr)) {
			queue->Release();
			return hr;
		}
		return HandChain(queue, real, ppSwapChain);
	}

	HRESULT WrappedFactory::CreateSwapChainForCoreWindow(IUnknown* pDevice, IUnknown* pWindow, const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
		WrappedQueue* queue = QueueOf(pDevice);
		if (!queue) return real2_->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
		IDXGISwapChain1* real = nullptr;
		const HRESULT hr = real2_->CreateSwapChainForCoreWindow(queue->Real(), pWindow, pDesc, pRestrictToOutput, &real);
		if (FAILED(hr)) {
			queue->Release();
			return hr;
		}
		return HandChain(queue, real, ppSwapChain);
	}

	HRESULT WrappedFactory::CreateSwapChainForComposition(IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
		WrappedQueue* queue = QueueOf(pDevice);
		if (!queue) return real2_->CreateSwapChainForComposition(pDevice, pDesc, pRestrictToOutput, ppSwapChain);
		IDXGISwapChain1* real = nullptr;
		const HRESULT hr = real2_->CreateSwapChainForComposition(queue->Real(), pDesc, pRestrictToOutput, &real);
		if (FAILED(hr)) {
			queue->Release();
			return hr;
		}
		return HandChain(queue, real, ppSwapChain);
	}

	HRESULT WrapFactory(IUnknown* factory, REFIID riid, void** out) {
		if (!factory || !out) return E_POINTER;
		WrappedFactory* proxy = nullptr;
		if (SUCCEEDED(factory->QueryInterface(kFactoryProxyIid, (void**)&proxy))) {
			const HRESULT hr = proxy->QueryInterface(riid, out);
			proxy->Release();
			return hr;
		}
		IDXGIFactory* real = nullptr;
		if (FAILED(factory->QueryInterface(__uuidof(IDXGIFactory), (void**)&real))) return E_NOINTERFACE;
		proxy = new WrappedFactory(real);
		real->Release();
		const HRESULT hr = proxy->QueryInterface(riid, out);
		proxy->Release();
		return hr;
	}

	// --- Swap chain -------------------------------------------------------------------------------------------------

	WrappedSwapChain::WrappedSwapChain(WrappedQueue* queue, IDXGISwapChain* real) : queue_(queue) {
		queue_->AddRef();
		void** slots[] = {(void**)&real0_, (void**)&real1_, (void**)&real2_, (void**)&real3_, (void**)&real4_};
		for (int v = 0; v < kVersions; ++v)
			if (FAILED(real->QueryInterface(VersionIid(v), slots[v]))) *slots[v] = nullptr;
		Object* object = queue_->Device()->Objects().Register(this, ObjectKind::SwapChain);
		id_ = object->id;
	}

	WrappedSwapChain::~WrappedSwapChain() {
		queue_->Device()->Objects().Destroyed(id_);
		for (int v = kVersions - 1; v >= 0; --v)
			if (IUnknown* versioned = (IUnknown*)RealVersion(v)) versioned->Release();
		queue_->Release();
	}

	ULONG WrappedSwapChain::Release() {
		const ULONG left = --references_;
		if (left == 0) delete this;
		return left;
	}

	HRESULT WrappedSwapChain::QueryInterface(REFIID riid, void** out) {
		if (!out) return E_POINTER;
		if (riid == kSwapChainProxyIid || riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject)) {
			*out = static_cast<IDXGISwapChain4*>(this);
			AddRef();
			return S_OK;
		}
		for (int v = 0; v < kVersions; ++v) {
			if (riid == VersionIid(v)) {
				if (!RealVersion(v)) return E_NOINTERFACE;
				*out = static_cast<IDXGISwapChain4*>(this);
				AddRef();
				return S_OK;
			}
		}
		return real0_->QueryInterface(riid, out);
	}

	HRESULT WrappedSwapChain::GetDevice(REFIID riid, void** ppDevice) {
		if (riid == __uuidof(ID3D12CommandQueue) || riid == __uuidof(IUnknown)) return queue_->QueryInterface(riid, ppDevice);
		return real0_->GetDevice(riid, ppDevice);
	}

	void WrappedSwapChain::RegisterBuffers() {
		DXGI_SWAP_CHAIN_DESC desc = {};
		if (FAILED(real0_->GetDesc(&desc))) return;
		WrappedDevice* device = queue_->Device();
		Tracker& tracker = device->Objects();
		std::vector<u64> buffers;
		for (UINT i = 0; i < desc.BufferCount; ++i) {
			ID3D12Resource* buffer = nullptr;
			if (FAILED(real0_->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&buffer))) break;
			Object* object = tracker.Find(buffer);
			if (!object) {
				ResourceCreate record = {};
				record.kind = ResourceKind::SwapChainBuffer;
				record.heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
				record.initialState = D3D12_RESOURCE_STATE_PRESENT;
				record.desc = ToDesc1(buffer->GetDesc());
				object = tracker.Add(buffer, ObjectKind::Resource, kCreateResource, record);
				object->named = buffer;
				auto track = std::make_unique<ResourceTrack>();
				track->kind = ResourceKind::SwapChainBuffer;
				track->desc = record.desc;
				track->states.assign(1, D3D12_RESOURCE_STATE_PRESENT);
				{
					std::unique_lock<std::shared_mutex> guard(tracker.lock);
					object->resource = std::move(track);
				}
				WatchLifetime(buffer, tracker, object->id);
				device->Frames().Created(object);
			}
			buffers.push_back(object->id);
			buffer->Release();
		}
		if (buffers == buffers_) return;
		buffers_ = buffers;
		// Which buffers are the swap chain's: metadata for the replay, which makes them as plain textures.
		SwapChainCreate record = {id_, queue_->Id(), desc.BufferDesc.Width, desc.BufferDesc.Height, desc.BufferDesc.Format, (u32)buffers_.size(), buffers_.data()};
		Object* self = tracker.FindId(id_);
		if (self) {
			ByteWriter out;
			Ser s(out, &tracker);
			Serialize(s, record);
			std::unique_lock<std::shared_mutex> guard(tracker.lock);
			self->recordType = kSwapChain;
			self->record = std::move(out.Buffer());
		}
		if (self) device->Frames().Created(self);
	}

	ID3D12Resource* WrappedSwapChain::CurrentBuffer(u64& id) {
		RegisterBuffers();
		const UINT index = real3_ ? real3_->GetCurrentBackBufferIndex() : 0;
		ID3D12Resource* buffer = nullptr;
		if (FAILED(real0_->GetBuffer(index, __uuidof(ID3D12Resource), (void**)&buffer))) return nullptr;
		id = queue_->Device()->Objects().Id(buffer);
		return buffer;
	}

	HRESULT WrappedSwapChain::Present(UINT SyncInterval, UINT Flags) {
		if (Flags & DXGI_PRESENT_TEST) return real0_->Present(SyncInterval, Flags);
		FrameCapture& frames = queue_->Device()->Frames();
		frames.BeforePresent(this, SyncInterval, Flags);
		const HRESULT hr = real0_->Present(SyncInterval, Flags);
		frames.AfterPresent();
		return hr;
	}

	HRESULT WrappedSwapChain::Present1(UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
		if (PresentFlags & DXGI_PRESENT_TEST) return real1_->Present1(SyncInterval, PresentFlags, pPresentParameters);
		FrameCapture& frames = queue_->Device()->Frames();
		frames.BeforePresent(this, SyncInterval, PresentFlags);
		const HRESULT hr = real1_->Present1(SyncInterval, PresentFlags, pPresentParameters);
		frames.AfterPresent();
		return hr;
	}

	HRESULT WrappedSwapChain::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) {
		const HRESULT hr = real0_->GetBuffer(Buffer, riid, ppSurface);
		if (SUCCEEDED(hr)) RegisterBuffers();
		return hr;
	}

	HRESULT WrappedSwapChain::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
		return real0_->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
	}

	HRESULT WrappedSwapChain::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue) {
		std::vector<IUnknown*> queues;
		if (ppPresentQueue) {
			const UINT count = BufferCount ? BufferCount : 1;
			for (UINT i = 0; i < count; ++i) {
				WrappedQueue* queue = QueueOf(ppPresentQueue[i]);
				queues.push_back(queue ? (IUnknown*)queue->Real() : ppPresentQueue[i]);
				if (queue) queue->Release();
			}
		}
		return real3_->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue ? queues.data() : nullptr);
	}
}
