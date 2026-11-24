#include "Serve/BodySwap.h"
#include "Compiler/Projects.h"
#include "Model/Builder/ModelResolver.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Expressions/LambdaNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Serve/BodyEdits.h"
#include "Serve/ValueEdits.h"
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace C4Compiler
{
	namespace
	{
		bool SameProject(const Project& a, const Project& b)
		{
			return a.name == b.name && a.sources == b.sources && a.includes == b.includes && a.defines == b.defines && a.depends == b.depends &&
				a.reflect == b.reflect;
		}

		bool SameProjects(const ProjectSet& a, const ProjectSet& b)
		{
			if (a.Count() != b.Count() || a.Root() != b.Root()) return false;
			for (uint32 i = 0; i < a.Count(); ++i)
				if (!SameProject(a.At(i), b.At(i))) return false;
			return true;
		}

		bool SameRegistration(const SourceInfo& a, const SourceInfo& b)
		{
			return a.displayName == b.displayName && a.baseDir == b.baseDir && a.absPath == b.absPath && a.project == b.project &&
				a.library == b.library && a.byName == b.byName && a.binding == b.binding && a.bindings == b.bindings;
		}

		// The files `edited` parsed again (a file whose parse it took from `live` has `live`'s tree); why not when they are
		// not the same files of the same projects.
		std::string ChangedFiles(const CompilerInstance& live, const CompilerInstance& edited, std::vector<uint32>& changed)
		{
			const SourceManager& was = live.GetSourceManager();
			const SourceManager& is = edited.GetSourceManager();
			if (!SameProjects(was.Projects(), is.Projects())) return "the projects changed";
			if (was.Count() != is.Count()) return "files were added or removed";
			for (uint32 id = 1; id <= was.Count(); ++id)
			{
				if (was.HasInfo(id) != is.HasInfo(id)) return "files were added or removed";
				if (!was.HasInfo(id)) continue;
				if (!SameRegistration(was.GetInfo(id), is.GetInfo(id))) return was.GetInfo(id).displayName + " is another file now";
				const Ref<CompilationUnit> before = live.FindCompilationUnit(id);
				const Ref<CompilationUnit> after = edited.FindCompilationUnit(id);
				if (!before != !after) return was.GetInfo(id).displayName + " is compiled now, or no longer";
				if (before && before->GetRootAST() != after->GetRootAST()) changed.push_back(id);
			}
			return {};
		}

		// The model's functions declared or defined by each of `definitions` (a definition's `before` node).
		std::unordered_map<const AstNode*, std::vector<const FunctionRecord*>> FunctionsOf(const ProgramModel& model,
			const std::vector<const KeptDefinition*>& definitions)
		{
			std::unordered_map<const AstNode*, std::vector<const FunctionRecord*>> functions;
			for (const KeptDefinition* kept : definitions) functions.emplace(kept->before, std::vector<const FunctionRecord*>{});
			Core::Vector<const FunctionRecord*> records;
			model.AllFunctionRecords(records);
			for (const FunctionRecord* record : records)
				for (const AstNode* declaration : record->declarations)
					if (const auto found = functions.find(declaration); found != functions.end()) found->second.push_back(record);
			return functions;
		}

		uint64 RequestsAsked(const ProgramModel& model)
		{
			uint64 count = 0;
			for (const auto& [what, asked] : model.Making().Requests()) count += asked;
			return count;
		}

		// The model's pass that opens bodies (ModelResolver::InstantiateBodyReferences) makes the template instances a body
		// names in type positions, in its order: the new body must name what the old named, so the pass makes the same.
		std::string SameInstancesNamed(ProgramModel& model, const FunctionRecord& record, const KeptDefinition& kept)
		{
			const uint64 requested = RequestsAsked(model);
			const bool same = ModelResolver::TypeInstancesNamedIn(model, kept.before->GetBody().Get(), record.LookupUnit()) ==
				ModelResolver::TypeInstancesNamedIn(model, kept.after->GetBody().Get(), record.LookupUnit());
			if (RequestsAsked(model) != requested) return "an edited body names a template instance the model has not made";
			if (!same) return "an edited body names other template instances than before";
			return {};
		}

		// The bodies of the lambdas `node` writes, in its order.
		void LambdaBodies(const AstNode& node, std::vector<const AstNode*>& bodies)
		{
			if (node.Type() == NodeType::LambdaExpression) bodies.push_back(static_cast<const LambdaNode&>(node).GetBody().Get());
			for (const Ref<AstNode>& child : node.Children())
				if (child) LambdaBodies(*child, bodies);
		}

		// A moved body's lambdas, old body -> new: the same tokens, so the same lambdas in the same order.
		void MovedLambdas(const KeptDefinition& kept, std::unordered_map<const AstNode*, const AstNode*>& moved)
		{
			std::vector<const AstNode*> was, is;
			LambdaBodies(*kept.before->GetBody(), was);
			LambdaBodies(*kept.after->GetBody(), is);
			for (size_t i = 0; i < was.size() && i < is.size(); ++i) moved.emplace(was[i], is[i]);
		}

		// The test registry keeps each test's line, which the harness reports: a line moved past a test's is a cold build.
		std::string TestsKeepTheirLines(const ProgramModel& model, uint32 file, const SourceLocation& movedFrom)
		{
			if (movedFrom.line == 0) return {};
			for (const TestRegistry::Test& test : model.Tests().Tests())
				if (test.sourceId == file && test.line >= movedFrom.line) return "a moved line moves a [test]'s";
			return {};
		}

		// The symbols of the globals each of `globals` defines (a definition's `before` node), static members too.
		std::unordered_map<const AstNode*, std::vector<SymbolInfo*>> SymbolsOf(const ProgramModel& model, const std::vector<const KeptGlobal*>& globals)
		{
			std::unordered_map<const AstNode*, std::vector<SymbolInfo*>> symbols;
			for (const KeptGlobal* kept : globals) symbols.emplace(kept->before, std::vector<SymbolInfo*>{});
			model.Symbols()->ForEachScopedSymbol([&](const Core::String&, SymbolInfo* info, const Scope&) {
				if (info->kind != SymbolKind::Variable) return;
				if (const auto found = symbols.find(info->declNode); found != symbols.end()) found->second.push_back(info);
			});
			return symbols;
		}

		// A global whose new value an edit serves: one its initializer folded to a value, which its storage starts with and
		// lowering reads where it is named.
		std::string ServedValue(const SymbolInfo& global)
		{
			const std::string name = (global.plainName.empty() ? global.mangledName : global.plainName).c_str();
			if (!global.linkSymbol.empty() || !global.initializer) return "'" + name + "' has no value of its own";
			if (global.constValue.IsNone()) return "'" + name + "' starts at run time";
			return {};
		}

		// `global` reads `kept`'s new definition where it read the old: its declaration and its initializer.
		void PointAtNew(SymbolInfo& global, const KeptGlobal& kept)
		{
			const auto& was = kept.before->GetDeclarators();
			const auto& is = kept.after->GetDeclarators();
			global.declNode = kept.after;
			for (uint32 i = 0; i < was.Count() && i < is.Count(); ++i)
				if (global.initializer && global.initializer == was[i].m_Initializer.Get()) global.initializer = is[i].m_Initializer.Get();
		}

		// The template instances the model's pass that opens bodies makes for each of `readers`' bodies, in its order.
		std::vector<std::vector<const Type*>> InstancesNamed(ProgramModel& model, const std::vector<const FunctionRecord*>& readers)
		{
			std::set<std::pair<const AstNode*, uint32>> opened;
			std::vector<std::vector<const Type*>> named;
			for (const FunctionRecord* reader : readers)
				if (reader->bodyNode && reader->bodyNode->Type() == NodeType::CodeBlock && opened.emplace(reader->bodyNode, reader->LookupUnit()).second)
					named.push_back(ModelResolver::TypeInstancesNamedIn(model, reader->bodyNode, reader->LookupUnit()));
			return named;
		}

		// docs/compiler/07 stage 2b: the edited globals and those folded from them folded again, their storage's partitions
		// emitted again, and the lowered bodies that read one of them joining `relowered` - when a cold build would make the
		// same of them (ReachOfValues, FoldAgain) and the bodies name the template instances they named.
		std::string ServeValues(const CompilerInstance& live, ProgramModel& model, ProgramBackend& backend, const std::vector<SymbolInfo*>& edited,
			const std::vector<const FunctionRecord*>& lowered, std::vector<const FunctionRecord*>& relowered)
		{
			ValueReach reach;
			if (std::string why = ReachOfValues(live, model, edited, lowered, reach); !why.empty()) return why;
			const uint64 requested = RequestsAsked(model);
			const auto named = InstancesNamed(model, reach.readers);
			if (std::string why = FoldAgain(model, reach.globals); !why.empty()) return why;
			if (InstancesNamed(model, reach.readers) != named || RequestsAsked(model) != requested) return "a new value makes a body name other template instances";
			backend.StorageChanged(reach.globals);
			relowered.insert(relowered.end(), reach.readers.begin(), reach.readers.end());
			return {};
		}

		std::vector<const FunctionRecord*> Unique(const std::vector<const FunctionRecord*>& records)
		{
			std::unordered_set<const FunctionRecord*> seen;
			std::vector<const FunctionRecord*> unique;
			for (const FunctionRecord* record : records)
				if (seen.insert(record).second) unique.push_back(record);
			return unique;
		}

		// `record` reads `kept`'s new definition where it read the old: its declarations, its body, its parameters' defaults.
		void PointAtNew(const FunctionRecord& record, const KeptDefinition& kept)
		{
			for (const AstNode*& declaration : record.declarations)
				if (declaration == kept.before) declaration = kept.after;
			if (record.bodyNode == kept.before->GetBody().Get()) record.bodyNode = kept.after->GetBody().Get();
			const auto& was = kept.before->GetParameters();
			const auto& is = kept.after->GetParameters();
			for (const AstNode*& value : record.paramDefaults)
				for (uint32 i = 0; i < was.Count() && i < is.Count(); ++i)
					if (value && value == was[i]->GetDefaultValue().Get()) value = is[i]->GetDefaultValue().Get();
		}
	}

	std::string SwapEdits(CompilerInstance& live, const CompilerInstance& edited, ProgramBackend& backend, RetiredParses& retired)
	{
		std::vector<uint32> changed;
		if (std::string why = ChangedFiles(live, edited, changed); !why.empty()) return why;
		std::vector<FileEdit> edits;
		std::vector<const KeptDefinition*> definitions;
		std::vector<const KeptGlobal*> globals;
		for (uint32 id : changed)
		{
			edits.push_back(FindFileEdit(*live.FindCompilationUnit(id), *edited.FindCompilationUnit(id)));
			const std::string why = !edits.back().refusal.empty() ? edits.back().refusal
				: TestsKeepTheirLines(live.GetProgramModel(), id, edits.back().movedFrom);
			if (!why.empty()) return live.GetSourceManager().GetInfo(id).displayName + ": " + why;
		}
		for (const FileEdit& edit : edits)
		{
			for (const KeptDefinition& kept : edit.definitions) definitions.push_back(&kept);
			for (const KeptGlobal& kept : edit.globals) globals.push_back(&kept);
		}

		ProgramModel& model = live.GetProgramModelForLowering();
		const auto functions = FunctionsOf(model, definitions);
		for (const KeptDefinition* kept : definitions)
			if (kept->bodyChanged)
				for (const FunctionRecord* record : functions.at(kept->before))
					if (std::string why = SameInstancesNamed(model, *record, *kept); !why.empty()) return why;
		const auto symbols = SymbolsOf(model, globals);
		std::vector<SymbolInfo*> valued;
		for (const KeptGlobal* kept : globals)
		{
			if (!kept->valueChanged) continue;
			if (symbols.at(kept->before).empty()) return "a global whose value changed has no symbol in the model";
			for (SymbolInfo* global : symbols.at(kept->before))
			{
				if (std::string why = ServedValue(*global); !why.empty()) return why;
				valued.push_back(global);
			}
		}
		// A function the build before did not lower (a template's instance nothing reached) stays unlowered: what reaches
		// a function is the lowered bodies' needs, which Relower keeps.
		const std::vector<const FunctionRecord*> lowered = backend.LoweredFunctions();
		const std::unordered_set<const FunctionRecord*> wasLowered(lowered.begin(), lowered.end());
		std::vector<const FunctionRecord*> relowered;
		std::unordered_map<const AstNode*, const AstNode*> movedLambdas;
		for (const KeptDefinition* kept : definitions)
		{
			if (kept->moved) MovedLambdas(*kept, movedLambdas);
			for (const FunctionRecord* record : functions.at(kept->before))
			{
				const bool ownBody = record->bodyNode == kept->before->GetBody().Get();
				PointAtNew(*record, *kept);
				if (ownBody && kept->bodyChanged && wasLowered.count(record)) relowered.push_back(record);
			}
		}
		for (const KeptGlobal* kept : globals)
			for (SymbolInfo* global : symbols.at(kept->before)) PointAtNew(*global, *kept);
		model.ClosureBodiesMoved(movedLambdas);
		for (uint32 id : changed)
		{
			retired.push_back(live.FindCompilationUnit(id)->ToPriorParse());
			live.TakeEditedFile(id, edited);
		}
		live.TakeMacroDiagnostics(edited);
		if (!valued.empty())
			if (std::string why = ServeValues(live, model, backend, valued, lowered, relowered); !why.empty()) return why;
		return backend.Relower(Unique(relowered));
	}
} // namespace C4Compiler
