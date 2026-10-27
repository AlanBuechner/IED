// A COM-style interface whose methods return structs by value, as D3D12's do (GetCPUDescriptorHandleForHeapStart,
// GetDesc, GetResourceAllocationInfo): through MSVC's member-function convention, with a hidden result pointer after
// `this` even for 8 bytes.
struct Handle8 { unsigned long long ptr; };
struct Range16 { unsigned long long ptr; int count; int flags; };
struct Desc24 { int width; int height; unsigned long long bytes; double scale; };

class IHeap
{
public:
	virtual Handle8 CpuStart() = 0;
	virtual Range16 Range() = 0;
	virtual Desc24 Desc() = 0;
	virtual Desc24 Scaled(int factor, double scale) = 0; // arguments after the hidden result pointer
	virtual unsigned int Release() = 0;
};

IHeap* CreateHeap(unsigned long long base);
