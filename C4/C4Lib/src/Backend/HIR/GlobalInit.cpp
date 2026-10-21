#include "Backend/HIR/GlobalInit.h"
#include "Backend/HIR/ConstantImage.h"
#include "Backend/HIR/DeclLowerer.h"
#include "Backend/HIR/LifecycleFacts.h"
#include "Backend/HIR/LoweringContext.h"
#include "Backend/HIR/TemplateBindings.h"
#include "Model/ProgramModel.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include <algorithm>
#include <unordered_map>

namespace Scripting
{
	namespace
	{
		struct GlobalVariable
		{
			const SymbolInfo* sym = nullptr;
			const Declarator* declarator = nullptr;
			const Scope* scope = nullptr;
			const ObjectType* owner = nullptr; // the type whose static data member it is
		};

		const Declarator* DeclaratorOf(const SymbolInfo& sym)
		{
			const auto* var = dynamic_cast<const VariableDefinitionNode*>(sym.declNode);
			if (!var) return nullptr;
			for (const Declarator& d : var->GetDeclarators())
				if (d.m_NameToken.GetText() == sym.plainName) return &d;
			return nullptr;
		}

		// A folded constant is already the storage's initial value; foreign storage starts in its own program.
		bool StartsAtRuntime(const ModelQueries& q, const SymbolInfo& sym, const Declarator& d)
		{
			if (!sym.type || !sym.linkSymbol.empty() || !sym.constValue.IsNone()) return false;
			return d.m_Initializer || !d.m_CtorArgs.Empty() || LifecycleFacts::NeedsConstruct(q, sym.type) || LifecycleFacts::NeedsDestroy(q, sym.type);
		}

		std::unordered_map<const Scope*, const ObjectType*> OwnersByScope(const ProgramModel& model)
		{
			std::unordered_map<const Scope*, const ObjectType*> owners;
			for (const ObjectType* obj : model.ObjectTypes())
				if (const Scope* scope = model.TypeScope(obj)) owners[scope] = obj;
			return owners;
		}

		bool DeclaredBefore(const GlobalVariable& a, const GlobalVariable& b)
		{
			if (a.sym->definingUnit != b.sym->definingUnit) return a.sym->definingUnit < b.sym->definingUnit;
			return a.declarator->m_NameToken.GetByteOffset() < b.declarator->m_NameToken.GetByteOffset();
		}

		// The globals `wanted` picks, each with its declarator, scope and owning type.
		template <typename Wanted>
		std::vector<GlobalVariable> GlobalsWhere(ProgramModel& model, Wanted wanted)
		{
			const auto owners = OwnersByScope(model);
			std::vector<GlobalVariable> vars;
			model.Symbols()->ForEachGlobalVariable([&](const SymbolInfo& sym, const Scope& scope) {
				const Declarator* d = DeclaratorOf(sym);
				if (!d || !wanted(sym, *d)) return;
				auto owner = owners.find(&scope);
				vars.push_back({ &sym, d, &scope, owner != owners.end() ? owner->second : nullptr });
			});
			return vars;
		}

		// The globals of one storage duration (every thread's own, or the process's) left to start.
		std::vector<GlobalVariable> StartedAtRuntime(ProgramModel& model, const StartedGlobals& started, bool threadLocal)
		{
			ModelQueries queries(&model);
			std::vector<GlobalVariable> vars = GlobalsWhere(model, [&](const SymbolInfo& sym, const Declarator& d) {
				return sym.isThreadLocal == threadLocal && !started.count(&sym) && StartsAtRuntime(queries, sym, d);
			});
			std::stable_sort(vars.begin(), vars.end(), DeclaredBefore);
			return vars;
		}

		bool StartsAsConstant(const SymbolInfo& sym, const Declarator& d)
		{
			return d.m_Initializer && sym.type && sym.linkSymbol.empty() && !sym.constValue.IsNone();
		}

		// When a variable starts: with the program (or its thread), or on its first use, where a start that throws
		// leaves the storage zero for the next use to try again.
		enum class StartTime { WithTheProgram, OnFirstUse };

