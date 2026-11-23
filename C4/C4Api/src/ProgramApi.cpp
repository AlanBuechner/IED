// The C API's version 2 (docs/tools/06-c4-api.md section 3): a whole program, analyzed and queried through the language
// server's own session, resolver and features, so the answers are the ones the editor gets.

#include "Core/Logging/Log.h"
#include "C4Api.h"
#include "ApiSupport.h"
#include "Features/DeclarationText.h"
#include "Features/Definition.h"
#include "Features/Implementation.h"
#include "Features/References.h"
#include "Features/TypeText.h"
#include "Model/TypeHandle.h"
#include "Model/Types.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Protocol/LspTypes.h"
#include "Server/CompilationSession.h"
#include "Server/NodeLocator.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
#include <algorithm>
#include <filesystem>
#include <list>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using C4LSP::Features::DeclarationText;
using C4LSP::Server::CompilationSession;
namespace Protocol = C4LSP::Protocol;

namespace
{
	// A place as the API gives it: the strings are its own.
	struct Place
	{
		std::string file;
		c4a_position start = {};
		c4a_position end = {};
	};

	struct DiagnosticEntry
	{
		c4a_severity severity = C4A_ERROR;
		Place at;
		std::string message;
	};

	struct MemberEntry
	{
		c4a_kind kind = C4A_VARIABLE;
		std::string name;
		std::string signature;
		std::string access;
		bool isStatic = false;
		Place declared;
	};
}

struct c4a_program
{
	CompilationSession session;
	std::string root;
	std::string error;
	std::vector<DiagnosticEntry> diagnostics;
	std::list<std::string> strings; // what the structs of its queries point to: stable until it is destroyed
	std::mutex lock;

	const char* Own(std::string text)
	{
		strings.push_back(std::move(text));
		return strings.back().c_str();
	}
};

struct c4a_locations
{
	std::vector<Place> places;
};

struct c4a_members
{
	std::vector<MemberEntry> items;
};

namespace
{
	// Where a query is: the file's source, its nodes down to the position.
	struct Spot
	{
		uint32 sourceId = 0;
		std::string uri;
		Protocol::Position position;
		C4LSP::Server::LocatedNode located;
	};

	std::string Slashed(std::string path)
	{
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	}

	std::string NameOfSource(const CompilationSession& session, uint32 sourceId)
	{
		const auto& info = session.GetSourceManager().GetInfo(sourceId);
		return Slashed(info.absPath.empty() ? info.displayName : info.absPath);
	}

	std::string FileOfUri(const CompilationSession& session, const std::string& uri)
	{
		const uint32 sourceId = session.FindSourceIdByUri(uri);
		return sourceId != 0 ? NameOfSource(session, sourceId) : Slashed(Protocol::DocumentUri::FromUri(uri));
	}

	c4a_position PositionOf(const Protocol::Position& at) { return { at.line + 1, at.character + 1 }; }

	Place PlaceOf(const CompilationSession& session, const Protocol::Location& location)
	{
		return { FileOfUri(session, location.uri), PositionOf(location.range.start), PositionOf(location.range.end) };
	}

	c4a_location ToLocation(const Place& place) { return { place.file.c_str(), place.start, place.end }; }

	c4a_kind KindOf(const C4Compiler::AstNode& declaration)
	{
		using C4Compiler::NodeType;
		switch (declaration.Type())
		{
		case NodeType::Namespace: return C4A_NAMESPACE;
		case NodeType::ObjectDefinition:
		{
			const C4Compiler::ObjectKind kind = static_cast<const C4Compiler::ObjectDefinitionNode&>(declaration).Kind();
			return kind == C4Compiler::ObjectKind::Struct ? C4A_STRUCT : kind == C4Compiler::ObjectKind::Interface ? C4A_INTERFACE : C4A_CLASS;
		}
		case NodeType::EnumDefinition: return C4A_ENUM;
		case NodeType::EnumMember: return C4A_ENUM_VALUE;
		case NodeType::UnionDefinition: return C4A_UNION;
		case NodeType::AliasDefinition: return C4A_ALIAS;
		case NodeType::AnnotationDeclaration: return C4A_ANNOTATION;
		case NodeType::FunctionDefinition: return C4A_FUNCTION;
		default: return C4A_VARIABLE;
		}
	}

	const char* AccessName(C4Compiler::AccessModifier access)
	{
		switch (access)
		{
		case C4Compiler::AccessModifier::Private: return "private";
		case C4Compiler::AccessModifier::Protected: return "protected";
		default: return "public";
		}
	}

