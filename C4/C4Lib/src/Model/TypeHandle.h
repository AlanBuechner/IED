#pragma once
#include "Types.h"
#include "Core/Core.h"

namespace Scripting
{

	/// The abstract type object: wraps a const Type* and hides its shape behind capability
	/// queries. Consumers (HIR lowering, codegen) ask "does this support X?" instead of switching
	/// on TypeKind and manually unwrapping Pointer/GcRef/Reference/Alias chains - those wrapper
	/// conventions live here in exactly one place.
	///
	/// A handle is a value type: cheap to copy, safe to store on HIR nodes, comparable by the
	/// canonical pointer identity of the underlying type (TypeRegistry interns wrappers).
	class TypeHandle
	{
	public:
		TypeHandle() = default;
		TypeHandle(const Type* t) : m_Type(t) {}  // implicit: HIR nodes and queries hand out const Type*

		bool IsValid() const { return m_Type != nullptr; }
		explicit operator bool() const { return m_Type != nullptr; }

		/// Escape hatch for the mechanical LLVM-mapping layer (TypeLowering) only. Semantic
		/// decision code should use the capability queries below instead.
		const Type* Raw() const { return m_Type; }

		TypeKind GetKind() const { return m_Type ? m_Type->GetKind() : TypeKind::Invalid; }
		bool IsSameAs(TypeHandle other) const { return m_Type == other.m_Type; }
		bool operator==(TypeHandle other) const { return m_Type == other.m_Type; }
		bool operator!=(TypeHandle other) const { return m_Type != other.m_Type; }

		Core::String GetName() const { return m_Type ? m_Type->GetName() : Core::String(); }
		Core::String ToString() const { return m_Type ? m_Type->ToString() : Core::String("<none>"); }

		// Layout facts (valid once the semantic RESOLVE pass has run on the type).
		uint64 GetSize() const { return m_Type ? m_Type->GetSize() : 0; }
		uint32 GetAlignment() const { return m_Type ? static_cast<uint32>(m_Type->GetAlignment()) : 0; }

		// Numeric classification. IsNumeric excludes enums by design - enum values compare
		// through the primitive path but are not arithmetic operands (see fix #25).
		bool IsNumeric() const { return m_Type && m_Type->IsNumeric(); }
		bool IsIntegral() const { return m_Type && m_Type->IsIntegral(); }
		bool IsFloatingPoint() const { return m_Type && m_Type->IsFloatingPoint(); }
		bool IsUnsignedInteger() const { return m_Type && m_Type->IsUnsignedInteger(); }

		// Indirection. Pointee strips exactly ONE wrapper layer (Pointer, GcRef, or Reference);
		// for non-pointer-like kinds it returns the handle itself. DerefAll strips every layer to
		// the base type (aliases are NOT followed - use ResolveAlias for that).
		TypeHandle Pointee() const;
		TypeHandle DerefAll() const;
		bool IsPointerLike() const;      // Pointer, GcRef, or Reference
		bool IsReference() const { return m_Type && m_Type->GetKind() == TypeKind::Reference; }
		bool IsArray() const { return m_Type && m_Type->GetKind() == TypeKind::Array; }
		TypeHandle ResolveAlias() const; // follows AliasType layers to the aliased type

		// Indirection bricks (raw-pointer form for code that works with const Type*). These are THE
		// single place pointer/reference/alias unwrapping lives - they moved here off the Type base
		// class because indirection reasoning is a wrapper concern, and this handle is the definitive
		// wrapper API. Every call site below uses these instead of hand-rolled unwrap loops.
		/// The type one layer in (Pointer/Reference); returns `t` unchanged for other kinds.
		static const Type* GetPointedType(const Type* t);
		/// Strip all Pointer/Reference layers to the base type they ultimately refer to. Does NOT
		/// follow alias wrappers (callers that need that do it explicitly).
		static const Type* DerefToBase(const Type* t);
		/// Strip the transparent VALUE-IDENTITY layers - Reference and Alias - to reach the underlying
		/// value type. Pointers are NOT stripped: a pointer is a first-class value (int32* is not int32),
		/// so deref / delete / pointer-member-access must see the pointer, not its pointee. Use
		/// GetPointedType or DerefToBase when you specifically want to go through a pointer. THE brick for
		/// member access and field resolution: a missed alias silently degrades to an ambiguous all-types-
		/// by-name scan that binds the wrong template instantiation, so no call site may roll its own loop.
		static const Type* UnwrapToValue(const Type* t);
		/// Whether a value of this type has a well-defined truth value usable in a boolean context
		/// (if/while condition, && / || operand): numbers, enums, pointers and function pointers do -
		/// `if (ptr)`, `if (i)` and C-API BOOL returns are valid idioms. A reference has its referent's.
		/// Objects, arrays and packed vectors do NOT. THE single brick for this decision; callers must not
		/// re-derive the kind list, or a new scalar kind silently stops being usable.
		static bool HasTruthValue(const Type* t);
		/// Whether a value of this type may contain a managed pointer (a pointer or reference anywhere in it,
		/// through arrays, fields and bases). The collector scans only such values: heap objects of other
		/// types are allocated noscan, and only such globals are roots.
		static bool MayHoldPointers(const Type* t);

		// Capability queries - the point of the handle. Each unwraps indirection itself, so a
		// caller never distinguishes "pointer to object" from "gc-ref to object".
		bool SupportsIndexing() const;   // array or pointer-like
		const ObjectType* AsObject() const;  // the object type this handle denotes (through pointers/refs), or nullptr
		const ObjectType::FieldInfo* FindField(const Core::String& name) const; // own + inherited fields of AsObject
		/// True when `opName` (canonical annotation, e.g. "add", "equal", "index") is supported for
		/// this type with the given other operand: built-in support, or a [operator(...)] method on
		/// this type's own/base chain (outTarget, when non-null, receives the dispatch target).
		bool SupportsOperator(const Core::String& opName, TypeHandle other, const ObjectType::MethodInfo** outTarget = nullptr) const;

	private:
		const Type* m_Type = nullptr;
	};

} // namespace Scripting
