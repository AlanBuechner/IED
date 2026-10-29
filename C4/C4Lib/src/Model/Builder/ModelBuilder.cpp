#include "Model/Builder/ModelBuilder.h"
#include "Parser/Nodes/Statements/AsmStatementNode.h"
#include "Model/Templates/MemberTemplates.h"
#include "Model/Types/AliasType.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/ValueContainment.h"
#include "Model/Types/VirtualTables.h"
#include "Model/Abstractness.h"
#include "Model/TargetAnnotation.h"
#include "Model/Builder/ObjectIdentity.h"
#include "Model/Builder/ModelResolver.h" // pass 2: fixed-point resolution + GAP A body scan
#include "Model/Builder/FreeFunctionResolver.h"
#include "Model/Templates/TemplateMethodDefinitions.h"
#include "Compiler/CompilerInstance.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h" // HasTemplateArgs: body-scanning pass (GAP A)
#include "Model/ConstEvaluator.h"
#include "Model/OperatorAnnotations.h" // [group("name")] collection for reflection (plan 14)
#include <functional>

namespace Scripting
{
	ModelBuilder::ModelBuilder(ErrorTracker* errors) : m_Errors(errors) {}

	void ModelBuilder::ErrorAt(const Context& ctx, const AstNode* where, const Core::String& message)
	{
		if (ctx.errors) ctx.errors->AddError(where ? where->startLocation : SourceLocation(), message, ErrorSeverity::Error);
	}

	static const char* SymbolKindPhrase(SymbolKind kind)
	{
		switch (kind)
		{
			case SymbolKind::Type: return "a type";
			case SymbolKind::Function: return "a function";
			case SymbolKind::Variable: return "a variable";
			default: return "a namespace";
		}
	}

	Core::String ModelBuilder::Redefinition(const Context& ctx, const char* what, const Core::String& name) const
	{
		Core::String message = "redefinition of " + Core::String(what) + " '" + name + "'";
		const SymbolInfo* existing = ctx.scope->Find(name);
		if (!existing) return message;
		message += ": it is already " + Core::String(SymbolKindPhrase(existing->kind));
		const Core::String file = ctx.model->SourceName(existing->definingUnit);
		if (existing->declNode && !file.empty())
			message += " (" + file + ":" + Core::String(std::to_string(existing->declNode->startLocation.line)) + ")";
		return message;
	}

	bool ModelBuilder::IsBinding(uint32 unitId, const AstNode& declaration) const
	{
		if (!m_Sources || unitId == 0 || !m_Sources->HasInfo(unitId)) return false;
		const uint32 line = m_Sources->GetInfo(unitId).bindingsLine;
		return line != 0 && declaration.startLocation.line >= line;
	}

	std::string_view ModelBuilder::DeclarationText(uint32 unitId, const AstNode& declaration) const
	{
		const SourceInfo& info = m_Sources->GetInfo(unitId);
		const size_t start = declaration.startLocation.byteOffset;
		const size_t end = declaration.endLocation.byteOffset;
		if (!info.source || start >= end || end > info.source->Size()) return {};
		return std::string_view(info.source->Data() + start, end - start);
	}

	bool ModelBuilder::RepeatsBinding(const Context& ctx, const AstNode& declaration, const Core::String& name)
	{
		if (!IsBinding(ctx.unitId, declaration)) return false;
		const std::string_view text = DeclarationText(ctx.unitId, declaration);
		if (text.empty()) return false;
		for (const auto& entry : ctx.scope->Entries(name))
		{
			if (!entry->declNode || !IsBinding(entry->definingUnit, *entry->declNode)) continue;
			if (DeclarationText(entry->definingUnit, *entry->declNode) != text) continue;
			bool known = entry->definingUnit == ctx.unitId;
			for (uint32 unit : entry->repeatedIn) known = known || unit == ctx.unitId;
			if (!known) entry->repeatedIn.Push(ctx.unitId);
			return true;
		}
		return false;
	}

