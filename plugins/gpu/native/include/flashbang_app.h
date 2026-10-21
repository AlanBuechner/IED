// flashbang_app.h - IED's GPU capture for C and C++ programs (docs/22 "Capture"; C4 programs use Bedrock Gpu::Capture).
// Load the library at startup, hand it your D3D12 device and DXGI factory right after making them, and keep using the
// pointers it gives back; Vulkan programs need none of this (the loader brings the layer in). Without the library -
// a program not started by IED for capture, or one built for shipping - every call gives back what it was given.
//
//     flashbang::Library capture;
//     flashbang::Load(capture);                                  // FLASHBANG_DLL, set by IED's Launch for Capture
//     D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
//     device = flashbang::WrapDevice(capture, device);           // takes your reference, returns the one to keep
//     factory = flashbang::WrapFactory(capture, factory);
//     ...
//     if (keyPressed) flashbang::TriggerCapture(capture, 1);    // or IED's Capture Next Frame
#pragma once

#include <windows.h>
#include <stdint.h>

namespace flashbang
{
	struct Library
	{
		HMODULE module = nullptr;
		void* (*wrapDevice)(void*) = nullptr;
		void* (*wrapFactory)(void*) = nullptr;
		void (*triggerCapture)(int32_t) = nullptr;
		void (*startFrame)(void) = nullptr;
		void (*endFrame)(void) = nullptr;
		int32_t (*isCapturing)(void) = nullptr;
		void (*setComment)(const char*) = nullptr;
		void (*annotate)(const char*, const char*) = nullptr;
	};

	// Loads the capture library from `path`, else from the FLASHBANG_DLL environment variable; false when neither
	// names one that loads.
	inline bool Load(Library& library, const wchar_t* path = nullptr) {
		wchar_t named[MAX_PATH * 2] = {};
		if (!path) {
			const DWORD length = GetEnvironmentVariableW(L"FLASHBANG_DLL", named, MAX_PATH * 2);
			if (length == 0 || length >= MAX_PATH * 2) return false;
			path = named;
		}
		library.module = LoadLibraryW(path);
		if (!library.module) return false;
		library.wrapDevice = (void* (*)(void*))GetProcAddress(library.module, "FlashbangWrapD3D12DeviceTake");
		library.wrapFactory = (void* (*)(void*))GetProcAddress(library.module, "FlashbangWrapDxgiFactoryTake");
		library.triggerCapture = (void (*)(int32_t))GetProcAddress(library.module, "FlashbangTriggerCapture");
		library.startFrame = (void (*)(void))GetProcAddress(library.module, "FlashbangStartFrame");
		library.endFrame = (void (*)(void))GetProcAddress(library.module, "FlashbangEndFrame");
		library.isCapturing = (int32_t (*)(void))GetProcAddress(library.module, "FlashbangIsCapturing");
		library.setComment = (void (*)(const char*))GetProcAddress(library.module, "FlashbangSetComment");
		library.annotate = (void (*)(const char*, const char*))GetProcAddress(library.module, "FlashbangAnnotate");
		return library.wrapDevice != nullptr;
	}

	template <class T> T* WrapDevice(Library& library, T* device) { return library.wrapDevice ? (T*)library.wrapDevice(device) : device; }
	template <class T> T* WrapFactory(Library& library, T* factory) { return library.wrapFactory ? (T*)library.wrapFactory(factory) : factory; }

	inline void TriggerCapture(Library& library, int32_t frames = 1) {
		if (library.triggerCapture) library.triggerCapture(frames);
	}
	inline void StartFrame(Library& library) {
		if (library.startFrame) library.startFrame();
	}
	inline void EndFrame(Library& library) {
		if (library.endFrame) library.endFrame();
	}
	inline bool IsCapturing(Library& library) { return library.isCapturing && library.isCapturing() != 0; }
	inline void SetComment(Library& library, const char* text) {
		if (library.setComment) library.setComment(text);
	}
	inline void Annotate(Library& library, const char* key, const char* value) {
		if (library.annotate) library.annotate(key, value);
	}
}
