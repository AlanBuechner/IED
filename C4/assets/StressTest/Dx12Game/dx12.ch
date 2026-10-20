// dx12.ch - DirectX12/DXGI named constants for C4 programs that interop with <d3d12.h>/DXGI.
//
// Like windows.ch, this defines only the #define-style constants that are NOT auto-bound by
// CBind (CBind binds the real enums/structs/COM interfaces from the SDK; it intentionally skips
// preprocessor macros). Values are verified against the Windows SDK (10.0.26100).
//
// NOTE on resource states: d3d12.h has NO `D3D12_RESOURCE_STATES_PRESENT` constant - that name is
// a *combining macro* D3D12_RESOURCE_STATES(x) = (x | (x<<16)). For a single state, use the real
// auto-bound enum members directly: D3D12_RESOURCE_STATE_PRESENT (= 0) and
// D3D12_RESOURCE_STATE_RENDER_TARGET (= 4). Those come from the interop binding, not this header.

// ---- DXGI present flags (dxgi.h) ----
const int32 DXGI_PRESENT_ALLOW_TEARING = 0x00000200;

// ---- DXGI buffer usage flags (dxgitype.h / dxgi.h) ----
const int32 DXGI_USAGE_RENDER_TARGET_OUTPUT = 0x00000020;
