#include "Model/ProgramModel.h"
#include <functional>
#include "Model/Templates/FunctionTemplates.h"
#include "Model/OverloadResolution.h"
#include "Model/ConstEvaluator.h"
#include "Model/OperatorTable.h"
#include "Model/TypeHandle.h" // UnwrapToValue / GetPointedType bricks (moved off the Type base)
#include "Model/TemplateInstantiator.h" // template subsystem: these two verbs delegate to it
#include "Model/TypeResolver.h"        // type-position resolution: the type verbs delegate to it
#include "Model/LoweringScopes.h"      // doc 06 scopes: the lowering verbs delegate to it
#include "Model/SymbolQueries.h"       // name queries: the query verbs delegate to it
#include "Model/Types/QualifiedName.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Declarations/FunPtrTypeNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h" // complete type: bodies are stored as pointers

namespace Scripting
{
	ProgramModel::ProgramModel() : m_Types(std::make_unique<TypeStore>()), m_Symbols(std::make_unique<SymbolTable>()) {}
	ProgramModel::~ProgramModel() = default;

	ProgramModel::ProgramModel(ProgramModel&& other) noexcept = default;
	ProgramModel& ProgramModel::operator=(ProgramModel&& other) noexcept = default;

	const Core::Vector<const ObjectType*>& ProgramModel::ObjectTypes() const { return m_ObjectTypes; }
	const Core::Vector<const EnumType*>& ProgramModel::EnumTypes() const { return m_EnumTypes; }

	const FunctionRecord* ProgramModel::SynthesizedLifecycle(const ObjectType* type, LifecycleOp op) const
	{
		auto it = m_SynthesizedLifecycle.find({ type, op });
		return it == m_SynthesizedLifecycle.end() ? nullptr : it->second;
	}

	const FunctionRecord* ProgramModel::EnsureSynthesizedLifecycle(const ObjectType* type, LifecycleOp op) const
	{
		if (const FunctionRecord* existing = SynthesizedLifecycle(type, op)) return existing;
		static const char* const kNames[] = { "", "create", "delete", "copy" };
		Core::Vector<const Type*> params;
		FunctionRecord rec;
		if (op == LifecycleOp::Copy)
		{
			params.Push(m_Types->Reference(type, true));
			rec.paramNames.Push("other");
		}
		rec.type = m_Types->Function(m_Types->Builtin(TypeKind::Void), params);
		auto scopeIt = m_TypeScopes.find(type);
		rec.scope = scopeIt != m_TypeScopes.end() ? scopeIt->second : nullptr;
		rec.containingType = type;
		rec.synthesized = op;
		rec.minArity = params.Count();
		rec.name = Core::String("__c4_") + kNames[static_cast<int>(op)];
		// Named for the operation and the type's scope (an instance's scope names its arguments), so every
		// program that writes this function names it alike.
		rec.mangledName = "__c4_" + Core::String(kNames[static_cast<int>(op)]) + "_" + TypeSymbol(type);
		const FunctionRecord* stored = m_Symbols->AddFunctionRecord(rec);
		m_SynthesizedLifecycle[{ type, op }] = stored;
		return stored;
	}

	Core::String ProgramModel::TypeSymbol(const ObjectType* type) const
	{
		const Scope* scope = TypeScope(type);
		return scope ? CanonicalSymbolName(scope->GetParent(), scope->GetName()) : EscapedText(QualifiedName(type));
	}

	void ProgramModel::AllFunctionRecords(Core::Vector<const FunctionRecord*>& out) const
	{
		for (const auto& r : m_Symbols->Records()) out.Push(r.get());
	}

	const InstantiationBindings* ProgramModel::TypeInstantiationBindings(const Type* instantiated) const
	{
		for (uint32 i = 0; i < m_InstantiationBindings.Count(); ++i)
			if (m_InstantiationBindings[i].first == instantiated) return &m_InstantiationBindings[i].second;
		return nullptr;
	}

	const InstantiationBindings* ProgramModel::FunctionInstantiationBindings(const FunctionRecord* rec) const
	{
		for (uint32 i = 0; i < m_FuncInstantiationBindings.Count(); ++i)
			if (m_FuncInstantiationBindings[i].first == rec) return &m_FuncInstantiationBindings[i].second;
		return nullptr;
	}

