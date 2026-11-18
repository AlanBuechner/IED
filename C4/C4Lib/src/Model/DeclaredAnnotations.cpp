#include "Model/DeclaredAnnotations.h"
#include "Model/AnnotationArguments.h"
#include "Model/Annotations.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/PointerType.h"
#include "Model/Types/QualifiedName.h"
#include "Parser/NearNames.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"

namespace Scripting
{
	namespace
	{
		constexpr const char* kParameterRule = "an annotation parameter is a bool, an integer, a float or double, a const char*, an enum or typename";

		const AnnotationItem* DeclarationItem(const AstNode* declaration)
		{
			const Core::Vector<Ref<AnnotationNode>>* annotations = DeclarationAnnotations(declaration);
			if (!annotations) return nullptr;
			for (const auto& node : *annotations)
				if (node)
					for (const auto& group : node->GetBracketGroups())
						for (const auto& item : group.annotations)
							if (item && item->GetName() == kAnnotationDeclaration) return item.Get();
			return nullptr;
		}

		// `typename` alone, the way a type argument's parameter is written.
		bool IsTypename(const QualTypeNode& type)
		{
			const auto& name = type.BaseName();
			return name && name->SegmentCount() == 1 && name->GetSegments()[0].nameToken.GetText() == "typename" && !name->GetSegments()[0].templateArgs &&
				type.IndirectionCount() == 0 && !type.IsLeadingConst();
		}

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

	bool DeclaresAnnotation(const AstNode* declaration) { return DeclarationItem(declaration) != nullptr; }

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
	{
		for (const AnnotationDeclarationSite& site : sites)
			if (site.declaration) Declare(model, site);
	}

	void DeclaredAnnotations::Declare(const ProgramModel& model, const AnnotationDeclarationSite& site)
	{
		const FunctionDefinitionNode& declaration = *site.declaration;
		auto signature = std::make_unique<AnnotationSignature>();
		signature->declaration = &declaration;
		signature->name = declaration.GetName()->GetSegments()[0].nameToken.GetText();
		signature->qualifiedName = QualifiedNameOf(site.scope, signature->name);
		if (IsCompilerAnnotationName(signature->name))
		{
			Problem(declaration.GetName()->startLocation, "'" + signature->name + "' is the compiler's own annotation (docs/language/12 section 2): it cannot be declared");
			return;
		}
		if (!ReadPlaces(declaration, *signature) || !ReadParameters(model, site, *signature)) return;

		Overloads& overloads = m_ByName[signature->name.c_str()];
		for (const AnnotationSignature* earlier : overloads)
			if (SameParameters(*earlier, *signature))
			{
				Problem(declaration.GetName()->startLocation, "[" + signature->name + "] is declared twice with the same parameters: " +
					earlier->qualifiedName + " already declares " + earlier->ToString());
				return;
			}
		overloads.Push(signature.get());
		m_Signatures.push_back(std::move(signature));
	}

	bool DeclaredAnnotations::ReadPlaces(const FunctionDefinitionNode& declaration, AnnotationSignature& signature)
	{
		const AnnotationItem* item = DeclarationItem(&declaration);
		if (!item || item->GetArguments().Empty()) return true;
		signature.places = AnnotationPlaces::None();
		for (const auto& argument : item->GetArguments())
		{
			const auto* literal = dynamic_cast<const LiteralNode*>(argument.Get());
			if (!literal) return false; // the parser said so: a compiler annotation takes string literals
			AnnotationPlace place;
			if (!ParseAnnotationPlace(literal->GetValue(), place))
			{
				Problem(argument->startLocation, "'" + Core::String(literal->GetValue()) + "' is no place an annotation goes: one of " + AnnotationPlaceNames());
				return false;
			}
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
		if (site.declaration->IsVariadic())
			return Problem(site.declaration->GetCloseParenToken().GetStartLocation(),
				"an annotation takes no C '...': name what it takes as a pack (`typename... more`, `const char*... names`)");
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
		if (IsTypename(*type))
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

	const DeclaredAnnotations::Overloads* DeclaredAnnotations::Find(const std::string& name) const
	{
		auto found = m_ByName.find(OwnName(name));
		return found == m_ByName.end() ? nullptr : &found->second;
	}

	const DeclaredAnnotations::Overloads* DeclaredAnnotations::MisspeltBy(const std::string& name) const
	{
		const std::string own = OwnName(name);
		if (m_ByName.count(own)) return nullptr;
		const Overloads* closest = nullptr;
		std::string closestName;
		size_t closestEdits = 0;
		for (const auto& [declared, overloads] : m_ByName)
		{
			if (!IsNearMiss(own, declared)) continue;
			const size_t edits = EditDistance(own, declared);
			if (closest && (edits > closestEdits || (edits == closestEdits && declared > closestName))) continue; // the same answer in any map order
			closest = &overloads;
			closestName = declared;
			closestEdits = edits;
		}
		return closest;
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
