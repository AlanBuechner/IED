// Native ground-truth: replicate the EXACT C4 stress-game DX12 init + CreateCommandList.
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <cstdio>
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
int main(){
    HRESULT hr;
    ID3D12Device* dev=nullptr;
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev);
    fprintf(stderr,"device hr=0x%08X\n",hr); if(hr) return 1;
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT; qd.Flags=(D3D12_COMMAND_QUEUE_FLAGS)0; qd.NodeMask=0; qd.Priority=0;
    ID3D12CommandQueue* q=nullptr;
    hr = dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q);
    fprintf(stderr,"queue hr=0x%08X\n",hr); if(hr) return 1;
    IDXGIFactory2* fac=nullptr;
    hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), (void**)&fac);
    fprintf(stderr,"factory hr=0x%08X\n",hr); if(hr) return 1;
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=(WNDPROC)DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"t";
    RegisterClassExW(&wc);
    HWND hwnd=CreateWindowExW(0,L"t",L"t",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    fprintf(stderr,"hwnd=%p\n",(void*)hwnd); if(!hwnd) return 1;
    ShowWindow(hwnd,SW_SHOW);
    DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width=640; sd.Height=480; sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count=1;
    sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount=2; sd.Scaling=DXGI_SCALING_STRETCH;
    sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.Flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; sd.AlphaMode=DXGI_ALPHA_MODE_UNSPECIFIED;
    IDXGISwapChain3* sc=nullptr;
    hr = fac->CreateSwapChainForHwnd(q,hwnd,&sd,nullptr,nullptr,(IDXGISwapChain1**)&sc);
    fprintf(stderr,"swapchain hr=0x%08X\n",hr); if(hr) return 1;
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors=2; hd.Flags=(D3D12_DESCRIPTOR_HEAP_FLAGS)0; hd.NodeMask=0;
    ID3D12DescriptorHeap* heap=nullptr;
    hr = dev->CreateDescriptorHeap(&hd,__uuidof(ID3D12DescriptorHeap),(void**)&heap);
    fprintf(stderr,"heap hr=0x%08X\n",hr); if(hr) return 1;
    ID3D12Fence* fence=nullptr;
    hr = dev->CreateFence(0,(D3D12_FENCE_FLAGS)0,__uuidof(ID3D12Fence),(void**)&fence);
    fprintf(stderr,"fence hr=0x%08X\n",hr); if(hr) return 1;
    ID3D12CommandAllocator* alloc=nullptr;
    hr = dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),(void**)&alloc);
    fprintf(stderr,"allocator hr=0x%08X ptr=%p\n",hr,(void*)alloc); if(hr) return 1;
    ID3D12GraphicsCommandList* list=nullptr;
    hr = dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list);
    fprintf(stderr,"CreateCommandList #1 hr=0x%08lX ptr=%p\n",hr,(void*)list);
    // SECOND consecutive call, identical args:
    ID3D12GraphicsCommandList* list2=nullptr;
    HRESULT hr2 = dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list2);
    fprintf(stderr,"CreateCommandList #2 hr=0x%08lX ptr=%p\n",hr2,(void*)list2);
    if(hr==0 && list){
        ID3D12Resource* bb=nullptr; sc->GetBuffer(0,__uuidof(ID3D12Resource),(void**)&bb);
        D3D12_RENDER_TARGET_VIEW_DESC rd{}; rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; rd.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
        dev->CreateRenderTargetView(bb,&rd,h);
        list->Reset(alloc,nullptr);
        float cc[4]={0.1f,0.2f,0.3f,1.0f};
        list->ClearRenderTargetView(h,cc,0,nullptr);
        D3D12_RESOURCE_BARRIER bar{}; bar.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; bar.Flags=(D3D12_RESOURCE_BARRIER_FLAGS)0;
        bar.Transition.pResource=bb; bar.Transition.Subresource=0; bar.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT; bar.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1,&bar);
        bar.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET; bar.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1,&bar);
        list->Close();
        ID3D12CommandList* const lists[1]={list};
        q->ExecuteCommandLists(1,lists);
        fence->Signal(1);
        sc->Present(1,DXGI_PRESENT_ALLOW_TEARING);
        fprintf(stderr,"FULL FRAME OK\n");
    }
    return 0;
}
