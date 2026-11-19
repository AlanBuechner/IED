#include "Model/DeclaredAnnotations.h"
#include "Model/AnnotationArguments.h"
#include "Model/Annotations.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/PointerType.h"
#include "Model/Types/QualifiedName.h"
#include "Parser/NearNames.h"
#include "Parser/Nodes/Declarations/AnnotationDeclarationNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include <algorithm>

namespace Scripting
{
	namespace
	{
		constexpr const char* kParameterRule = "an annotation parameter is a bool, an integer, a float or double, a const char*, an enum or typename";

		Core::String Article(const Core::String& noun)
		{
			const char first = noun.empty() ? 'x' : noun[0];
			const bool vowel = first == 'a' || first == 'e' || first == 'i' || first == 'o'; // `a uint8`, as it is said
			return (vowel ? "an " : "a ") + noun;
		}

		// A string as C4 writes it: quoted, a quote, backslash or line end escaped.
		Core::String Quoted(const std::string& text)
		{
			const char backslash = 92;
			Core::String out = "\"";
			for (const char c : text)
			{
				if (c == '"' || c == backslash) out += backslash;
				if (c == '\n') { out += backslash; out += 'n'; continue; }
				out += c;
			}
			return out + "\"";
		}

		Core::String QualifiedNameOf(const Scope* scope, const Core::String& name)
		{
			Core::String qualified = name;
			for (const Scope* at = scope; at; at = at->GetParent())
				if (at->GetKind() == Scope::Kind::Namespace && !at->GetName().empty()) qualified = at->GetName() + "::" + qualified;
			return qualified;
		}

		// The namespaces a name written in `scope` is looked up in, innermost first, as each qualifies a name: `Scene::Inner`,
		// `Scene`, then the global namespace's "". A type's scope adds nothing: annotations are declared in namespaces.
		std::vector<std::string> NamespacesOutward(const Scope* scope)
		{
			std::vector<std::string> outward;
			for (const Scope* at = scope; at; at = at->GetParent())
				if (at->GetKind() == Scope::Kind::Namespace && !at->GetName().empty()) outward.push_back(QualifiedNameOf(at->GetParent(), at->GetName()).c_str());
			outward.push_back("");
			return outward;
		}

		std::string Qualify(const std::string& prefix, const std::string& name) { return prefix.empty() ? name : prefix + "::" + name; }

		// `namespace Scene`, `the global namespace`: where an error says an annotation is declared.
		Core::String NamespaceOf(const AnnotationSignature& signature)
		{
			const size_t colon = signature.qualifiedName.rfind("::");
			return colon == Core::String::npos ? Core::String("the global namespace") : "namespace " + signature.qualifiedName.substr(0, colon);
		}

		bool SameParameters(const AnnotationSignature& a, const AnnotationSignature& b)
		{
			if (a.parameters.Count() != b.parameters.Count()) return false;
			for (uint32 i = 0; i < a.parameters.Count(); ++i)
			{
				const AnnotationParameter& x = a.parameters[i];
				const AnnotationParameter& y = b.parameters[i];
				if (x.kind != y.kind || x.type != y.type || x.isPack != y.isPack) return false;
			}
			return true;
		}
	}

	// --- AnnotationParameter, AnnotationSignature ------------------------------------------------------------------------------

	Core::String AnnotationParameter::ToString() const
	{
		Core::String text = kind == AnnotationParameterKind::Type ? Core::String("typename") : writtenType;
		if (isPack) text += "...";
		if (!name.empty()) text += " " + name;
		if (hasDefault) text += " = " + DescribeArgument(defaultValue);
		return text;
	}

	uint32 AnnotationSignature::RequiredCount() const
	{
		uint32 required = 0;
		for (uint32 i = 0; i < FixedCount(); ++i)
			if (!parameters[i].hasDefault) required = i + 1;
		return required;
	}

	Core::String AnnotationSignature::ToString() const
	{
		Core::String text = name + "(";
		for (uint32 i = 0; i < parameters.Count(); ++i) text += (i > 0 ? ", " : "") + parameters[i].ToString();
		return text + ")";
	}

