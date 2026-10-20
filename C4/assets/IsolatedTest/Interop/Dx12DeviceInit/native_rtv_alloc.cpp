// Native ground-truth #3: allocator -> GetBuffer -> CreateRenderTargetView -> allocator again.
// If the 2nd allocator fails natively too, it's D3D12 behavior (RTV creation poisons alloc).
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <cstdio>
int main(){
    HRESULT hr;
    ID3D12Device* dev=nullptr;
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev);
    if(hr) return 1;
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

    D3D12_CPU_DESCRIPTOR_HANDLE base = heap->GetCPUDescriptorHandleForHeapStart();
    UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    fprintf(stderr,"base=%p inc=%u\n",(void*)base.ptr,inc);

    for(int i=0;i<2;i++){
        ID3D12CommandAllocator* a=nullptr;
        HRESULT ra = dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),(void**)&a);
        fprintf(stderr,"iter %d alloc 0x%08lX %p\n",i,ra,(void*)a); if(ra) return 1;
        ID3D12Resource* bb=nullptr; sc->GetBuffer(i,__uuidof(ID3D12Resource),(void**)&bb);
        D3D12_RENDER_TARGET_VIEW_DESC rd{}; rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; rd.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE h = base; h.ptr += (SIZE_T)i*inc;
        dev->CreateRenderTargetView(bb,&rd,h);
        fprintf(stderr,"  buffer=%p rtv done\n",(void*)bb);
        bb->Release();
    }
    fprintf(stderr,"DONE\n");
    return 0;
}
