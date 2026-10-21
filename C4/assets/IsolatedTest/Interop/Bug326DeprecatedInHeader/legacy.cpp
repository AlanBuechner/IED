#include "legacy.h"

class Legacy : public ILegacy {
public:
	explicit Legacy(int value) : m_Value(value) {}
	int OldValue() override { return m_Value; }
	int Value() override { return m_Value; }
private:
	int m_Value;
};

ILegacy* MakeLegacy(int value) { return new Legacy(value); }
void FreeLegacy(ILegacy* legacy) { delete legacy; }