	std::string QualifiedNameOf(CompilationSession& session, const C4Compiler::AstNode& declaration)
	{
		const std::string owner = DeclarationText::OwnerOf(session, declaration);
		const std::string name = DeclarationText::NameOf(declaration);
		return owner.empty() ? name : owner + "::" + name;
	}

	Place DeclaredPlace(CompilationSession& session, const C4Compiler::AstNode& declaration)
	{
		return PlaceOf(session, C4LSP::Features::Definition::LocationForDeclaration(session, declaration));
	}

	bool LocateAt(c4a_program& program, const char* file, c4a_position at, Spot& spot)
	{
		if (!file || at.line == 0 || at.column == 0)
			return false;
		CompilationSession& session = program.session;
		spot.position = { at.line - 1, at.column - 1 };
		spot.uri = Protocol::DocumentUri::ToUri(file);
		spot.sourceId = session.FindSourceIdByUri(spot.uri);
		if (spot.sourceId == 0 && !program.root.empty())
		{
			spot.uri = Protocol::DocumentUri::ToUri(program.root + "/" + file);
			spot.sourceId = session.FindSourceIdByUri(spot.uri);
		}
		if (spot.sourceId == 0)
			return false;
		auto unit = session.GetCompiler().GetCompilationUnit(spot.sourceId);
		if (!unit || !unit->GetRootAST())
			return false;
		const C4Compiler::Source& source = *session.GetSourceManager().GetInfo(spot.sourceId).source;
		spot.located = C4LSP::Server::NodeLocator::Locate(source, spot.sourceId, unit->GetRootAST(), spot.position);
		return spot.located.Found();
	}

	const C4Compiler::Type* DeclaredTypeOf(CompilationSession& session, const C4Compiler::AstNode& declaration)
	{
		using C4Compiler::NodeType;
		const C4Compiler::QualTypeNode* written = nullptr;
		if (declaration.Type() == NodeType::VariableDefinition)
			written = static_cast<const C4Compiler::VariableDefinitionNode&>(declaration).GetType().Get();
		else if (declaration.Type() == NodeType::Parameter)
			written = static_cast<const C4Compiler::ParameterNode&>(declaration).GetType().Get();
		if (!written)
			return nullptr;
		return session.GetResolver().ResolveQualType(*written, DeclarationText::PathTo(session, declaration));
	}

	// The type of what is at the position, found as the hover finds it: a reference's, a written type's, a declaration's.
	const C4Compiler::Type* TypeAt(CompilationSession& session, const Spot& spot)
	{
		using C4Compiler::NodeType;
		const C4LSP::Server::SemanticResolver& resolver = session.GetResolver();
		const auto& path = spot.located.path;
		for (int i = static_cast<int>(path.Count()) - 1; i >= 0; --i)
		{
			const C4Compiler::AstNode& node = *path[i];
			if (node.Type() == NodeType::LambdaExpression)
				break;
			if (C4LSP::Server::SemanticResolver::IsReference(node, i > 0 ? path[i - 1] : nullptr))
				if (const C4Compiler::Type* type = resolver.ResolveReference(node, path).type)
					return type;
			if (node.Type() == NodeType::QualType)
				return resolver.ResolveQualType(static_cast<const C4Compiler::QualTypeNode&>(node), path);
			if (node.Type() == NodeType::VariableDefinition || node.Type() == NodeType::Parameter)
				return DeclaredTypeOf(session, node);
		}
		return nullptr;
	}

	std::string TypeName(const C4Compiler::Type* type)
	{
		return type ? std::string(C4LSP::Features::TypeText(type).c_str()) : std::string();
	}

	void CollectDiagnostics(c4a_program& program)
	{
		CompilationSession& session = program.session;
		std::set<std::tuple<uint32, uint32, uint32, std::string>> seen;
		auto add = [&](const C4Compiler::CompilationError& error)
		{
			const uint32 sourceId = error.location.sourceId;
			DiagnosticEntry entry;
			entry.severity = error.severity == C4Compiler::ErrorSeverity::Warning ? C4A_WARNING : C4A_ERROR;
			entry.message = error.message;
			if (sourceId != 0)
			{
				const Protocol::Range range = session.ToEditorRange(sourceId, error.location, error.location);
				entry.at = { sourceId != 0 ? NameOfSource(session, sourceId) : std::string(), PositionOf(range.start), PositionOf(range.start) };
			}
			if (seen.insert(std::make_tuple(sourceId, entry.at.start.line, entry.at.start.column, entry.message)).second)
				program.diagnostics.push_back(std::move(entry));
		};
		for (const auto& error : session.GetCompiler().GetAllDiagnostics())
			add(error);
		for (const auto& error : session.GetLoweringDiagnostics())
			add(error);
	}

