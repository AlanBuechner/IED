// flashbang.dll: the capture library (docs/22 "Capture"). Loading it does nothing by itself: the session and its control
// pipe start with the first call of the API (a wrapped device, a trigger) or the Vulkan loader's first use of the layer.
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
	return TRUE;
}
