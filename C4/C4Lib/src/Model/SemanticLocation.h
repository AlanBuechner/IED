#pragma once
#include "Core/Core.h"

namespace Scripting
{
	class CompilationUnit;
	struct Scope;
	class FunctionType;
	class ObjectType;
	struct TemplateBinding;

	/// Immutable per-function semantic record, built once during COLLECT. The target shape for any
	/// "current function" state: anything that depends on WHICH function is being worked on asks its
	/// SemanticLocation for this, instead of reading shared mutable state. (The two ModuleContext
	/// instances of it - return type and containing type - are already gone: the return type is
	/// stamped on each ReturnNode and the containing type lives on the HIR call/method nodes.)
	struct FunctionScopeInfo
	{
		const FunctionType* type = nullptr;          // resolved signature (return + params)
		const ObjectType* containingType = nullptr;  // for methods; null for free functions. `this` denotes this.
		const Scope* paramScope = nullptr;           // scope holding the parameters (+ this for methods)
		bool isTemplateInstantiation = false;        // body lowered from a template shell
		const TemplateBinding* bindings = nullptr;   // non-null iff isTemplateInstantiation

		const Type* ReturnType() const { return type ? type->GetReturnType() : nullptr; }
		uint32 ParamCount() const { return type ? type->GetParameterCount() : 0; }
		const Type* ParamType(uint32 i) const
		{
			if (!type || i >= type->GetParameterCount()) return nullptr;
			return type->GetParameters()[i].m_Type;
		}
	};

	/// WHO is asking a semantic question: the source unit plus a position in its scope chain.
	/// Captured by value and passed with the work - there is no "current function" global, so a
	/// query can never see another function's symbols (the stale-`this` class of bug, fix #48).
	struct SemanticLocation
	{
		const CompilationUnit* unit = nullptr;   // which source unit (null = program scope)
		const Scope* scope = nullptr;            // innermost scope of the asking context
		const FunctionScopeInfo* fn = nullptr;   // enclosing function; null at file scope

		/// Location for a nested block inside this one (same function, deeper scope).
		SemanticLocation InBlock(const Scope* blockScope) const
		{
			SemanticLocation l = *this;
			l.scope = blockScope ? blockScope : scope;
			return l;
		}

		bool InFunction() const { return fn != nullptr; }
		const ObjectType* ContainingType() const { return fn ? fn->containingType : nullptr; }
	};

} // namespace Scripting
