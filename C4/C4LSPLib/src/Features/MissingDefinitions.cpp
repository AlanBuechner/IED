#include "Features/MissingDefinitions.h"
#include "Features/DeclarationText.h"
#include "Server/CompilationSession.h"
#include "Model/ProgramModel.h"
#include "Model/SymbolTable.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include <atomic>
#include <unordered_map>
#include <unordered_set>

namespace C4LSP::Features
{
	using Scripting::AstNode;
	using Scripting::FunctionDefinitionNode;
	using Scripting::FunctionRecord;
	using Scripting::NodeType;
	using Scripting::ObjectDefinitionNode;

	// On by default, as Visual Studio's; the client may turn it off (`missingDefinitions`), on the message thread while the
	// worker publishes (BUG-492): atomic.
	static std::atomic<bool> s_Enabled = true;

	void MissingDefinitions::SetEnabled(bool enabled) { s_Enabled.store(enabled, std::memory_order_relaxed); }
	bool MissingDefinitions::IsEnabled() { return s_Enabled.load(std::memory_order_relaxed); }

	namespace
	{
		// A declaration the warning may be about: its function needs a body somewhere.
		struct Declared
		{
			const FunctionDefinitionNode* declaration = nullptr;
			const ObjectDefinitionNode* owner = nullptr; // the type it is a method of; null for a free function
			bool templated = false;                       // a template's, or a method of one: no function until a use
		};

		bool NeedsABody(const FunctionDefinitionNode& function, const ObjectDefinitionNode* owner)
		{
			if (function.IsDefinition() || !function.GetName() || function.GetName()->SegmentCount() != 1)
				return false; // a body, or no plain name of its own to define
			if (function.IsPureVirtual())
				return false; // `abstract` or `= 0`
			return !owner || owner->Kind() != Scripting::ObjectKind::Interface;
		}

		// The declarations without a body a file writes: at its namespaces' scope and in its types' bodies, never in a
		// function's body (a function declared there is the body's own, not a declaration of a program's function).
		void CollectDeclared(const Server::CompilationSession& session, const AstNode& node, uint32 sourceId,
			const ObjectDefinitionNode* owner, bool templated, Core::Vector<Declared>& out)
		{
			for (const auto& child : node.Children())
			{
				if (!child || child->startLocation.sourceId != sourceId || session.InBindings(child->startLocation))
					continue;
				switch (child->Type())
				{
				case NodeType::Namespace:
					CollectDeclared(session, *child, sourceId, nullptr, false, out);
					break;
				case NodeType::ObjectDefinition: {
					const auto& object = static_cast<const ObjectDefinitionNode&>(*child);
					CollectDeclared(session, object, sourceId, &object, templated || object.IsTemplated() || object.IsSpecialization(), out);
					break;
				}
				case NodeType::FunctionDefinition: {
					const auto& function = static_cast<const FunctionDefinitionNode&>(*child);
					if (NeedsABody(function, owner))
						out.Push({ &function, owner, templated || function.IsTemplated() });
					break;
				}
				default:
					break;
				}
			}
		}

		using RecordsOf = std::unordered_map<const AstNode*, Core::Vector<const FunctionRecord*>>;

		// The compiler's functions each declaration declares, in one pass over the program's functions.
		RecordsOf RecordsDeclaring(Server::CompilationSession& session, const Core::Vector<Declared>& declared)
		{
			std::unordered_set<const AstNode*> wanted;
			for (const Declared& d : declared)
				wanted.insert(d.declaration);
			Core::Vector<const FunctionRecord*> records;
			session.GetCompiler().GetProgramModel().AllFunctionRecords(records);
			RecordsOf out;
			for (const FunctionRecord* record : records)
				for (const AstNode* node : record->declarations)
					if (wanted.count(node))
						out[node].Push(record);
			return out;
		}

		bool HasCode(const Core::Vector<const FunctionRecord*>& records)
		{
			for (const FunctionRecord* record : records)
				if (Scripting::HasCode(*record) || record->isBuiltin)
					return true;
			return false;
		}

		bool HasPack(const FunctionDefinitionNode& function)
		{
			for (const auto& parameter : function.GetParameters())
				if (parameter && parameter->IsPack())
					return true;
			return false;
		}

