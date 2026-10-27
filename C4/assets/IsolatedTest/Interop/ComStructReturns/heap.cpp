#include "heap.h"

namespace
{
	class Heap final : public IHeap
	{
	public:
		explicit Heap(unsigned long long base) : m_Base(base) {}

		Handle8 CpuStart() override { return Handle8{ m_Base }; }
		Range16 Range() override { return Range16{ m_Base + 64, 32, 3 }; }
		Desc24 Desc() override { return Desc24{ 640, 480, m_Base * 2, 1.5 }; }
		Desc24 Scaled(int factor, double scale) override { return Desc24{ 640 * factor, 480 * factor, m_Base * factor, scale * 2.0 }; }
		unsigned int Release() override
		{
			delete this;
			return 0;
		}

	private:
		unsigned long long m_Base;
	};
}

IHeap* CreateHeap(unsigned long long base) { return new Heap(base); }
