// Capture: IED's GPU frame capture for a program (IED docs/22 "Capture"), through the capture library IED's Launch for
// Capture names in FLASHBANG_DLL. A D3D12 program hands it its device and DXGI factory right after making them and
// keeps using the proxies it gets back; a Vulkan program needs none of this (the loader brings the capture layer in).
// Without the library - a program not started for capture, or one built for shipping - every call gives back what it
// was given and records nothing. It holds the library and frees it on delete, so it cannot be copied: move it with
// take().
//     Gpu::Capture capture;
//     capture.Load();                                            // FLASHBANG_DLL; false when the program runs alone
//     D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), &device);
//     device = capture.WrapD3D12Device(device);                  // takes the device's reference
//     factory = capture.WrapDxgiFactory(factory);
//     if (key) capture.TriggerCapture(1);                        // or IED's Capture Next Frame
//     capture.StartFrame(); ... capture.EndFrame();              // a frame with no Present (tools, compute)

include "Platform/library.ch"
include "Text/string.ch"

namespace Gpu
{
	class Capture
	{
		[operator("create")] public void Init() {}   // not loaded

		// Loads the capture library FLASHBANG_DLL names; false, with Error(), when it is not set or does not load.
		public bool Load();
		// Loads the capture library at `path`; false, with Error(), when it does not load or is not one.
		public bool Load(const char* path);
		public void Unload();                         // idempotent; proxies made before stop recording, not working
		public bool IsLoaded() const { return wrapDevice != nullptr; }
		public const char* Error() const { return error.CStr(); }
		public int32 Version() const;                // the library's interface version, 0 when not loaded

		// The ID3D12Device (any version) to keep using: a proxy that takes `device`'s reference, or `device` itself
		// when not loaded.
		public void* WrapD3D12Device(void* device);
		// The IDXGIFactory (any version) to keep using, as WrapD3D12Device; swap chains it makes are captured.
		public void* WrapDxgiFactory(void* factory);

		public void TriggerCapture(int32 frames);    // the next `frames` frames
		public void StartFrame();                     // frame bounds for a program that does not Present
		public void EndFrame();
		public bool IsCapturing() const;
		public void SetComment(Core::StringView text);                        // kept with the next capture
		public void Annotate(Core::StringView key, Core::StringView value);  // a note at this point of the frame
		public void SetCaptureFolder(Core::StringView folder);               // instead of FLASHBANG_CAPTURE_DIR
		public int32 CaptureCount() const;            // captures this process has written
		public Core::String CapturePath(int32 index) const;                  // empty for no such capture

		private Platform::Library library;
		private Core::String error;
		private funcptr<int32()> version;
		private funcptr<void*(void*)> wrapDevice;
		private funcptr<void*(void*)> wrapFactory;
		private funcptr<void(int32)> triggerCapture;
		private funcptr<void()> startFrame;
		private funcptr<void()> endFrame;
		private funcptr<int32()> isCapturing;
		private funcptr<void(const char*)> setComment;
		private funcptr<void(const char*, const char*)> annotate;
		private funcptr<void(const char*)> setCaptureFolder;
		private funcptr<int32()> captureCount;
		private funcptr<int32(int32, char*, int32)> capturePath;
	}
}