	Core::String ModelBuilder::Why(const Context& ctx, const AstNode* type, uint32 unitId, const Scope* scope) const
	{
		// The failure of resolving `type` again, after every declaration has had its chance.
		const auto* qualified = dynamic_cast<const QualTypeNode*>(type);
		if (!qualified) return "";
		auto resolved = ctx.model->ResolveTypeInContext(*qualified, unitId, scope);
		return resolved.Ok() ? Core::String() : ": " + resolved.message;
	}

	// `what` did not resolve: reported with the reason, at the part of `type` that fails when the resolver knows it
	// (an array size), else at `declaration`.
	void ModelBuilder::ReportUnresolved(const Context& ctx, const AstNode* declaration, const Core::String& what, const AstNode* type,
		uint32 unitId, const Scope* scope)
	{
		const auto* qualified = dynamic_cast<const QualTypeNode*>(type);
		if (!qualified || !ctx.errors) { ErrorAt(ctx, declaration, what); return; }
		auto resolved = ctx.model->ResolveTypeInContext(*qualified, unitId, scope);
		if (resolved.Ok()) { ErrorAt(ctx, declaration, what); return; }
		const SourceLocation at = resolved.loc.line ? resolved.loc : (declaration ? declaration->startLocation : SourceLocation());
		ctx.errors->AddError(at, what + ": " + resolved.message, ErrorSeverity::Error);
	}

	Core::String ModelBuilder::SignatureWhy(const Context& ctx, const FunctionDefinitionNode& fn, uint32 unitId, const Scope* scope) const
	{
		if (Core::String why = Why(ctx, fn.GetReturnType().Get(), unitId, scope); !why.empty()) return why;
		for (const auto& p : fn.GetParameters())
			if (Core::String why = Why(ctx, p->GetType().Get(), unitId, scope); !why.empty()) return why;
		return "";
	}

	// `Q::F` stays unresolved because nothing is named Q, or because a type in its signature does not resolve.
	Core::String ModelBuilder::QualifiedWhy(const Context& ctx, const PendingQualifiedFunc& f) const
	{
		const SymbolInfo* qualifier = FreeFunctionResolver::QualifierOf(*ctx.model, f);
		if (!qualifier) return ": no namespace or type named '" + f.prefix + "'";
		const auto* owner = qualifier->kind == SymbolKind::Type ? dynamic_cast<const ObjectType*>(qualifier->type) : nullptr;
		const Scope* scope = owner ? ctx.model->TypeScope(owner) : qualifier->scope ? qualifier->scope : f.declScope;
		return SignatureWhy(ctx, *f.fn, f.unitId, scope);
	}

	namespace
	{
		QueryResult<void> CheckHierarchy(const ObjectType* obj)
		{
			auto bases = ClassHierarchy::CheckBaseList(obj);
			return bases.Ok() ? ClassHierarchy::CheckInterfaceMembers(obj) : bases;
		}

		QueryResult<void> CheckSize(const ObjectType& obj)
		{
			Core::String why = ObjectSizeProblem(obj);
			return why.empty() ? QueryResult<void>::Success() : QueryResult<void>::Fail(QueryError::NotAValue, why);
		}

		// A field holds a whole object, so its type cannot be abstract (D-29).
		QueryResult<void> CheckFieldValues(const ProgramModel& model, const ObjectType* obj)
		{
			for (const auto& f : obj->GetFields())
				if (Core::String why = Abstractness::WhyNoValue(model, f.m_Type); !why.empty())
					return QueryResult<void>::Fail(QueryError::NotAValue, Abstractness::ByValue("field '" + f.mName + "'", why),
						f.m_DeclaringNode ? f.m_DeclaringNode->startLocation : SourceLocation());
			return QueryResult<void>::Success();
		}