	// --- DeclaredAnnotations -------------------------------------------------------------------------------------------------

	DeclaredAnnotations::DeclaredAnnotations(const ProgramModel& model, const Core::Vector<AnnotationDeclarationSite>& sites)
		: m_Symbols(model.Symbols())
	{
		for (const AnnotationDeclarationSite& site : sites)
			if (site.declaration) Declare(model, site);
	}

	void DeclaredAnnotations::Declare(const ProgramModel& model, const AnnotationDeclarationSite& site)
	{
		const AnnotationDeclarationNode& declaration = *site.declaration;
		const SourceLocation& at = declaration.GetNameToken().GetStartLocation();
		auto signature = std::make_unique<AnnotationSignature>();
		signature->declaration = &declaration;
		signature->name = declaration.GetName();
		signature->qualifiedName = QualifiedNameOf(site.scope, signature->name);
		signature->scope = site.scope;
		signature->unit = site.unitId;
		if (signature->name == kAnnotationKeyword)
		{
			Problem(at, "'annotation' starts an annotation's declaration: it names none");
			return;
		}
		if (IsCompilerAnnotationName(signature->name))
		{
			Problem(at, "'" + signature->name + "' is the compiler's own annotation (docs/language/12 section 2): it cannot be declared");
			return;
		}
		if (!ReadPlaces(declaration, *signature) || !ReadParameters(model, site, *signature)) return;

		Overloads& overloads = m_ByQualifiedName[signature->qualifiedName.c_str()];
		for (const AnnotationSignature* earlier : overloads)
			if (SameParameters(*earlier, *signature))
			{
				Problem(at, "[" + signature->qualifiedName + "] is declared twice with the same parameters: " + earlier->ToString());
				return;
			}
		overloads.Push(signature.get());
		m_Signatures.push_back(std::move(signature));
	}

	bool DeclaredAnnotations::ReadPlaces(const AnnotationDeclarationNode& declaration, AnnotationSignature& signature)
	{
		if (declaration.GetPlaceTokens().Empty()) return true;
		signature.places = AnnotationPlaces::None();
		for (const Token& written : declaration.GetPlaceTokens())
		{
			AnnotationPlace place;
			if (!ParseAnnotationPlace(written.GetText().c_str(), place))
				return Problem(written.GetStartLocation(), "'" + written.GetText() + "' is no place an annotation goes: one of " + AnnotationPlaceNames());
			if (signature.places.Has(place))
				return Problem(written.GetStartLocation(), "[" + signature.name + "] goes on " + AnnotationPlaceNoun(place) + " once: '" + written.GetText() +
					"' is written twice");
			signature.places.Add(place);
		}
		return true;
	}

	bool DeclaredAnnotations::ReadParameters(const ProgramModel& model, const AnnotationDeclarationSite& site, AnnotationSignature& signature)
	{
		const auto& written = site.declaration->GetParameters();
		bool defaulted = false;
		for (uint32 i = 0; i < written.Count(); ++i)
		{
			AnnotationParameter parameter;
			if (!ReadParameter(model, site, *written[i], parameter)) return false;
			const SourceLocation& at = written[i]->startLocation;
			if (parameter.isPack && i + 1 < written.Count())
				return Problem(at, "a pack is an annotation's last parameter: '" + parameter.ToString() + "' is followed by more");
			if (defaulted && !parameter.hasDefault && !parameter.isPack)
				return Problem(at, "a parameter with a default is followed only by parameters with one, or a pack: '" + parameter.ToString() + "' has none");
			defaulted = defaulted || parameter.hasDefault;
			signature.parameters.Push(std::move(parameter));
		}
		return true;
	}

