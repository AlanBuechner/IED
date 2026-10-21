// The capture library's C API (native/include/flashbang_app.h; Bedrock Gpu::Capture binds the same exports): a program
// hands its D3D12 device and DXGI factory over to be captured, asks for captures and marks frames.
#include "capture.h"
#include "d3d12/wrappers.h"

#include <windows.h>

using namespace fb;

extern "C"
{
	__declspec(dllexport) int32_t FlashbangVersion(void) { return 1; }

	// A proxy of `device` as `riid` in `out`; the caller keeps its own reference to `device`.
	__declspec(dllexport) HRESULT FlashbangWrapD3D12Device(IUnknown* device, REFIID riid, void** out) { return capture::d3d12::WrapDevice(device, riid, out); }

	__declspec(dllexport) HRESULT FlashbangWrapDxgiFactory(IUnknown* factory, REFIID riid, void** out) { return capture::d3d12::WrapFactory(factory, riid, out); }

	// The same, for callers that keep one pointer: takes the program's reference to `object` and gives back the proxy
	// (any interface version of the object it was, since a proxy implements them all in one chain), or `object` itself
	// when it cannot be wrapped.
	__declspec(dllexport) void* FlashbangWrapD3D12DeviceTake(void* object) {
		IUnknown* original = (IUnknown*)object;
		void* proxy = nullptr;
		if (!original || FAILED(capture::d3d12::WrapDevice(original, __uuidof(ID3D12Device), &proxy))) return object;
		original->Release();
		return proxy;
	}

	__declspec(dllexport) void* FlashbangWrapDxgiFactoryTake(void* object) {
		IUnknown* original = (IUnknown*)object;
		void* proxy = nullptr;
		if (!original || FAILED(capture::d3d12::WrapFactory(original, __uuidof(IDXGIFactory), &proxy))) return object;
		original->Release();
		return proxy;
	}

	__declspec(dllexport) void FlashbangTriggerCapture(int32_t frames) { capture::Session::Get().Trigger(frames); }

	// Frame bounds for a program that does not Present: each StartFrame is a frame, captured when one was triggered.
	__declspec(dllexport) void FlashbangStartFrame(void) {
		const std::vector<capture::d3d12::WrappedDevice*> devices = capture::d3d12::Devices();
		const uint64_t frame = capture::Session::Get().NextFrame();
		if (devices.empty() || !capture::Session::Get().TakeTrigger()) return;
		for (capture::d3d12::WrappedDevice* device : devices) device->Frames().ManualStart(frame);
	}

	__declspec(dllexport) void FlashbangEndFrame(void) {
		for (capture::d3d12::WrappedDevice* device : capture::d3d12::Devices()) device->Frames().ManualEnd();
	}

	__declspec(dllexport) int32_t FlashbangIsCapturing(void) { return capture::Session::Get().Capturing() ? 1 : 0; }

	__declspec(dllexport) void FlashbangSetComment(const char* text) { capture::Session::Get().SetComment(text ? text : ""); }

	__declspec(dllexport) void FlashbangAnnotate(const char* key, const char* value) {
		if (key) capture::Session::Get().Annotate(key, value ? value : "");
	}

	__declspec(dllexport) void FlashbangSetCaptureDir(const char* folder) {
		if (folder) capture::Session::Get().SetCaptureDir(folder);
	}

	__declspec(dllexport) int32_t FlashbangCaptureCount(void) { return (int32_t)capture::Session::Get().Captures().size(); }

	// The path of capture `index` (0 = first) in `buffer`; its length, or -1 when there is no such capture. A buffer
	// too small gets nothing; call again with the length + 1.
	__declspec(dllexport) int32_t FlashbangCapturePath(int32_t index, char* buffer, int32_t capacity) {
		const auto captures = capture::Session::Get().Captures();
		if (index < 0 || (size_t)index >= captures.size()) return -1;
		const std::string& path = captures[(size_t)index].path;
		if (buffer && capacity > (int32_t)path.size()) {
			memcpy(buffer, path.data(), path.size());
			buffer[path.size()] = '\0';
		}
		return (int32_t)path.size();
	}
}