		// Whether `definition` (a body written out of line) could be `declaration`'s: the same name, owned by a type of its
		// type's name (or by no type), and as many parameters unless a pack makes them any number.
		bool CouldDefine(const FunctionDefinitionNode& definition, const FunctionDefinitionNode& declaration, const ObjectDefinitionNode* owner)
		{
			const auto& segments = definition.GetName()->GetSegments();
			if (&definition == &declaration || segments.Count() == 0 ||
				segments[segments.Count() - 1].nameToken.GetText() != declaration.GetName()->GetLastIdentifierToken().GetText())
				return false;
			if (owner && (segments.Count() < 2 || segments[segments.Count() - 2].nameToken.GetText() != owner->GetNameToken().GetText()))
				return false;
			if (HasPack(declaration) || HasPack(definition))
				return true;
			return definition.GetParameters().Count() == declaration.GetParameters().Count();
		}

		// Out-of-line bodies at the namespaces' scope of `node`, each to `visit`.
		template <typename Visit>
		void ForEachOutOfLineBody(const AstNode& node, Visit&& visit)
		{
			for (const auto& child : node.Children())
			{
				if (!child)
					continue;
				if (child->Type() == NodeType::Namespace)
					ForEachOutOfLineBody(*child, visit);
				else if (child->Type() == NodeType::FunctionDefinition)
				{
					const auto& function = static_cast<const FunctionDefinitionNode&>(*child);
					if (function.IsDefinition() && function.GetName())
						visit(function);
				}
			}
		}

	} // namespace

	const FunctionDefinitionNode* MissingDefinitions::TemplateBodyFor(Server::CompilationSession& session, const FunctionDefinitionNode& declaration,
		const ObjectDefinitionNode* owner)
	{
		auto& compiler = session.GetCompiler();
		for (uint32 sourceId = 1; sourceId <= compiler.SourceCount(); ++sourceId)
		{
			if (!compiler.GetSourceManager().HasInfo(sourceId))
				continue;
			auto unit = compiler.FindCompilationUnit(sourceId);
			if (!unit || !unit->GetRootAST())
				continue;
			const FunctionDefinitionNode* found = nullptr;
			ForEachOutOfLineBody(*unit->GetRootAST(), [&](const FunctionDefinitionNode& definition) {
				if (!found && CouldDefine(definition, declaration, owner))
					found = &definition;
			});
			if (found)
				return found;
		}
		return nullptr;
	}

	Core::Vector<MissingDefinition> MissingDefinitions::InDocument(Server::CompilationSession& session, uint32 sourceId)
	{
		Core::Vector<MissingDefinition> out;
		auto unit = session.GetCompiler().FindCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return out;
		Core::Vector<Declared> declared;
		CollectDeclared(session, *unit->GetRootAST(), sourceId, nullptr, false, declared);
		if (declared.Count() == 0)
			return out;

		const RecordsOf records = RecordsDeclaring(session, declared);
		for (const Declared& d : declared)
		{
			auto found = records.find(d.declaration);
			if (found != records.end() && HasCode(found->second))
				continue;
			// A template's function is its instances': the record of a member template itself has no body, whatever is
			// written for it, so a template's body is looked for by name.
			if (d.templated)
			{
				if (!TemplateBodyFor(session, *d.declaration, d.owner))
					out.Push({ d.declaration, found != records.end() ? found->second[0] : nullptr });
			}
			else if (found != records.end())
				out.Push({ d.declaration, found->second[0] });
			// Anything else the model has no function for (a type it could not make) is not known to lack a body.
		}
		return out;
	}

	Protocol::Range MissingDefinitions::NameRange(const Server::CompilationSession& session, const FunctionDefinitionNode& declaration)
	{
		const Scripting::Token& name = declaration.GetName()->GetLastIdentifierToken();
		return session.ToEditorRange(name.GetStartLocation().sourceId, name.GetStartLocation(), name.GetEndLocation());
	}

	std::string MissingDefinitions::MessageFor(const FunctionDefinitionNode& declaration)
	{
		return "Function definition for '" + DeclarationText::NameOf(declaration) + "' not found";
	}

	Core::Vector<Protocol::Diagnostic> MissingDefinitions::ForDocument(Server::CompilationSession& session, const std::string& uri)
	{
		Core::Vector<Protocol::Diagnostic> out;
		if (!s_Enabled)
			return out;
		const uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return out;
		for (const MissingDefinition& missing : InDocument(session, sourceId))
		{
			Protocol::Diagnostic diag;
			diag.range = NameRange(session, *missing.declaration);
			diag.severity = Protocol::DiagnosticSeverity::Warning;
			diag.message = MessageFor(*missing.declaration);
			diag.source = "c4";
			out.Push(diag);
		}
		return out;
	}
} // namespace C4LSP::Features
