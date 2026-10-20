// C4 RTTI runtime: checked casts over the MSVC x64 RTTI tables the compiler emits (docs/compiler/04,
// D-10). Linked into every executable (EnsureRttiRuntimeObject in BuildUtils).
//
// A polymorphic object's vptr points at its vtable; vtable[-1] is the CompleteObjectLocator, which gives
// the offset of that vptr inside the complete object and the complete type's base class array. Every
// table stores 32-bit offsets from the image base, which the locator's own `self` entry recovers.
// Type identity is the TypeDescriptor's address or, across modules, its decorated name - the rule the
// MSVC frame handler uses, so C++ types match too. C4 has no virtual bases (PMD.pdisp is always -1).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TypeDescriptor { const void* vftable; void* spare; char name[1]; } TypeDescriptor;
typedef struct CompleteObjectLocator { uint32_t signature, offset, cdOffset; int32_t typeDescriptor, classDescriptor, self; } CompleteObjectLocator;
typedef struct ClassHierarchyDescriptor { uint32_t signature, attributes, numBaseClasses; int32_t baseClassArray; } ClassHierarchyDescriptor;
typedef struct BaseClassDescriptor { int32_t typeDescriptor; uint32_t numContainedBases; int32_t mdisp, pdisp, vdisp; uint32_t attributes; int32_t classDescriptor; } BaseClassDescriptor;

enum { BASE_AMBIGUOUS = 2 };

typedef struct Match { char* address; int ambiguous; int found; } Match;

static int same_type(const TypeDescriptor* a, const TypeDescriptor* b)
{
	return a == b || strcmp(a->name, b->name) == 0;
}

// The sub-object of the complete object behind `obj` whose type is `target`.
static Match find_base(void* obj, const TypeDescriptor* target)
{
	Match m = { 0, 0, 0 };
	if (!obj || !target) return m;
	const CompleteObjectLocator* col = ((const CompleteObjectLocator* const*)(*(void* const*)obj))[-1];
	const char* image = (const char*)col - col->self;
	char* complete = (char*)obj - col->offset;
	const ClassHierarchyDescriptor* chd = (const ClassHierarchyDescriptor*)(image + col->classDescriptor);
	const int32_t* bases = (const int32_t*)(image + chd->baseClassArray);
	for (uint32_t i = 0; i < chd->numBaseClasses; ++i)
	{
		const BaseClassDescriptor* bcd = (const BaseClassDescriptor*)(image + bases[i]);
		if (!same_type((const TypeDescriptor*)(image + bcd->typeDescriptor), target)) continue;
		if (m.found || (bcd->attributes & BASE_AMBIGUOUS)) m.ambiguous = 1;
		if (!m.found) m.address = complete + bcd->mdisp;
		m.found = 1;
	}
	return m;
}

// `p as T*` / dynamic_cast<T*>(p): the `T` sub-object, or null when the dynamic type is not a `T` or
// contains more than one.
void* c4_dynamic_cast(void* obj, const void* target)
{
	Match m = find_base(obj, (const TypeDescriptor*)target);
	return m.found && !m.ambiguous ? m.address : 0;
}

// `p is T`: the dynamic type is a `T`, derives from it or implements it.
int32_t c4_instance_of(void* obj, const void* target)
{
	return find_base(obj, (const TypeDescriptor*)target).found;
}

// A vtable entry with no implementation (an abstract method reached on a constructed object).
void c4_purecall(void)
{
	fputs("C4: fatal: pure virtual method called\n", stderr);
	exit(1);
}
