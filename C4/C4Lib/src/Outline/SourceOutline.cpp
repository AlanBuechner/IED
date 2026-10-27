#include "Outline/SourceOutline.h"
#include "Compiler/CpuTarget.h"
#include "Compiler/ErrorTracker.h"
#include "Lexer/Lexer.h"
#include "Macro/MacroPass.h"
#include "Macro/PredefinedMacros.h"
#include "Parser/ParserState.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "SourceManager/Source.h"

namespace Scripting
{
	namespace
	{
		const char* kOutlinedFile = "outlined.cf";

		OutlinePosition PositionOf(const SourceLocation& location) { return { location.line, location.column }; }

		OutlineSeverity SeverityOf(ErrorSeverity severity)
		{
			return severity == ErrorSeverity::Warning ? OutlineSeverity::Warning : OutlineSeverity::Error;
		}

		OutlineKind ObjectKindOf(const ObjectDefinitionNode& object)
		{
			if (object.Kind() == ObjectKind::Struct) return OutlineKind::Struct;
			return object.Kind() == ObjectKind::Interface ? OutlineKind::Interface : OutlineKind::Class;
		}

		std::string WrittenFunctionName(const FunctionDefinitionNode& function)
		{
			std::string name;
			if (!function.GetName()) return name;
			for (const auto& segment : function.GetName()->GetSegments())
				name += (name.empty() ? "" : "::") + segment.nameToken.GetText();
			return name;
		}

		// A declaration with its kind, name and places; `name` is where its name starts.
		OutlineDeclaration Declared(OutlineKind kind, const std::string& name, const AstNode& node, const Token& nameToken, int32 parent)
		{
			OutlineDeclaration declaration;
			declaration.kind = kind;
			declaration.name = name;
			declaration.start = PositionOf(node.startLocation);
			declaration.end = PositionOf(node.endLocation);
			declaration.nameAt = PositionOf(nameToken.GetStartLocation());
			declaration.parent = parent;
			return declaration;
		}

		class OutlineWalk
		{
		public:
			std::vector<OutlineDeclaration> declarations;

			void Children(const AstNode& parent, int32 parentIndex)
			{
				for (const Ref<AstNode>& child : parent.Children())
					if (child) Add(*child, parentIndex);
			}

		private:
			int32 Push(const OutlineDeclaration& declaration)
			{
				declarations.push_back(declaration);
				return static_cast<int32>(declarations.size() - 1);
			}

			void Add(const AstNode& node, int32 parent)
			{
				switch (node.Type())
				{
				case NodeType::Namespace: AddNamespace(static_cast<const NamespaceNode&>(node), parent); break;
				case NodeType::ObjectDefinition: AddObject(static_cast<const ObjectDefinitionNode&>(node), parent); break;
				case NodeType::EnumDefinition: AddEnum(static_cast<const EnumDefinitionNode&>(node), parent); break;
				case NodeType::FunctionDefinition: AddFunction(static_cast<const FunctionDefinitionNode&>(node), parent); break;
				case NodeType::VariableDefinition: {
					const auto& variable = static_cast<const VariableDefinitionNode&>(node);
					Push(Declared(OutlineKind::Variable, variable.GetNameToken().GetText(), node, variable.GetNameToken(), parent));
					break;
				}
				case NodeType::UnionDefinition: {
					const auto& value = static_cast<const UnionDefinitionNode&>(node);
					Push(Declared(OutlineKind::Union, value.GetNameToken().GetText(), node, value.GetNameToken(), parent));
					break;
				}
				case NodeType::AliasDefinition: {
					const auto& alias = static_cast<const AliasDefinitionNode&>(node);
					Push(Declared(OutlineKind::Alias, alias.GetNameToken().GetText(), node, alias.GetNameToken(), parent));
					break;
				}
				default:
					break; // includes, statements and expressions declare nothing an outline lists
				}
			}