	bool DeclaredAnnotations::ReadParameter(const ProgramModel& model, const AnnotationDeclarationSite& site, const ParameterNode& written,
		AnnotationParameter& parameter)
	{
		parameter.name = written.HasName() ? written.GetNameToken().GetText() : Core::String();
		parameter.isPack = written.IsPack();
		const QualTypeNode* type = written.GetType().Get();
		if (!type) return false;
		parameter.writtenType = type->ToString();
		if (type->IsTypename())
			parameter.kind = AnnotationParameterKind::Type;
		else
		{
			auto resolved = model.ResolveTypeInUnitContext(*type, site.unitId, site.scope);
			if (!resolved.Ok()) return Problem(type->startLocation, Core::String(kParameterRule) + ": '" + type->ToString() + "' names no type (" + resolved.message + ")");
			parameter.type = TypeHandle::UnwrapAliases(resolved.value);
			if (!AnnotationParameterKindOf(parameter.type, parameter.kind))
				return Problem(type->startLocation, Core::String(kParameterRule) + ": '" + type->ToString() + "' is none of them");
		}
		return ReadDefault(model, site, written, parameter);
	}

	bool DeclaredAnnotations::ReadDefault(const ProgramModel& model, const AnnotationDeclarationSite& site, const ParameterNode& written,
		AnnotationParameter& parameter)
	{
		const AstNode* value = written.GetDefaultValue().Get();
		if (!value) return true;
		if (parameter.kind == AnnotationParameterKind::Type || parameter.isPack)
			return Problem(value->startLocation, "'" + parameter.ToString() + "' takes no default: a " +
				(parameter.isPack ? "pack takes as many arguments as are written" : "typename's argument is written"));
		auto folded = FoldAnnotationArgument(model, *value, nullptr, site.scope);
		if (!folded.Ok()) return Problem(folded.loc, folded.message);
		const ArgumentFit fit = FitArgument(folded.value, value, parameter);
		if (!fit.fits) return Problem(value->startLocation, "the default of '" + parameter.ToString() + "' is " + DescribeParameter(parameter) + ": " + fit.why);
		parameter.hasDefault = true;
		parameter.defaultValue = fit.value;
		return true;
	}

	std::string DeclaredAnnotations::OwnName(const std::string& name)
	{
		const size_t colon = name.rfind("::");
		return colon == std::string::npos ? name : name.substr(colon + 2);
	}

	DeclaredAnnotations::Overloads DeclaredAnnotations::Seen(const std::string& qualifiedName, uint32 unit) const
	{
		Overloads seen;
		if (const Overloads* overloads = Named(qualifiedName))
			for (const AnnotationSignature* overload : *overloads)
				if (!m_Symbols || m_Symbols->IsVisible(overload->unit, unit)) seen.Push(overload);
		return seen;
	}

	const DeclaredAnnotations::Overloads* DeclaredAnnotations::Named(const std::string& qualifiedName) const
	{
		auto found = m_ByQualifiedName.find(qualifiedName);
		return found == m_ByQualifiedName.end() ? nullptr : &found->second;
	}

	// As C4 looks up any name: the innermost namespace around the use whose `written` the use's file sees wins, and hides
	// the ones further out.
	DeclaredAnnotations::Overloads DeclaredAnnotations::Find(const std::string& written, const AnnotationContext& at) const
	{
		for (const std::string& prefix : NamespacesOutward(at.scope))
		{
			Overloads seen = Seen(Qualify(prefix, written), at.unit);
			if (!seen.Empty()) return seen;
		}
		return {};
	}

	DeclaredAnnotations::Overloads DeclaredAnnotations::MisspeltBy(const std::string& written, const AnnotationContext& at) const
	{
		const std::string own = OwnName(written);
		const std::string qualifier = written.substr(0, written.size() - own.size()); // `Scene::` of `Scene::Rnage`
		Overloads closest;
		std::string closestName;
		size_t closestEdits = 0;
		for (const auto& [qualified, overloads] : m_ByQualifiedName)
		{
			const std::string declared = overloads[0]->name.c_str();
			if (declared == own || !IsNearMiss(own, declared)) continue;
			const size_t edits = EditDistance(own, declared);
			if (!closest.Empty() && (edits > closestEdits || (edits == closestEdits && declared >= closestName))) continue; // the same answer in any map order
			Overloads meant = Find(qualifier + declared, at);
			if (meant.Empty()) continue;
			closest = std::move(meant);
			closestName = declared;
			closestEdits = edits;
		}
		return closest;
	}