	MemberEntry MemberOf(CompilationSession& session, const std::string& name, c4a_kind kind, bool isStatic, C4Compiler::AccessModifier access, const C4Compiler::AstNode* declaration)
	{
		MemberEntry member;
		member.kind = kind;
		member.name = name;
		member.isStatic = isStatic;
		member.access = AccessName(access);
		if (declaration)
		{
			member.signature = DeclarationText::Detail(session, DeclarationText::InItsType(session, *declaration));
			member.declared = DeclaredPlace(session, *declaration);
		}
		return member;
	}

	c4a_members* MembersOfObject(CompilationSession& session, const C4Compiler::ObjectType& object)
	{
		c4a_members* members = new c4a_members();
		for (const auto& field : object.GetFields())
			if (!field.m_IsPackedStorage)
				members->items.push_back(MemberOf(session, field.mName.c_str(), C4A_VARIABLE, field.m_IsStatic, field.m_Access, field.m_DeclaringNode));
		for (const auto& method : object.GetMethods())
			members->items.push_back(MemberOf(session, method.mName.c_str(), C4A_FUNCTION, method.m_IsStatic, method.m_Access, method.m_DeclaringNode));
		return members;
	}

	void Analyze(c4a_program& program, const c4a_program_options& options)
	{
		CompilationSession& session = program.session;
		if (options.root)
		{
			program.root = Slashed(std::filesystem::absolute(options.root).lexically_normal().string()); // the session's files are absolute
			session.SetRootFolder(program.root);
		}
		if (options.library_folder) session.SetLibraryFolder(options.library_folder);
		if (options.projects_file) session.SetProjectsFile(options.projects_file);
		std::vector<std::string> includes; // a build registers every source under an -I folder: the root's files are the program
		if (!program.root.empty() && !options.projects_file) includes.push_back(program.root);
		for (uint32_t i = 0; i < options.include_path_count; ++i)
			if (options.include_paths && options.include_paths[i]) includes.push_back(options.include_paths[i]);
		session.SetIncludePaths(includes);
		session.SetDefines(C4Api::DefineMap(options.defines, options.define_count));
		for (uint32_t i = 0; i < options.source_count; ++i)
			if (options.sources && options.sources[i].name && options.sources[i].text)
				session.RegisterInMemorySource(options.sources[i].name, std::string(options.sources[i].text, static_cast<size_t>(options.sources[i].length)));
		session.Recompile();
		program.error = session.GetSetupProblem();
		CollectDiagnostics(program);
	}

	const C4Compiler::ObjectType* ObjectNamed(CompilationSession& session, const std::string& qualifiedName)
	{
		std::vector<C4LSP::Server::SemanticResolver::ObjectDecl> objects;
		session.GetResolver().CollectObjectTypes(objects);
		for (const auto& found : objects)
			if (found.object && found.decl && QualifiedNameOf(session, *found.decl) == qualifiedName)
				return found.object;
		return nullptr;
	}

	const C4Compiler::ObjectType* ObjectAt(CompilationSession& session, const Spot& spot)
	{
		if (const C4Compiler::ObjectType* object = C4LSP::Features::Implementation::TypeUnderCursor(session, spot.located))
			return object;
		const C4Compiler::Type* type = TypeAt(session, spot);
		return type ? dynamic_cast<const C4Compiler::ObjectType*>(C4Compiler::TypeHandle::UnwrapToValue(type)) : nullptr;
	}
}