		// Each variable lowers in its own context: its unit is its visibility domain and its declaring scope
		// (a namespace or a class) is where its initializer's names are found.
		void StartVariable(ProgramModel& model, ErrorTracker& errors, HIRFunction& out, const GlobalVariable& var, BlockNode* into, StartTime time)
		{
			LoweringContext ctx(&model, &errors);
			ctx.out = &out;
			ctx.unitId = var.sym->definingUnit;
			ctx.scope = var.scope;
			ctx.startedGlobal = var.sym;
			ScopeGuard frame = ctx.queries().PushScope(Scope::Kind::Function, "__c4_global_init");
			if (var.owner) BindTemplateParameters(ctx, model.TypeInstantiationBindings(var.owner));
			LValue lv; lv.kind = LValue::Kind::Global; lv.type = TypeHandle::UnwrapToValue(var.sym->type); lv.sym = var.sym;
			VariableStart start;
			start.initializer = var.declarator->m_Initializer.Get();
			start.arguments = &var.declarator->m_CtorArgs;
			start.site = var.sym->declNode;
			start.name = var.sym->plainName;
			DeclLowerer lowerer(ctx);
			const LValue* storage = out.NewLValue(lv);
			auto started = time == StartTime::OnFirstUse ? lowerer.StartStatic(start, storage, into) : lowerer.StartGlobal(start, storage, into);
			if (!started.Ok()) ctx.Fail(var.sym->declNode, started);
		}
	}

	bool StartsWithCode(ProgramModel& model, const SymbolInfo& sym)
	{
		const Declarator* d = DeclaratorOf(sym);
		return d && StartsAtRuntime(ModelQueries(&model), sym, *d);
	}

	void CheckConstantStarts(ProgramModel& model, ErrorTracker& errors)
	{
		HIRFunction discarded;
		discarded.root = discarded.NewNode<BlockNode>(nullptr);
		for (const GlobalVariable& var : GlobalsWhere(model, StartsAsConstant))
			StartVariable(model, errors, discarded, var, discarded.root, StartTime::WithTheProgram);
	}

	namespace
	{
		// Each start is lowered on its own: one that only stores constant numbers becomes data instead of code.
		bool LowerStarts(ProgramModel& model, ErrorTracker& errors, HIRFunction& out, StartedGlobals& started, bool threadLocal,
			std::vector<ConstantStart>& constants)
		{
			out.root = out.NewNode<BlockNode>(nullptr);
			const std::vector<GlobalVariable> vars = StartedAtRuntime(model, started, threadLocal);
			for (const GlobalVariable& var : vars)
			{
				started.insert(var.sym);
				BlockNode* start = out.NewNode<BlockNode>(nullptr);
				const size_t failures = errors.GetErrors().Count();
				StartVariable(model, errors, out, var, start, StartTime::WithTheProgram);
				const uint64 size = TypeHandle::UnwrapToValue(var.sym->type)->GetSize();
				std::optional<ConstantData> image = errors.GetErrors().Count() == failures ? ConstantImage(*start, *var.sym, size) : std::nullopt;
				if (image) constants.push_back({ var.sym, std::move(*image), start });
				else out.root->stmts.push_back(start);
			}
			return !vars.empty();
		}
	}

	bool LowerGlobalInit(ProgramModel& model, ErrorTracker& errors, HIRFunction& out, StartedGlobals& started,
		std::vector<ConstantStart>& constants)
	{
		return LowerStarts(model, errors, out, started, false, constants);
	}

	bool LowerThreadInit(ProgramModel& model, ErrorTracker& errors, HIRFunction& out, StartedGlobals& started,
		std::vector<ConstantStart>& constants)
	{
		return LowerStarts(model, errors, out, started, true, constants);
	}

	std::vector<FirstUseStart> LowerFirstUseStarts(ProgramModel& model, ErrorTracker& errors, StartedGlobals& started)
	{
		std::vector<FirstUseStart> starts;
		for (const GlobalVariable& var : StartedAtRuntime(model, started, true))
		{
			started.insert(var.sym);
			FirstUseStart& start = starts.emplace_back();
			start.variable = var.sym;
			start.function = std::make_unique<HIRFunction>();
			HIRFunction& out = *start.function;
			out.root = out.NewNode<BlockNode>(nullptr);
			BlockNode* once = out.NewNode<BlockNode>(nullptr);
			StartVariable(model, errors, out, var, once, StartTime::OnFirstUse);
			out.root->stmts.push_back(out.NewNode<OnceNode>(once, var.sym->plainName, true));
		}
		return starts;
	}
} // namespace Scripting
