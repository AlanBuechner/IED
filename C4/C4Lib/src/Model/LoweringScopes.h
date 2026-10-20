#pragma once
#include "Model/ProgramModel.h"

namespace Scripting
{
	// =====================================================================
	// LOWERING SCOPES (doc 06) - the ONLY way scopes change while a function body is lowered. RAII:
	// the guard pops its layer on destruction (including across early returns), so a leak is
	// structurally impossible. Locals registered in a pushed layer shadow outer names for every query
	// until the layer pops - variable shadowing, local types, and function-local declarations all work
	// with no extra API. Template bodies are lowered by pushing a layer holding FAKE ALIASES (T := int32)
	// so `T` resolves to the bound concrete type through the ordinary name lookup - no AST rewriting, ever.
	// A struct/class/enum/function declared INSIDE a body becomes a concrete type/signature in the store,
	// with its NAME registered in the current scope layer: visible exactly where declared, gone when the
	// layer pops; the concrete type stays owned by the store (the model's ownership invariant).
	// =====================================================================
	class LoweringScopes
	{
	public:
		ScopeGuard PushScope(ProgramModel& model, Scope::Kind kind, const Core::String& name);
		bool RegisterLocal(ProgramModel& model, const Core::String& name, SymbolInfo info); // current layer
		const SymbolInfo* FindLocal(const ProgramModel& model, const Core::String& name) const;   // lowering chain only

		QueryResult<const Type*> RegisterLocalType(ProgramModel& model, const AstNode& decl);
		QueryResult<const FunctionRecord*> RegisterLocalFunction(ProgramModel& model, const FunctionDefinitionNode& fn);
		// A local `alias Name = T;` inside a body: resolve T against the current layer and register the
		// NAME as a type symbol in that layer (like a template's fake alias). No HIR node is emitted - only
		// later uses of the name produce nodes. Mirrors RegisterLocalType; without this, a parseable local
		// alias fell through StmtLowerer to an internal "statement not yet lowered" failure.
		QueryResult<const Type*> RegisterLocalAlias(ProgramModel& model, const AliasDefinitionNode& alias);

	private:
		friend class ProgramModel; // the only caller (public verbs delegate here)
	};
} // namespace Scripting