		// A pure packed vector copies and goes away as one register value, so a delete or copy of its own could never
		// run. Checked on the type, not the method: a template instance lowers only the methods it calls.
		QueryResult<void> CheckPackedLifecycle(const ProgramModel& model, const ObjectType* obj)
		{
			if (!obj->IsPurePackedVector()) return QueryResult<void>::Success();
			for (uint32 i = 0; i < obj->GetMethodCount(); ++i)
			{
				const FunctionRecord* rec = model.RecordForMethod(obj, &obj->GetMethods()[i]);
				if (!rec || rec->isStatic) continue;
				for (const Core::String& op : rec->operatorAnnotations)
					if (op == "delete" || op == "copy")
						return QueryResult<void>::Fail(QueryError::NotAValue, "'" + obj->GetName() + "' is a packed vector, which copies and goes "
							"away as one register value: it cannot have [operator(\"" + op + "\")] (keep what needs it in a class that holds the vector)",
							rec->declarations.Empty() ? SourceLocation() : rec->declarations[0]->startLocation);
			}
			return QueryResult<void>::Success();
		}

		bool IsVoid(const Type* t)
		{
			while (t && t->GetKind() == TypeKind::Alias) t = static_cast<const AliasType*>(t)->GetAliasedType();
			return t && t->GetKind() == TypeKind::Void;
		}

		// A parameter holds a value, so `void` is no parameter's type; C's `(void)` alone is an empty list (BUG-391).
		QueryResult<void> CheckParameterTypes(const FunctionRecord& rec)
		{
			if (!rec.type) return QueryResult<void>::Success();
			const auto& params = rec.type->GetParameters();
			for (uint32 i = 0; i < params.Count(); ++i)
				if (IsVoid(params[i].m_Type))
				{
					const Core::String name = i < rec.paramNames.Count() && !rec.paramNames[i].empty() ? "'" + rec.paramNames[i] + "'" : "#" + std::to_string(i + 1);
					return QueryResult<void>::Fail(QueryError::NotAValue, "parameter " + name + " of '" + rec.name + "' has type void: a parameter "
						"holds a value (a function without parameters is written " + rec.name + "() or " + rec.name + "(void))",
						rec.declarations.Empty() ? SourceLocation() : rec.declarations[0]->startLocation);
				}
			return QueryResult<void>::Success();
		}

		void Report(ErrorTracker* errors, const ObjectType* obj, const QueryResult<void>& rule)
		{
			if (!rule.Ok() && errors) errors->AddError(rule.loc.line ? rule.loc : obj->GetDeclLocation(), rule.message, ErrorSeverity::Error);
		}

		// A global holds a whole object too; static members are checked as fields.
		void CheckGlobalValues(ErrorTracker* errors, const ProgramModel& model)
		{
			model.Symbols()->ForEachGlobalVariable([&](const SymbolInfo& sym, const Scope& scope) {
				if (scope.GetKind() == Scope::Kind::Class || !sym.type || !errors) return;
				if (Core::String why = Abstractness::WhyNoValue(model, sym.type); !why.empty())
					errors->AddError(sym.declNode ? sym.declNode->startLocation : SourceLocation(), Abstractness::ByValue("'" + sym.plainName + "'", why),
						ErrorSeverity::Error);
			});
		}
	}

	void ModelBuilder::CheckObjectTypes(const Context& ctx, ProgramModel& model)
	{
		for (const ObjectType* obj : model.ObjectTypes())
		{
			Report(ctx.errors, obj, CheckHierarchy(obj));
			Report(ctx.errors, obj, ValueContainment::CheckNotSelfContaining(obj));
			Report(ctx.errors, obj, CheckSize(*obj));
			Report(ctx.errors, obj, VirtualTables::CheckFinalMethods(obj));
			Report(ctx.errors, obj, CheckFieldValues(model, obj));
			Report(ctx.errors, obj, CheckPackedLifecycle(model, obj));
		}
		CheckGlobalValues(ctx.errors, model);
		Core::Vector<const FunctionRecord*> records;
		model.AllFunctionRecords(records);
		for (const FunctionRecord* rec : records)
		{
			if (auto names = CheckLifecycleOperators(*rec); !names.Ok() && ctx.errors)
				ctx.errors->AddError(rec->bodyNode ? rec->bodyNode->startLocation : SourceLocation(), names.message, ErrorSeverity::Error);
			if (auto params = CheckParameterTypes(*rec); !params.Ok() && ctx.errors)
				ctx.errors->AddError(params.loc, params.message, ErrorSeverity::Error);
		}
		CheckTargets(ctx, model, records);
	}