	Core::String DeclaredAnnotations::Unseen(const ProgramModel& model, const std::string& written, const AnnotationContext& at) const
	{
		const std::string own = OwnName(written);
		std::vector<std::string> sameName;
		for (const auto& [qualified, overloads] : m_ByQualifiedName)
			if (overloads[0]->name == own) sameName.push_back(qualified);
		std::sort(sameName.begin(), sameName.end());
		const std::vector<std::string> outward = NamespacesOutward(at.scope);
		auto reached = [&](const std::string& qualified) {
			for (const std::string& prefix : outward)
				if (Qualify(prefix, written) == qualified) return true;
			return false;
		};
		const Core::String here = "annotation [" + Core::String(written.c_str()) + "] is not visible here: ";
		for (const std::string& qualified : sameName) // one the use names, in a file its file does not include, comes first
			if (reached(qualified))
			{
				const AnnotationSignature& declared = *(*Named(qualified))[0];
				return here + "it is declared in " + model.SourceName(declared.unit) + ", which this file does not include";
			}
		if (sameName.empty()) return {};
		const AnnotationSignature& declared = *(*Named(sameName[0]))[0];
		const bool seen = !Seen(sameName[0], at.unit).Empty();
		return here + declared.qualifiedName + " is declared in " + NamespaceOf(declared) + ": write [" + declared.qualifiedName + "]" +
			(seen ? Core::String() : ", and include " + model.SourceName(declared.unit));
	}

	const AnnotationSignature* DeclaredAnnotations::Declared(const AstNode* declaration) const
	{
		for (const auto& signature : m_Signatures)
			if (signature->declaration == declaration) return signature.get();
		return nullptr;
	}

	// --- Kinds -----------------------------------------------------------------------------------------------------------

	bool AnnotationParameterKindOf(const Type* type, AnnotationParameterKind& out)
	{
		if (!type) return false;
		switch (type->GetKind())
		{
		case TypeKind::Bool: out = AnnotationParameterKind::Bool; return true;
		case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64:
		case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt32: case TypeKind::UInt64:
			out = AnnotationParameterKind::Integer;
			return true;
		case TypeKind::Float32: case TypeKind::Float64: out = AnnotationParameterKind::Float; return true;
		case TypeKind::Enum: out = AnnotationParameterKind::Enum; return true;
		case TypeKind::Pointer: {
			const Type* pointee = TypeHandle::UnwrapAliases(static_cast<const PointerType*>(type)->GetPointeeType());
			if (!pointee || pointee->GetKind() != TypeKind::Char8) return false;
			out = AnnotationParameterKind::String;
			return true;
		}
		default: return false;
		}
	}

	Core::String DescribeParameter(const AnnotationParameter& parameter)
	{
		switch (parameter.kind)
		{
		case AnnotationParameterKind::String: return "a string";
		case AnnotationParameterKind::Type: return "a type";
		case AnnotationParameterKind::Enum: return Article(QualifiedName(parameter.type)) + " value";
		default: return Article(DisplayName(parameter.type));
		}
	}

	Core::String DescribeArgument(const ReflectedArgument& argument)
	{
		if (argument.type) return "'" + QualifiedName(argument.type) + "'";
		if (argument.value.kind == ConstantValue::Kind::String) return Quoted(argument.value.stringValue);
		return ConstantText(argument.value);
	}

	Core::String ArgumentKindNoun(const ReflectedArgument& argument)
	{
		if (argument.type) return "a type";
		switch (argument.value.kind)
		{
		case ConstantValue::Kind::Bool: return "a bool";
		case ConstantValue::Kind::Int64: return "an integer";
		case ConstantValue::Kind::Float64: return "a number with a fraction";
		case ConstantValue::Kind::String: return "a string";
		default: return "no constant";
		}
	}
} // namespace Scripting