	const FunctionRecord* ProgramModel::RecordFor(const FunctionType* sig) const
	{
		auto it = m_RecordBySignature.find(sig);
		return it != m_RecordBySignature.end() ? it->second : nullptr;
	}

	const FunctionRecord* ProgramModel::RecordForMethod(const ObjectType* type, const ObjectType::MethodInfo* method) const
	{
		if (!type || !method) return nullptr;
		auto recsIt = m_MethodRecords.find(type);
		if (recsIt == m_MethodRecords.end()) return nullptr;
		const auto& recs = recsIt->second;
		// The method list and the record list grow together (one Push per AddMethod), so the
		// method's index in the list IS its index in the records: a method of the list is found by its
		// address; a copy of a MethodInfo by name and arity.
		const auto& methods = type->GetMethods();
		const std::less<const ObjectType::MethodInfo*> before;
		if (methods.Count() > 0 && !before(method, &methods[0]) && before(method, &methods[0] + methods.Count()))
		{
			const uint32 i = (uint32)(method - &methods[0]);
			return i < recs.Count() ? recs[i] : nullptr;
		}
		for (uint32 i = 0; i < methods.Count(); ++i)
			if (methods[i].mName == method->mName && methods[i].m_ParameterTypes.Count() == method->m_ParameterTypes.Count())
				return i < recs.Count() ? recs[i] : nullptr;
		return nullptr;
	}

	const LifecycleMethods& ProgramModel::LifecycleMethodsOf(const ObjectType* type) const
	{
		const auto records = m_MethodRecords.find(type);
		const uint32 recordCount = records == m_MethodRecords.end() ? 0 : records->second.Count();
		auto [entry, fresh] = m_LifecycleMethods.try_emplace(type);
		KnownLifecycle& known = entry->second;
		if (fresh || known.methods != type->GetMethodCount() || known.records != recordCount)
			known = { type->GetMethodCount(), recordCount, FindLifecycleMethods(*this, type) };
		return known.found;
	}

	const ObjectType::MethodInfo* ProgramModel::MethodForRecord(const FunctionRecord& record) const
	{
		if (!record.containingType || record.isExtensionMethod) return nullptr;
		auto recsIt = m_MethodRecords.find(record.containingType);
		if (recsIt == m_MethodRecords.end()) return nullptr;
		const auto& recs = recsIt->second;
		const auto& methods = record.containingType->GetMethods();
		for (uint32 i = 0; i < recs.Count() && i < methods.Count(); ++i)
			if (recs[i] == &record) return &methods[i];
		return nullptr;
	}

	// ---------------------------------------------------------------------------
	// Qualified name walking (visibility-aware): "Math::Vec" resolves segment by segment. Every
	// non-final segment must be a namespace OR a type with a class scope (nested types: "S::Inner").
	// Each segment is checked against the asking unit's include closure; a missing or invisible
	// segment yields nullptr. Every name query goes through this one walk.
	// ---------------------------------------------------------------------------
	// TYPE + NAME QUERIES: thin delegates to the TypeResolver subsystem (doc 04). Kept as members so
	// every existing call site (builder, lowering, internal queries) works unchanged.
	// ---------------------------------------------------------------------------
	Core::String ProgramModel::OutOfProjectReach(const SymbolInfo& symbol, const Core::String& name) const
	{
		if (!m_Projects || m_ReachUnit == 0) return "";
		auto asking = m_ProjectOfUnit.find(m_ReachUnit);
		auto declaring = m_ProjectOfUnit.find(symbol.definingUnit);
		if (asking == m_ProjectOfUnit.end() || declaring == m_ProjectOfUnit.end() || m_Projects->CanUse(asking->second, declaring->second)) return "";
		const std::string& user = m_Projects->At(asking->second).name;
		const std::string& owner = m_Projects->At(declaring->second).name;
		return "'" + name + "' is declared in project '" + Core::String(owner.c_str()) + "', and project '" + Core::String(user.c_str()) +
			"' does not depend on '" + Core::String(owner.c_str()) + "'";
	}