	// A [target] names features of the build's processor, and its function returns no vector whose register would
	// depend on the target: the function is built for its target with the features on.
	void ModelBuilder::CheckTargets(const Context& ctx, const ProgramModel& model, const Core::Vector<const FunctionRecord*>& records)
	{
		for (const FunctionRecord* rec : records)
		{
			const std::vector<std::string> features = TargetFeaturesOf(*rec);
			if (features.empty() || !ctx.errors) continue;
			std::string error;
			if (!model.Target().With(features, error)) ctx.errors->AddError(TargetAnnotationAt(*rec), Core::String(("[target]: " + error).c_str()));
			else if (ReturnsTargetDependentVector(*rec))
				ctx.errors->AddError(TargetAnnotationAt(*rec), "[target]: the function returns a vector wider than 16 bytes, which comes back in "
					"a register only some targets have: return it through a reference parameter");
		}
	}

	// ===========================================================================
	// PASS 1 - REGISTER (doc 02). One walk per unit. Shells only: types exist (incomplete),
	// names exist in the scope tree, template definitions are recorded. NOTHING is resolved,
	// so declaration order never matters. Each unit reads its ORIGINAL children (pre-merge) so a
	// shared header registers exactly once, under the unit that defines it.
	// ===========================================================================
	void ModelBuilder::Build(CompilerInstance& compiler, ProgramModel& model)
	{
		Context ctx;
		ctx.model = &model;
		ctx.errors = m_Errors;
		ctx.scope = model.Symbols()->GetRoot();
		m_Sources = &compiler.GetSourceManager();

		model.m_PreludeUnit = compiler.GetSourceManager().PreludeId();
		model.SetProjects(&compiler.GetSourceManager().Projects(), ProjectOfEachUnit(compiler.GetSourceManager()));
		for (uint32 s = 1; s <= compiler.SourceCount(); ++s) // source ids are 1-based
		{
			auto unit = compiler.GetCompilationUnit(s);
			if (!unit) continue;
			model.m_SourceNames[unit->SourceId()] = compiler.GetSourceManager().GetInfo(unit->SourceId()).displayName;
			const SourceInfo& source = compiler.GetSourceManager().GetInfo(unit->SourceId());
			model.m_SourcePaths[unit->SourceId()] = source.absPath.empty() ? source.displayName : source.absPath; // an in-memory source has only a name
			if (unit->SourceId() == model.m_PreludeUnit) continue;
			ctx.unitId = unit->SourceId();
			const auto& children = unit->GetOriginalChildren();
			for (const auto& child : children)
				VisitDeclaration(ctx, child.Get());
		}
		VisitPrelude(ctx, compiler);

		// Visibility (doc 09): record each unit's include closure so cross-file lookups check
		// "is the defining file in my include set?". A unit sees itself plus everything it
		// transitively includes. Without this every cross-file symbol is invisible.
		for (uint32 s = 1; s <= compiler.SourceCount(); ++s) // source ids are 1-based
		{
			auto unit = compiler.GetCompilationUnit(s);
			if (!unit) continue;
			Core::Vector<uint32> closure;
			closure.Push(unit->SourceId());
			for (uint32 inc : unit->GetIncludes())
				if (inc != unit->SourceId()) closure.Push(inc);
			if (model.m_PreludeUnit != 0 && model.m_PreludeUnit != unit->SourceId()) closure.Push(model.m_PreludeUnit);
			model.Symbols()->SetIncludeClosure(unit->SourceId(), closure);
	}


		MemberTemplates::AttachDefinitions(model);

		// Queries are answerable from here on; pass 2 resolves the shells into a complete model.
		model.BeginResolution();
		// PASS 2 - RESOLVE (doc 02): fixed point. Every shell exists, so a name always resolves
		// to something registered in pass 1 regardless of where it was written.
		for (uint32 round = 0; round < 64; ++round)
		{
			if (!ModelResolver().ResolveRound(*this, ctx)) break;
		}

		// GAP A: pass 2 never opens a body, so a `Vec<int32> v;` inside one is unresolved and its
		// specialization has no members/methods. Instantiate every body-referenced specialization as a
		// fixed point (instantiating S<int32> creates method records whose bodies may name more).
		uint32 prevRecs = 0;
		for (int round = 0; round < 8; ++round)
		{
			ModelResolver().InstantiateBodyReferences(*this, ctx);
			Core::Vector<const FunctionRecord*> now; ctx.model->AllFunctionRecords(now);
			if (now.Count() == prevRecs) break; // converged: this pass created no new methods
			prevRecs = now.Count();
		}

		model.m_Types->FinalizeLayouts(); // size/alignment fixed point for mutually-recursive types
		CheckObjectTypes(ctx, model);

		// Anything still pending is a genuine error (unknown type, cycle, ...). Report it.
		for (const auto& v : m_PendingVars)
			ReportUnresolved(ctx, v.var, "unresolved type for variable '" + v.var->GetNameToken().GetText() + "'", v.var->GetType().Get(), v.unitId,
				v.info->declaringScope);
		for (const auto& f : m_PendingFields)
			ReportUnresolved(ctx, f.var, "unresolved type for field '" + f.var->GetNameToken().GetText() + "' in '" + f.obj->GetName() + "'",
				f.var->GetType().Get(), f.unitId, model.TypeScope(f.obj));
		for (const auto& b : m_PendingBases) ErrorAt(ctx, b.type, "unresolved base class for '" + b.obj->GetName() + "'" + Why(ctx, b.type, b.unitId, b.declScope));
		for (const auto& e : m_PendingEnums)
			ErrorAt(ctx, e.en, "unresolved enum '" + e.en->GetNameToken().GetText() + "'" + (e.why.empty() ? Core::String() : ": " + e.why));
		for (const auto& a : m_PendingAliases)
			ErrorAt(ctx, a.target, "unresolved alias target for '" + a.raw->GetName() + "'" + Why(ctx, a.target, a.unitId, a.scope));
		for (const auto& u : m_PendingUnions) ReportUnresolvedUnion(ctx, u);
		for (const auto& f : m_PendingFuncs)
			ErrorAt(ctx, f.fn, "unresolved signature for function '" + f.name + "'" + SignatureWhy(ctx, *f.fn, f.unitId, f.scope));
		for (const auto& f : m_PendingQualifiedFuncs)
			ErrorAt(ctx, f.fn, "unresolved signature for function '" + f.prefix + "::" + f.last + "'" + QualifiedWhy(ctx, f));
		for (const auto& mismatch : TemplateMethodDefinitions::Unmatched(model)) ErrorAt(ctx, mismatch.definition, mismatch.why);
		for (const auto& d : m_PendingMethodDecls)
			ErrorAt(ctx, d.fn, "unresolved method '" + d.fn->GetName()->GetFirstIdentifierToken().GetText() + "' in '" + d.obj->GetName() + "'" +
				SignatureWhy(ctx, *d.fn, d.unitId, model.TypeScope(d.obj)));

		// A constant that never folded starts at runtime like any global (04), unless it depends on itself.
		ctx.model->Symbols()->ForEachScopedSymbol([&](const Core::String&, SymbolInfo* info, const Scope&)
		{
			if (info->kind != SymbolKind::Variable || !info->isConst || !info->initializer || !info->constValue.IsNone()) return;
			if (auto r = ConstEvaluator(*ctx.model).EvaluateConstant(*info); r.error == QueryError::CyclicDependency)
				ErrorAt(ctx, info->initializer, r.message);
		});
	}

