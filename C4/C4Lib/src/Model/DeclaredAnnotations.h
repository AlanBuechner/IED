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
		Core::String name;          // its own name, `Range`
		Core::String qualifiedName; // `Scene::Range`
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

	/// A declaration that declares nothing as written: said where it is, by the model's check.
	struct AnnotationDeclarationProblem
	{
		SourceLocation at;
		Core::String message;
	};

	/// The annotations a program declares, each by its own name with its overloads: `annotation Range(double min, double max)
	/// on field;` declares `[Range]`, whatever namespace it is written in.
	class DeclaredAnnotations
	{
	public:
		using Overloads = Core::Vector<const AnnotationSignature*>;

		DeclaredAnnotations() = default;
		DeclaredAnnotations(const ProgramModel& model, const Core::Vector<AnnotationDeclarationSite>& sites);

		/// The declarations of the annotation `name` (`Range`, or `Scene::Range` by its last part); null when none declares it.
		const Overloads* Find(const std::string& name) const;
		/// The declarations of the annotation `name` is a typo of: none is named so, and one is a near miss of it (the closest);
		/// null otherwise.
		const Overloads* MisspeltBy(const std::string& name) const;
		/// Every declared annotation's overloads, by its own name.
		const std::unordered_map<std::string, Overloads>& All() const { return m_ByName; }
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

		std::vector<std::unique_ptr<AnnotationSignature>> m_Signatures; // std::vector: Core::Vector copies when it is assigned
		std::unordered_map<std::string, Overloads> m_ByName;
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
