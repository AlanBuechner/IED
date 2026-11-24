#include "Serve/BodySwap.h"
#include "Compiler/Projects.h"
#include "Model/Builder/ModelResolver.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Expressions/LambdaNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Serve/BodyEdits.h"
#include <unordered_map>

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

	std::string SwapBodies(CompilerInstance& live, const CompilerInstance& edited, ProgramBackend& backend, RetiredParses& retired)
	{
		std::vector<uint32> changed;
		if (std::string why = ChangedFiles(live, edited, changed); !why.empty()) return why;
		std::vector<BodyEdit> edits;
		std::vector<const KeptDefinition*> definitions;
		for (uint32 id : changed)
		{
			edits.push_back(FindBodyEdit(*live.FindCompilationUnit(id), *edited.FindCompilationUnit(id)));
			const std::string why = !edits.back().refusal.empty() ? edits.back().refusal
				: TestsKeepTheirLines(live.GetProgramModel(), id, edits.back().movedFrom);
			if (!why.empty()) return live.GetSourceManager().GetInfo(id).displayName + ": " + why;
		}
		for (const BodyEdit& edit : edits)
			for (const KeptDefinition& kept : edit.definitions) definitions.push_back(&kept);

		ProgramModel& model = live.GetProgramModelForLowering();
		const auto functions = FunctionsOf(model, definitions);
		for (const KeptDefinition* kept : definitions)
			if (kept->bodyChanged)
				for (const FunctionRecord* record : functions.at(kept->before))
					if (std::string why = SameInstancesNamed(model, *record, *kept); !why.empty()) return why;
		std::vector<const FunctionRecord*> relowered;
		std::unordered_map<const AstNode*, const AstNode*> movedLambdas;
		for (const KeptDefinition* kept : definitions)
		{
			if (kept->moved) MovedLambdas(*kept, movedLambdas);
			for (const FunctionRecord* record : functions.at(kept->before))
			{
				const bool ownBody = record->bodyNode == kept->before->GetBody().Get();
				PointAtNew(*record, *kept);
				if (ownBody && kept->bodyChanged) relowered.push_back(record);
			}
		}
		model.ClosureBodiesMoved(movedLambdas);
		for (uint32 id : changed)
		{
			retired.push_back(live.FindCompilationUnit(id)->ToPriorParse());
			live.TakeEditedFile(id, edited);
		}
		live.TakeMacroDiagnostics(edited);
		return backend.Relower(relowered);
	}
} // namespace C4Compiler