	void ModelBuilder::VisitUnit(Context& ctx, const AstNode* root)
	{
		if (root && root->Type() == NodeType::Namespace)
			RegisterNamespace(ctx, *static_cast<const NamespaceNode*>(root));
	}

	void ModelBuilder::VisitDeclaration(Context& ctx, const AstNode* node)
	{
		if (!node) return;
		switch (node->Type())
		{
		case NodeType::Namespace:        RegisterNamespace(ctx, *static_cast<const NamespaceNode*>(node)); break;
		case NodeType::ObjectDefinition: RegisterObject(ctx, *static_cast<const ObjectDefinitionNode*>(node)); break;
		case NodeType::EnumDefinition:   RegisterEnum(ctx, *static_cast<const EnumDefinitionNode*>(node)); break;
		case NodeType::AliasDefinition:  RegisterAlias(ctx, *static_cast<const AliasDefinitionNode*>(node)); break;
		case NodeType::UnionDefinition:  RegisterUnion(ctx, *static_cast<const UnionDefinitionNode*>(node)); break;
		case NodeType::VariableDefinition: RegisterVariable(ctx, *static_cast<const VariableDefinitionNode*>(node)); break;
		case NodeType::FunctionDefinition: RegisterFunction(ctx, *static_cast<const FunctionDefinitionNode*>(node)); break;
		case NodeType::AsmStatement:     RegisterFileAssembly(ctx, *static_cast<const AsmStatementNode*>(node)); break;
		default: break; // includes, externs, etc. carry no model facts of their own
		}
	}

