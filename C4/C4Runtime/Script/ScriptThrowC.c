// Throwing from script code (docs/runtime/04 section 6.3). MSVC's throw information is relative to the image that
// holds it; _CxxThrowException finds that image with RtlPcToFileHeader, which knows only the images the loader
// mapped. A script image registers its range here, and its throws pass the base themselves.

#include "ScriptRuntimeC.h"
#include <stdlib.h>
#include <windows.h>

enum
{
	kCxxExceptionCode = 0xE06D7363,  // 'msc' | 0xE0000000: a C++ exception
	kCxxMagic = 0x19930520,          // the throw information format of MSVC since 2005
	kCxxParameters = 4,
};

typedef struct ScriptImage
{
	uintptr_t base;
	uintptr_t end;
} ScriptImage;

static SRWLOCK g_lock = SRWLOCK_INIT;
static ScriptImage* g_images;
static size_t g_count;
static size_t g_capacity;

void c4_script_image_add(const void* base, const void* end)
{
	AcquireSRWLockExclusive(&g_lock);
	if (g_count == g_capacity)
	{
		const size_t capacity = g_capacity ? g_capacity * 2 : 8;
		ScriptImage* grown = (ScriptImage*)realloc(g_images, capacity * sizeof(ScriptImage));
		if (grown)
		{
			g_images = grown;
			g_capacity = capacity;
		}
	}
	if (g_count < g_capacity)
	{
		g_images[g_count].base = (uintptr_t)base;
		g_images[g_count].end = (uintptr_t)end;
		g_count++;
	}
	ReleaseSRWLockExclusive(&g_lock);
}

void c4_script_image_remove(const void* base)
{
	AcquireSRWLockExclusive(&g_lock);
	for (size_t i = 0; i < g_count; i++)
	{
		if (g_images[i].base != (uintptr_t)base) continue;
		g_images[i] = g_images[--g_count];
		break;
	}
	ReleaseSRWLockExclusive(&g_lock);
}

static uintptr_t image_base_of(const void* address)
{
	uintptr_t base = 0;
	AcquireSRWLockShared(&g_lock);
	for (size_t i = 0; i < g_count; i++)
		if ((uintptr_t)address >= g_images[i].base && (uintptr_t)address < g_images[i].end) { base = g_images[i].base; break; }
	ReleaseSRWLockShared(&g_lock);
	return base;
}

void c4_script_throw(void* object, const void* throwInfo)
{
	const ULONG_PTR parameters[kCxxParameters] = { kCxxMagic, (ULONG_PTR)object, (ULONG_PTR)throwInfo,
		throwInfo ? image_base_of(throwInfo) : 0 };
	RaiseException(kCxxExceptionCode, EXCEPTION_NONCONTINUABLE, kCxxParameters, parameters);
	__builtin_unreachable();
}
