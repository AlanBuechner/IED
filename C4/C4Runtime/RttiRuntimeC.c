// C4 RTTI runtime: checked casts over the MSVC x64 RTTI tables the compiler emits (docs/compiler/04,
// D-10, RttiTablesC.h). Linked into every executable (EnsureRttiRuntimeObject in BuildUtils).
//
// A polymorphic object's vptr points at its vtable; vtable[-1] is the CompleteObjectLocator, which gives
// the offset of that vptr inside the complete object and the complete type's base class array.
// Type identity is the TypeDescriptor's address or, across modules, its decorated name - the rule the
// MSVC frame handler uses, so C++ types match too. C4 has no virtual bases (PMD.pdisp is always -1).

#include "PlatformRuntimeC.h"
#include "RttiTablesC.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	const char* image = c4_table_image(col);
	char* complete = (char*)obj - col->offset;
	const ClassHierarchyDescriptor* chd = (const ClassHierarchyDescriptor*)c4_table_ref(image, &col->classDescriptor);
	const int32_t* bases = (const int32_t*)c4_table_ref(image, &chd->baseClassArray);
	for (uint32_t i = 0; i < chd->numBaseClasses; ++i)
	{
		const BaseClassDescriptor* bcd = (const BaseClassDescriptor*)c4_table_ref(image, &bases[i]);
		if (!same_type((const TypeDescriptor*)c4_table_ref(image, &bcd->typeDescriptor), target)) continue;
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
	c4_fatal("pure virtual method called");
}