			void AddNamespace(const NamespaceNode& space, int32 parent)
			{
				if (space.IsRoot())
				{
					Children(space, parent);
					return;
				}
				Children(space, Push(Declared(OutlineKind::Namespace, space.Name(), space, space.GetNameToken(), parent)));
			}

			void AddObject(const ObjectDefinitionNode& object, int32 parent)
			{
				const int32 index = Push(Declared(ObjectKindOf(object), object.GetNameToken().GetText(), object, object.GetNameToken(), parent));
				for (const Ref<AstNode>& member : object.GetBodyMembers())
					if (member) Add(*member, index);
			}

			void AddEnum(const EnumDefinitionNode& enumeration, int32 parent)
			{
				const int32 index = Push(Declared(OutlineKind::Enum, enumeration.GetNameToken().GetText(), enumeration, enumeration.GetNameToken(), parent));
				for (const Ref<EnumMemberNode>& value : enumeration.GetMembers())
					if (value) Push(Declared(OutlineKind::EnumValue, value->GetNameToken().GetText(), *value, value->GetNameToken(), index));
			}

			void AddFunction(const FunctionDefinitionNode& function, int32 parent)
			{
				const std::string name = WrittenFunctionName(function);
				if (!name.empty()) Push(Declared(OutlineKind::Function, name, function, function.GetName()->GetFirstIdentifierToken(), parent));
			}
		};

		std::vector<OutlineDiagnostic> MacroDiagnosticsOf(const MacroPass::FileResult& result)
		{
			std::vector<OutlineDiagnostic> diagnostics;
			for (const MacroDiagnostic& found : result.diagnostics)
				diagnostics.push_back({ SeverityOf(found.severity), { found.line, found.column }, found.message });
			return diagnostics;
		}

		// The lexer's diagnostics first: a bad literal or an unclosed comment causes the parse errors after it.
		void AddParseDiagnostics(const Lexer& lexer, const ErrorTracker& errors, std::vector<OutlineDiagnostic>& diagnostics)
		{
			for (const auto& entry : lexer.Diagnostics())
				diagnostics.push_back({ OutlineSeverity::Error, PositionOf(entry.second.location), entry.second.message });
			for (const CompilationError& error : errors.GetErrors())
				diagnostics.push_back({ SeverityOf(error.severity), PositionOf(error.location), error.message });
		}

		MacroPass::FileResult Preprocessed(const std::string& text, const std::map<std::string, std::string>& defines)
		{
			MacroPass::Input file;
			file.absPath = kOutlinedFile;
			file.name = kOutlinedFile;
			file.text = text;
			file.predefined = PredefinedMacros(false, CpuTarget(), {});
			for (const auto& [name, value] : defines) file.predefined[name] = value;
			std::vector<MacroPass::Input> files;
			files.push_back(std::move(file));
			std::vector<MacroPass::FileResult> results = MacroPass::RunFiles(std::move(files), [](const std::string&, const std::string&) { return std::string(); });
			return results.empty() ? MacroPass::FileResult() : std::move(results.front());
		}
	}

	std::vector<OutlineDeclaration> DeclarationsOf(const NamespaceNode& root)
	{
		OutlineWalk walk;
		walk.Children(root, -1);
		return std::move(walk.declarations);
	}

	SourceOutline OutlineOf(const std::string& text, const std::map<std::string, std::string>& defines)
	{
		const MacroPass::FileResult expanded = Preprocessed(text, defines);
		SourceOutline outline;
		outline.diagnostics = MacroDiagnosticsOf(expanded);
		Ref<Source> source = Source::FromMemory(expanded.text);
		Lexer lexer(*source, 0);
		ErrorTracker errors;
		ParserState state(lexer, errors);
		auto root = CreateRef<NamespaceNode>();
		root->SetIsRoot(true);
		root->ParseBody(state);
		AddParseDiagnostics(lexer, errors, outline.diagnostics);
		outline.declarations = DeclarationsOf(*root);
		return outline;
	}
}
