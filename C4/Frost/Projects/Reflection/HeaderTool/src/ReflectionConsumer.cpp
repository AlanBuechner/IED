#include "ReflectionConsumer.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/DeclGroup.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/AST/QualTypeNames.h"
#include "llvm/Support/FileSystem.h"

fs::path clang::ReflectionConsumer::s_GeneratedFolder = "";
fs::path clang::ReflectionConsumer::s_SourceFolder = "";
std::unordered_map<fs::path, Reflection::File> clang::ReflectionConsumer::s_ReflectedFiles;

bool is_subpath(const std::filesystem::path& path, const std::filesystem::path& base) {
	auto rel = std::filesystem::relative(path, base);
	return !rel.empty() && rel.native()[0] != '.';
}

namespace clang {

	// --------------------------- Helper Functions --------------------------- //
	std::string GetAnnotations(Attr const* attr) {
		std::string str;
		llvm::raw_string_ostream os(str);
		LangOptions langopts;
		PrintingPolicy policy(langopts);
		attr->printPretty(os, policy);
		// remove [__attribute__((annotate("] from start of string and [")))] from the end
		int formatingStartSize = std::string("__attribute__((annotate(\"").size();
		int formatingEndSize = std::string("\")))").size();
		return str.substr(formatingStartSize, str.size() - formatingEndSize - formatingStartSize);
	}

	std::string GetAnnotations(const AttrVec& attrs) {
		std::string attributes;
		for (uint32_t i = 0; i < attrs.size(); i++) {
			auto& attr = attrs[i];
			if (attr->getKind() == attr::Annotate) {
				attributes += GetAnnotations(attr);
			}
			if (i != attrs.size() - 1)
				attributes += ",";
		}
		return attributes;
	}

	std::string GetTypeAsString(QualType type, ASTContext* ctx) {
		PrintingPolicy policy(ctx->getPrintingPolicy());
		policy.SuppressScope = false;
		policy.AnonymousTagLocations = false;
		policy.FullyQualifiedName = true;
		policy.SuppressTagKeyword = false;
		policy.AlwaysIncludeTypeForTemplateArgument = true;
		return TypeName::getFullyQualifiedName(type, (const ASTContext&)*ctx, policy,
			false);
	}

	// --------------------------- Template Argument Extraction --------------------------- //

	std::vector<std::string> ExtractTemplateArgs(QualType type, ASTContext* ctx) {
		std::vector<std::string> args;
		const Type* ty = type.getTypePtrOrNull();
		if (!ty)
			return args;

		const TemplateSpecializationType* specType = dyn_cast<TemplateSpecializationType>(ty);
		if (!specType)
			return args;

		for (const auto& arg : specType->template_arguments()) {
			if (arg.getKind() == TemplateArgument::Type) {
				QualType argType = arg.getAsType();
				args.push_back(GetTypeAsString(argType, ctx));
			} else if (arg.getKind() == TemplateArgument::Integral) {
				auto val = arg.getAsIntegral();
				args.push_back(std::to_string(val.getLimitedValue()));
			}
		}

		return args;
	}

	std::string ClassifyContainer(const std::string& typeName) {
		// Sequential containers: Vector, vector, list
		if (typeName == "Core::Vector" || typeName == "std::vector" || typeName == "std::list")
			return "sequential";

		// Associative containers: map, unordered_map
		if (typeName == "std::map" || typeName == "std::unordered_map")
			return "associative";

		return "";
	}

	// --------------------------- Reflected Class --------------------------- //

	Reflection::Class BuildClass(const CXXRecordDecl* rec, ASTContext* ctx, const std::string& attrs)
	{
		Reflection::Class c = Reflection::Class(rec->getQualifiedNameAsString(), attrs);

		// load props
		for (const auto& field : rec->fields()) {
			std::string attrs = GetAnnotations(field->getAttrs());
			if (StringRef(attrs).starts_with("reflect-property")) {
				QualType type = field->getType().getDesugaredType((const ASTContext&)*ctx);
				std::string typeName = GetTypeAsString(type, ctx);

				Reflection::Property prop = Reflection::Property(field->getName().str(), typeName, attrs);

				// Extract template arguments from the property's type
				auto templateArgs = ExtractTemplateArgs(type, ctx);
				for (const auto& arg : templateArgs) {
					prop.m_TemplateArgs.Push(arg);
				}

				// Classify container kind based on the fully qualified type name
				prop.m_ContainerKind = ClassifyContainer(typeName);

				// Build path from class qualified name and field name
				prop.m_Path = rec->getQualifiedNameAsString() + "." + field->getName().str();

				c.m_Props.Push(prop);
			}

		}

		// load functions
		for (const auto& func : rec->methods()) {
			std::string attrs = GetAnnotations(func->getAttrs());
			if (StringRef(attrs).starts_with("reflect-function")) {
				std::string name = func->getNameAsString();
				Core::Vector<Core::String> types;
				for (unsigned int i = 0; i < func->getNumParams(); i++) {
					QualType type = func->getParamDecl(i)->getType()
						.getDesugaredType((const ASTContext&)*ctx)
						.getNonReferenceType();
					std::string typeName = GetTypeAsString(type, ctx);
					types.Push(typeName);
				}
				Reflection::Function func = Reflection::Function(name, types, attrs);
				c.m_Functions.Push(func);
			}
		}

		return c;
	}


	// --------------------------- Class finder --------------------------- //

	void ClassFinder::run(MatchFinder::MatchResult const& result) {
		m_context = result.Context;
		m_sourceman = result.SourceManager;

		CXXRecordDecl const* record = result.Nodes.getNodeAs<clang::CXXRecordDecl>("id");
		if (record)
		{
			// check if it has any attributes
			const AttrVec attrs = record->getAttrs();
			if (attrs.size() == 0)
				return;

			// get the attribute string
			std::string attributes = GetAnnotations(attrs);
			if (!StringRef(attributes).starts_with("reflect-class")) // return if it is not reflected
				return;

			// check if this class is in this project
			fs::path header = fs::path(m_sourceman->getFilename(record->getLocation()).str());
			if (!is_subpath(header, ReflectionConsumer::s_SourceFolder))
				return;

			Reflection::File& file = ReflectionConsumer::GetFile(header);
			file.m_Classes.Push(BuildClass(record, m_context, attributes));
		}
	}


	// --------------------------- Reflection Consumer --------------------------- //
	void ReflectionConsumer::Init(const fs::path& generatedFolder, const fs::path& sourceFolder)
	{
		s_GeneratedFolder = fs::absolute(generatedFolder);
		s_SourceFolder = fs::absolute(sourceFolder);
		std::filesystem::create_directory(s_GeneratedFolder);
	}


	void ReflectionConsumer::HandleTranslationUnit(ASTContext& Ctx) {

		// find classes
		ClassFinder classFinder;
		MatchFinder finder;
		DeclarationMatcher classMatcher = cxxRecordDecl(decl().bind("id"));
		finder.addMatcher(classMatcher, &classFinder);
		finder.matchAST(Ctx);

		// find functions


		// save file
		for (auto& file : s_ReflectedFiles) {
			fs::path generatedPath = ReflectionConsumer::s_GeneratedFolder / (file.first.filename().string() + ".reflected");
			file.second.Save(generatedPath);
		}
	}

	Reflection::File& ReflectionConsumer::GetFile(fs::path path)
	{
		// add file if not found
		auto loc = s_ReflectedFiles.find(path);
		if (loc == s_ReflectedFiles.end())
			s_ReflectedFiles[path] = Reflection::File(path.string());

		// add class to file
		return s_ReflectedFiles[path];
	}

} // namespace clang