	void ModelBuilder::RegisterFileAssembly(Context& ctx, const AsmStatementNode& block)
	{
		if (!block.GetOperands().Empty() || !block.GetClobbers().Empty() || block.ClobbersCallerSaved())
			return ErrorAt(ctx, &block, "a file-scope asm block adds its lines to the program as they are: it takes no operands or clobbers "
				"(write them in an asm statement inside a function)");
		for (const Token& option : block.GetOptions())
			if (option.GetText() != "att_syntax")
				return ErrorAt(ctx, &block, "a file-scope asm block takes only the option 'att_syntax', not '" + option.GetText() + "'");
		ctx.model->m_FileAssembly.Push(&block);
	}

	namespace
	{
		// The file-scope name a prelude declaration introduces: a function's or a type's.
		Core::String PreludeName(const AstNode* node)
		{
			if (const auto* fn = dynamic_cast<const FunctionDefinitionNode*>(node))
				return fn->GetName()->SegmentCount() == 1 ? fn->GetName()->GetSegments()[0].nameToken.GetText() : Core::String();
			if (const auto* obj = dynamic_cast<const ObjectDefinitionNode*>(node)) return obj->GetNameToken().GetText();
			return "";
		}
	}

	std::unordered_map<uint32, uint32> ModelBuilder::ProjectOfEachUnit(const SourceManager& sources)
	{
		std::unordered_map<uint32, uint32> projectOf;
		for (uint32 id = 1; id <= sources.Count(); ++id)
			if (sources.HasInfo(id) && !sources.GetInfo(id).library) projectOf[id] = sources.GetInfo(id).project;
		return projectOf;
	}

	void ModelBuilder::VisitPrelude(Context& ctx, CompilerInstance& compiler)
	{
		// After the program: a name the program declares at file scope is the program's, the prelude's is hidden. A
		// hidden function goes to a scope no name reaches, for the files the program's declaration is not visible in.
		auto prelude = ctx.model->m_PreludeUnit ? compiler.GetCompilationUnit(ctx.model->m_PreludeUnit) : nullptr;
		if (!prelude) return;
		ctx.unitId = ctx.model->m_PreludeUnit;
		Scope* root = ctx.model->Symbols()->GetRoot();
		for (const auto& child : prelude->GetOriginalChildren())
		{
			const Core::String name = PreludeName(child.Get());
			if (name.empty() || root->Entries(name).Empty()) VisitDeclaration(ctx, child.Get());
			else if (dynamic_cast<const FunctionDefinitionNode*>(child.Get())) VisitHiddenPrelude(ctx, child.Get());
		}
	}

	void ModelBuilder::VisitHiddenPrelude(Context& ctx, const AstNode* function)
	{
		if (!ctx.model->m_HiddenPrelude)
			ctx.model->m_HiddenPrelude = ctx.model->Symbols()->CreateScope(Scope::Kind::Namespace, "__prelude", ctx.model->Symbols()->GetRoot());
		Context hidden = ctx;
		hidden.scope = ctx.model->m_HiddenPrelude;
		VisitDeclaration(hidden, function);
	}
} // namespace Scripting
