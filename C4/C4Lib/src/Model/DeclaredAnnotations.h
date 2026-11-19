#pragma once
#include "Model/AnnotationPlaces.h"
#include "Model/ConstantValue.h"
#include "Lexer/Token.h"
#include "Parser/Nodes/Declarations/AnnotationDeclarationNode.h" // a signature's declaration is an AST node
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Scripting
{
	class ProgramModel;
	class SymbolTable;
	class Type;
	struct AstNode;
	struct ParameterNode;
	struct Scope;

	/// What an annotation parameter takes (docs/language/12 section 4.1).
	enum class AnnotationParameterKind : uint8 { Bool, Integer, Float, String, Enum, Type };

	/// One argument of a reflected annotation: a constant folded where the declaration is written, or the type it names
	/// (`[Requires(Render::MeshRenderer)]`).
	struct ReflectedArgument
	{
		ConstantValue value;        // the constant; None for a type
		const Type* type = nullptr; // the type the argument names; null for a constant
	};

	struct AnnotationParameter
	{
		Core::String name;
		AnnotationParameterKind kind = AnnotationParameterKind::Integer;
		const Type* type = nullptr; // what it is written as (an integer's width, the enum); null for typename
		Core::String writtenType;   // as the declaration writes it: `const char*`, `Render::Pass`
		bool isPack = false;
		bool hasDefault = false;
		ReflectedArgument defaultValue; // as the parameter takes it

		/// `double min`, `typename... more`, `double by = 1`.
		Core::String ToString() const;
	};

	/// One declaration of an annotation: `annotation Range(double min, double max) on field;`.
	struct AnnotationSignature
	{
		const AnnotationDeclarationNode* declaration = nullptr;
		Core::String name;            // its own name, `Range`
		Core::String qualifiedName;   // `Scene::Range` in namespace Scene; what reflection registers it as
		const Scope* scope = nullptr; // the namespace it is declared in; the global one's scope at namespace scope
		uint32 unit = 0;              // the file that declares it: a use sees it when the use's file includes it
		AnnotationPlaces places = AnnotationPlaces::Everywhere();
		Core::Vector<AnnotationParameter> parameters;

		bool HasPack() const { return !parameters.Empty() && parameters.Back().isPack; }
		/// The parameters before the pack, if there is one.
		uint32 FixedCount() const { return HasPack() ? parameters.Count() - 1 : parameters.Count(); }
		/// The arguments a use must write: the parameters before the pack that have no default.
		uint32 RequiredCount() const;
		/// The parameter argument `index` is for: a fixed one, else the pack.
		const AnnotationParameter& ParameterFor(uint32 index) const { return parameters[index < FixedCount() ? index : parameters.Count() - 1]; }
		/// `Range(double min, double max)`.
		Core::String ToString() const;
	};

	/// Where an annotation is declared: its declaration, the scope it is written in and its file.
	struct AnnotationDeclarationSite
	{
		const AnnotationDeclarationNode* declaration = nullptr;
		const Scope* scope = nullptr;
		uint32 unitId = 0;
	};

	/// Where a use of an annotation is written, which is where its name is looked up from: the scope of what it is on (a
	/// type's own scope for the type, its fields and methods; a namespace for the rest; null for the global namespace) and
	/// its file.
	struct AnnotationContext
	{
		const Scope* scope = nullptr;
		uint32 unit = 0;
	};

	/// A declaration that declares nothing as written: said where it is, by the model's check.
	struct AnnotationDeclarationProblem
	{
		SourceLocation at;
		Core::String message;
	};

	/// The annotations a program declares, each by its qualified name with its overloads. A use names one as any name is
	/// named (docs/language/12 section 4.2): `[Range]` from the use's scope out to the global namespace, the first that
	/// declares it in a file the use's file includes; `[Scene::Range]` the one namespace Scene declares.
	class DeclaredAnnotations
	{
	public:
		using Overloads = Core::Vector<const AnnotationSignature*>;

		DeclaredAnnotations() = default;
		DeclaredAnnotations(const ProgramModel& model, const Core::Vector<AnnotationDeclarationSite>& sites);

		/// The declarations `written` (`Range`, `Scene::Range`) names at `at`: those of the innermost namespace around the
		/// use that declares it, the ones the use's file sees; empty when none is seen.
		Overloads Find(const std::string& written, const AnnotationContext& at) const;
		/// Every declaration of the annotation `qualifiedName` (`Scene::Range`), or null.
		const Overloads* Named(const std::string& qualifiedName) const;
		/// The declarations a use of `written` that names none means: the closest near miss of its own name that it would
		/// name with the same qualifier; empty when there is none.
		Overloads MisspeltBy(const std::string& written, const AnnotationContext& at) const;
		/// Why an annotation of `written`'s own name is declared but not seen at `at` - in a namespace the use is not in,
		/// or in a file its file does not include - as an error says it; empty when none is declared.
		Core::String Unseen(const ProgramModel& model, const std::string& written, const AnnotationContext& at) const;
		/// Every declared annotation's overloads, by its qualified name.
		const std::unordered_map<std::string, Overloads>& All() const { return m_ByQualifiedName; }
		/// The declarations that declare nothing as written, each with why.
		const Core::Vector<AnnotationDeclarationProblem>& Problems() const { return m_Problems; }
		/// The signature `declaration` declares, or null.
		const AnnotationSignature* Declared(const AstNode* declaration) const;

		/// `Range` of `Scene::Range`.
		static std::string OwnName(const std::string& name);

	private:
		void Declare(const ProgramModel& model, const AnnotationDeclarationSite& site);
		bool ReadPlaces(const AnnotationDeclarationNode& declaration, AnnotationSignature& signature);
		bool ReadParameters(const ProgramModel& model, const AnnotationDeclarationSite& site, AnnotationSignature& signature);
		bool ReadParameter(const ProgramModel& model, const AnnotationDeclarationSite& site, const ParameterNode& written,
			AnnotationParameter& parameter);
		bool ReadDefault(const ProgramModel& model, const AnnotationDeclarationSite& site, const ParameterNode& written,
			AnnotationParameter& parameter);
		bool Problem(const SourceLocation& at, const Core::String& message) { m_Problems.Push({ at, message }); return false; }
		/// The overloads of `qualifiedName` a use in `unit` sees.
		Overloads Seen(const std::string& qualifiedName, uint32 unit) const;

		const SymbolTable* m_Symbols = nullptr; // whose include closures say which file sees which
		std::vector<std::unique_ptr<AnnotationSignature>> m_Signatures; // std::vector: Core::Vector copies when it is assigned
		std::unordered_map<std::string, Overloads> m_ByQualifiedName;
		Core::Vector<AnnotationDeclarationProblem> m_Problems;
	};

	/// What a parameter of `type` takes; false when an annotation takes no such argument.
	bool AnnotationParameterKindOf(const Type* type, AnnotationParameterKind& out);
	/// `a double`, `a string`, `a Render::Pass`, `a type`: what a parameter takes, as an error says it.
	Core::String DescribeParameter(const AnnotationParameter& parameter);
	/// `"far"`, `0.5`, `true`, `Render::Mesh`: a folded argument, as an error quotes it.
	Core::String DescribeArgument(const ReflectedArgument& argument);
	/// `a string`, `an integer`, `a type`: what a folded argument is.
	Core::String ArgumentKindNoun(const ReflectedArgument& argument);
} // namespace Scripting