extern "C"
{
	c4a_program* c4a_program_open(const c4a_program_options* options)
	{
		c4a_program* program = new c4a_program();
		if (!options)
		{
			program->error = "no options";
			return program;
		}
		Analyze(*program, *options);
		return program;
	}

	const char* c4a_program_error(const c4a_program* program)
	{
		return program && !program->error.empty() ? program->error.c_str() : nullptr;
	}

	int32_t c4a_program_bodies_lowered(const c4a_program* program)
	{
		return program && program->session.LoweredBodies() ? 1 : 0;
	}

	void c4a_program_destroy(c4a_program* program) { delete program; }

	uint32_t c4a_program_diagnostic_count(const c4a_program* program)
	{
		return program ? static_cast<uint32_t>(program->diagnostics.size()) : 0;
	}

	int32_t c4a_program_diagnostic_at(const c4a_program* program, uint32_t index, c4a_program_diagnostic* out)
	{
		if (!program || !out || index >= program->diagnostics.size()) return 0;
		const DiagnosticEntry& found = program->diagnostics[index];
		*out = { found.severity, found.at.file.c_str(), found.at.start, found.message.c_str() };
		return 1;
	}

	int32_t c4a_program_resolve(c4a_program* program, const char* file, c4a_position at, c4a_symbol* out)
	{
		if (!program || !out) return 0;
		std::lock_guard<std::mutex> guard(program->lock);
		Spot spot;
		if (!LocateAt(*program, file, at, spot)) return 0;
		CompilationSession& session = program->session;
		const C4Compiler::AstNode* declaration = C4LSP::Features::Definition::TargetAt(session, spot.sourceId, spot.located, spot.position);
		if (!declaration) return 0;
		const C4Compiler::Type* type = TypeAt(session, spot);
		if (!type) type = DeclaredTypeOf(session, *declaration);
		const C4Compiler::AstNode& shown = DeclarationText::InItsType(session, *declaration);
		const Place declared = DeclaredPlace(session, *declaration);
		*out = { KindOf(*declaration), program->Own(QualifiedNameOf(session, *declaration)), program->Own(DeclarationText::Qualified(session, shown)),
			program->Own(TypeName(type)), { program->Own(declared.file), declared.start, declared.end } };
		return 1;
	}

	const char* c4a_program_type_at(c4a_program* program, const char* file, c4a_position at)
	{
		if (!program) return nullptr;
		std::lock_guard<std::mutex> guard(program->lock);
		Spot spot;
		if (!LocateAt(*program, file, at, spot)) return nullptr;
		const C4Compiler::Type* type = TypeAt(program->session, spot);
		return type ? program->Own(TypeName(type)) : nullptr;
	}

	c4a_locations* c4a_program_references(c4a_program* program, const char* file, c4a_position at, int32_t include_declaration)
	{
		if (!program) return nullptr;
		std::lock_guard<std::mutex> guard(program->lock);
		Spot spot;
		if (!LocateAt(*program, file, at, spot)) return nullptr;
		const auto found = C4LSP::Features::References::At(program->session, spot.uri, spot.position, include_declaration != 0);
		if (found.Count() == 0) return nullptr;
		c4a_locations* locations = new c4a_locations();
		for (const Protocol::Location& location : found)
			locations->places.push_back(PlaceOf(program->session, location));
		return locations;
	}

	uint32_t c4a_locations_count(const c4a_locations* locations) { return locations ? static_cast<uint32_t>(locations->places.size()) : 0; }

	int32_t c4a_locations_at(const c4a_locations* locations, uint32_t index, c4a_location* out)
	{
		if (!locations || !out || index >= locations->places.size()) return 0;
		*out = ToLocation(locations->places[index]);
		return 1;
	}

	void c4a_locations_destroy(c4a_locations* locations) { delete locations; }

	c4a_members* c4a_program_members_of(c4a_program* program, const char* qualified_name)
	{
		if (!program || !qualified_name) return nullptr;
		std::lock_guard<std::mutex> guard(program->lock);
		const C4Compiler::ObjectType* object = ObjectNamed(program->session, qualified_name);
		return object ? MembersOfObject(program->session, *object) : nullptr;
	}

	c4a_members* c4a_program_members_at(c4a_program* program, const char* file, c4a_position at)
	{
		if (!program) return nullptr;
		std::lock_guard<std::mutex> guard(program->lock);
		Spot spot;
		if (!LocateAt(*program, file, at, spot)) return nullptr;
		const C4Compiler::ObjectType* object = ObjectAt(program->session, spot);
		return object ? MembersOfObject(program->session, *object) : nullptr;
	}

	uint32_t c4a_members_count(const c4a_members* members) { return members ? static_cast<uint32_t>(members->items.size()) : 0; }

	int32_t c4a_members_at(const c4a_members* members, uint32_t index, c4a_member* out)
	{
		if (!members || !out || index >= members->items.size()) return 0;
		const MemberEntry& found = members->items[index];
		*out = { found.kind, found.name.c_str(), found.signature.c_str(), found.access.c_str(), found.isStatic ? 1 : 0, ToLocation(found.declared) };
		return 1;
	}

	void c4a_members_destroy(c4a_members* members) { delete members; }
}
