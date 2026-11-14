#pragma once
// The MSVC x64 RTTI and EH tables the compiler emits (docs/compiler/04, D-10, D-13; Backend/CodeGen/RttiEmitter.cpp,
// EhTables.cpp), as the runtime reads them: c4_dynamic_cast and c4_instance_of (RttiRuntimeC.c) and, on Linux, the
// exception runtime (ExceptionsPosix.c), where MSVC's own runtime does not read them.
//
// A table refers to another by a 32-bit offset: from the image's base on Windows, as MSVC's runtime reads it, and
// from the referring field itself on Linux (D-86), where an ELF image has no __ImageBase. c4_table_ref reads either;
// 0 is no reference.

#include <stddef.h>
#include <stdint.h>

typedef struct TypeDescriptor { const void* vftable; void* spare; char name[1]; } TypeDescriptor;
typedef struct CompleteObjectLocator { uint32_t signature, offset, cdOffset; int32_t typeDescriptor, classDescriptor, self; } CompleteObjectLocator;
typedef struct ClassHierarchyDescriptor { uint32_t signature, attributes, numBaseClasses; int32_t baseClassArray; } ClassHierarchyDescriptor;
typedef struct BaseClassDescriptor { int32_t typeDescriptor; uint32_t numContainedBases; int32_t mdisp, pdisp, vdisp; uint32_t attributes; int32_t classDescriptor; } BaseClassDescriptor;

// What a throw names (MSVC's _ThrowInfo, _CatchableTypeArray, _CatchableType): the thrown object's destructor and every
// type a handler may catch it as, with the offset of that sub-object and how to copy it.
typedef struct ThrowInfo { uint32_t attributes; int32_t destroy, forwardCompat, catchableTypes; } ThrowInfo;
typedef struct CatchableTypeArray { int32_t count; int32_t types[1]; } CatchableTypeArray;
typedef struct CatchableType { uint32_t properties; int32_t typeDescriptor; int32_t mdisp, pdisp, vdisp; int32_t size; int32_t copy; } CatchableType;

enum
{
	kCatchableSimpleType = 1, // copied as bits (a scalar or a pointer)
	kHandlerConst = 1,
	kHandlerReference = 8,
	kHandlerCatchAll = 64,
};

// The image's base a table's offsets count from (Windows), found through the locator's offset to itself; any value on
// Linux, where c4_table_ref does not use it.
static inline const char* c4_table_image(const CompleteObjectLocator* col)
{
	return (const char*)col - col->self;
}

static inline const void* c4_table_ref(const char* image, const int32_t* field)
{
	if (!*field) return NULL;
#if defined(_WIN32)
	return image + *field;
#else
	(void)image;
	return (const char*)field + *field;
#endif
}
