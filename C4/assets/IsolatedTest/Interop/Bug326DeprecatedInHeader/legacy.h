// A header that deprecates some of what it declares, as the Windows SDK's shobjidl.h does: the shim CBind writes
// forwards all of it.
class ILegacy {
public:
	virtual ~ILegacy() {}
	[[deprecated("use Value")]] virtual int OldValue() = 0;
	virtual int Value() = 0;
};

ILegacy* MakeLegacy(int value);
void FreeLegacy(ILegacy* legacy);
[[deprecated("use Twice")]] inline int OldTwice(int value) { return value * 2; }
inline int Twice(int value) { return value * 2; }
