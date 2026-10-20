#include "boundary.h"

struct IProvider { virtual Handle Get(int32_t base) = 0; virtual int32_t Count() = 0; };
struct ICounter { virtual int32_t Next() = 0; };

int32_t ThrowInt(int32_t v) { throw v; }
int32_t ThrowCppError(int32_t code) { throw CppError{ code }; }
int32_t ThrowErrorPointer(int32_t code) { static CppError e; e.code = code; throw &e; }

int32_t CatchFromCallback(int32_t (*callback)(int32_t), int32_t arg)
{
	try { return callback(arg); }
	catch (int v) { return v * 10; }
	catch (const CppError& e) { return e.code * 100; }
	catch (...) { return -1; }
}

int32_t UseProvider(void* provider)
{
	IProvider* p = static_cast<IProvider*>(provider);
	Handle h = p->Get(5);
	return static_cast<int32_t>(h.value) * 10 + p->Count();
}

int32_t UseCounter(void* counter, int32_t times)
{
	ICounter* c = static_cast<ICounter*>(counter);
	int32_t last = 0;
	for (int32_t i = 0; i < times; ++i) last = c->Next();
	return last;
}
