// Native ground-truth #2: replicate the game's EXACT pre-loop sequence, then create TWO
// command allocators in a loop. If native also fails on the 2nd, it's D3D12 behavior; if native
// succeeds, C4's loop-back-edge stack alignment is broken.
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <cstdio>
int main(){
    HRESULT hr;
    ID3D12Device* dev=nullptr;
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev);
    fprintf(stderr,"device 0x%08lX\n",hr); if(hr) return 1;
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT; qd.Flags=(D3D12_COMMAND_QUEUE_FLAGS)0; qd.NodeMask=0; qd.Priority=0;
    ID3D12CommandQueue* q=nullptr; dev->CreateCommandQueue(&qd,__uuidof(ID3D12CommandQueue),(void**)&q);
    IDXGIFactory2* fac=nullptr; CreateDXGIFactory2(0,__uuidof(IDXGIFactory2),(void**)&fac);
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=(WNDPROC)DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"t";
    RegisterClassExW(&wc);
    HWND hwnd=CreateWindowExW(0,L"t",L"t",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ShowWindow(hwnd,SW_SHOW);
    DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width=640; sd.Height=480; sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count=1;
    sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount=2; sd.Scaling=DXGI_SCALING_STRETCH;
    sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.Flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; sd.AlphaMode=DXGI_ALPHA_MODE_UNSPECIFIED;
    IDXGISwapChain3* sc=nullptr; fac->CreateSwapChainForHwnd(q,hwnd,&sd,nullptr,nullptr,(IDXGISwapChain1**)&sc);
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors=2; hd.Flags=(D3D12_DESCRIPTOR_HEAP_FLAGS)0; hd.NodeMask=0;
    ID3D12DescriptorHeap* heap=nullptr; dev->CreateDescriptorHeap(&hd,__uuidof(ID3D12DescriptorHeap),(void**)&heap);
    ID3D12Fence* fence=nullptr; dev->CreateFence(0,(D3D12_FENCE_FLAGS)0,__uuidof(ID3D12Fence),(void**)&fence);
    HANDLE ev=CreateEventA(nullptr,0,0,nullptr);
    // THE LOOP: two allocators, exactly like the game.
    for(int i=0;i<2;i++){
        ID3D12CommandAllocator* a=nullptr;
        HRESULT r = dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),(void**)&a);
        fprintf(stderr,"alloc[%d] 0x%08lX %p\n",i,r,(void*)a);
    }
    return 0;
}