	const SymbolInfo* ProgramModel::FindVisibleQualified(const Core::String& name, uint32 unitId, const Scope* context) const
	{
		return TypeResolver().FindVisibleQualified(*this, name, unitId, context);
	}

	QueryResult<size_t> ProgramModel::SizeOf(const Type* t) const
	{
		if (!t) return QueryResult<size_t>::Fail(QueryError::NotFound, "no type");
		// Incomplete (forward-declared) types report Ok with 0: the two-pass model sizes them later.
		return QueryResult<size_t>::Success(t->GetSize());
	}

	QueryResult<size_t> ProgramModel::AlignOf(const Type* t) const
	{
		if (!t) return QueryResult<size_t>::Fail(QueryError::NotFound, "no type");
		return QueryResult<size_t>::Success(t->GetAlignment());
	}

	const Type* ProgramModel::ElementOf(const Type* t) const
	{
		if (!t) return nullptr;
		switch (t->GetKind()) {
		case TypeKind::Array: return static_cast<const ArrayType*>(t)->GetElementType();
		case TypeKind::Pointer: return static_cast<const PointerType*>(t)->GetPointeeType();
		default: return nullptr; // a scalar has no element concept
		}
	}

	QueryResult<const Type*> ProgramModel::QueryInstantiation(const QualTypeNode& node, uint32 unitId) const
	{
		const auto* base = node.BaseName().Get();
		if (!base || !node.HasTemplateArgs())
			return QueryResult<const Type*>::Fail(QueryError::NotFound, "not a template specialization");
		const Scope* from = m_Symbols->GetRoot();
		for (uint32 i = 0; i < base->SegmentCount(); ++i)
		{
			const auto& seg = base->GetSegments()[i];
			bool isFinal = (i == base->SegmentCount() - 1);
			if (!isFinal)
			{
				const SymbolInfo* info = m_Symbols->FindVisible(from, seg.nameToken.GetText(), unitId);
				if (!info || info->kind != SymbolKind::Namespace || !info->scope)
					return QueryResult<const Type*>::Fail(QueryError::NotFound, "not a namespace path: " + seg.nameToken.GetText());
				from = info->scope;
			}
		}
		const auto* lastSeg = &base->GetSegments()[base->SegmentCount() - 1];
		// Templates are registered under their LOCAL name (m_TypeTemplates[name]), not the full
		// qualified path, so a namespaced template (`Geometry::Cube<int32>`) must look up by its
		// last segment; `from` (the namespace scope) disambiguates. Passing fullPath made every
		// namespaced type-template instantiation fail with "not a template: Geometry::Cube".
		return InstantiateTypeTemplate(lastSeg->nameToken.GetText(), from, *lastSeg->templateArgs, unitId, nullptr, nullptr);
	}

	// --- Template verbs (doc 05): thin delegates to the TemplateInstantiator subsystem ---------
	QueryResult<const Type*> ProgramModel::InstantiateTypeTemplate(const Core::String& name, const Scope* declScope,
		const TemplateArgumentListNode& args, uint32 unitId, Scope* outerBinding, const Scope* argContext) const
	{
		return TemplateInstantiator().InstantiateTypeTemplate(*this, name, declScope, args, unitId, outerBinding, argContext);
	}

	QueryResult<const Type*> ProgramModel::ResolveType(const QualTypeNode& node, uint32 unitId) const
	{
		return TypeResolver().ResolveType(*this, node, unitId);
	}

	QueryResult<const Type*> ProgramModel::ResolveTypeInContext(const QualTypeNode& node, uint32 unitId, const Scope* context) const
	{
		return TypeResolver().ResolveTypeInContext(*this, node, unitId, context);
	}

	QueryResult<const Type*> ProgramModel::ResolveTypeInUnitContext(const QualTypeNode& node, uint32 unitId, const Scope* context) const
	{
		return TypeResolver().ResolveTypeInUnitContext(*this, node, unitId, context);
	}

	QueryResult<const Type*> ProgramModel::ResolveTypeInternal(const QualTypeNode& node, uint32 unitId, Scope* bindingScope, const Scope* context) const
	{
		return TypeResolver().ResolveTypeInternal(*this, node, unitId, bindingScope, context);
	}

	QueryResult<const Type*> ProgramModel::QueryType(const Core::String& name, uint32 unitId, const Scope* scopeContext) const
	{
		return TypeResolver().QueryType(*this, name, unitId, scopeContext);
	}

	// --- Lowering-time scopes (doc 06): thin delegates to the LoweringScopes subsystem ----------
	ScopeGuard ProgramModel::PushScope(Scope::Kind kind, const Core::String& name)
	{
		return LoweringScopes().PushScope(*this, kind, name);
	}

	bool ProgramModel::RegisterLocal(const Core::String& name, SymbolInfo info)
	{
		return LoweringScopes().RegisterLocal(*this, name, info);
	}

	const SymbolInfo* ProgramModel::FindLocal(const Core::String& name) const
	{
		return LoweringScopes().FindLocal(*this, name);
	}

	const Closure* ProgramModel::CreateClosure(const ClosureSpec& spec)
	{
		return ClosureTypes::Create(*this, spec);
	}

	const Closure* ProgramModel::ClosureOf(const Type* t) const
	{
		const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(t));
		auto it = obj ? m_Closures.find(obj) : m_Closures.end();
		return it == m_Closures.end() ? nullptr : it->second.get();
	}

	QueryResult<const Type*> ProgramModel::RegisterLocalType(const AstNode& decl)
	{
		return LoweringScopes().RegisterLocalType(*this, decl);
	}

	QueryResult<const Type*> ProgramModel::RegisterLocalAlias(const AliasDefinitionNode& alias)
	{
		return LoweringScopes().RegisterLocalAlias(*this, alias);
	}

	QueryResult<const FunctionRecord*> ProgramModel::RegisterLocalFunction(const FunctionDefinitionNode& fn)
	{
		return LoweringScopes().RegisterLocalFunction(*this, fn);
	}

	// --- Symbol queries (doc 04/08): thin delegates to the SymbolQueries subsystem -------------
	const TemplateNameType* ProgramModel::QueryTemplateName(const Core::String& name, uint32 unitId, const Scope* scope) const
	{
		TemplateCall call;
		call.name = name;
		call.unitId = unitId;
		call.scope = scope;
		return FunctionTemplates::NameOf(*this, call);
	}

	QueryResult<const FunctionType*> ProgramModel::QueryFunction(const Core::String& name, const Type* receiver,
		const TemplateArgumentListNode* tmplArgs, const Core::Vector<const Type*>& argTypes, const LookupContext& ctx,
		const FunctionRecord** outRecord) const
	{
		return SymbolQueries().QueryFunction(*this, name, receiver, tmplArgs, argTypes, ctx, outRecord);
	}

	QueryResult<OperatorBinding> ProgramModel::QueryOperator(const Core::String& opToken, const Type* lhs,
		const Type* rhs, const LookupContext& ctx) const
	{
		return SymbolQueries().QueryOperator(*this, opToken, lhs, rhs, ctx);
	}

	QueryResult<VariableInfo> ProgramModel::QueryVariable(const Core::String& name, const Type* receiver, const LookupContext& ctx) const
	{
		return SymbolQueries().QueryVariable(*this, name, receiver, ctx);
	}

	QueryResult<MemberBinding> ProgramModel::QueryMember(const Type* base, const Core::String& name) const
	{
		return SymbolQueries().QueryMember(*this, base, name);
	}

	QueryResult<const SymbolInfo*> ProgramModel::QuerySymbol(const Core::String& name, uint32 unitId, const Scope* context) const
	{
		return SymbolQueries().QuerySymbol(*this, name, unitId, context);
	}

	const std::vector<SymbolInfo>* ProgramModel::StorePack(std::vector<SymbolInfo> elements) const
	{
		m_PackStorage.push_back(std::move(elements));
		return &m_PackStorage.back();
	}

	QueryResult<ConstantValue> ProgramModel::QueryConstant(const Core::String& name, uint32 unitId, const Scope* context) const
	{
		return SymbolQueries().QueryConstant(*this, name, unitId, context);
	}
} // namespace Scripting
