#include "Model/AnnotationArguments.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/QualifiedName.h"
#include "Model/Types/Type.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include <limits>

namespace C4Compiler
{
	namespace
	{
		ArgumentFit Fits(ReflectedArgument value, bool converted = false)
		{
			ArgumentFit fit;
			fit.fits = true;
			fit.converted = converted;
			fit.value = std::move(value);
			return fit;
		}

		ArgumentFit Refused(const Core::String& why)
		{
			ArgumentFit fit;
			fit.why = why;
			return fit;
		}

		ArgumentFit WrongKind(const ReflectedArgument& argument) { return Refused(DescribeArgument(argument) + " is " + ArgumentKindNoun(argument)); }

		ReflectedArgument Constant(const ConstantValue& value)
		{
			ReflectedArgument argument;
			argument.value = value;
			return argument;
		}

		bool Holds(TypeKind kind, int64 value)
		{
			switch (kind)
			{
			case TypeKind::Int8: return value >= std::numeric_limits<int8>::min() && value <= std::numeric_limits<int8>::max();
			case TypeKind::Int16: return value >= std::numeric_limits<int16>::min() && value <= std::numeric_limits<int16>::max();
			case TypeKind::Int32: return value >= std::numeric_limits<int32>::min() && value <= std::numeric_limits<int32>::max();
			case TypeKind::UInt8: return value >= 0 && value <= std::numeric_limits<uint8>::max();
			case TypeKind::UInt16: return value >= 0 && value <= std::numeric_limits<uint16>::max();
			case TypeKind::UInt32: return value >= 0 && value <= int64(std::numeric_limits<uint32>::max());
			case TypeKind::UInt64: return value >= 0; // a folded integer is an int64
			default: return true;
			}
		}

		Core::Vector<Core::String> Segments(const Core::String& qualified)
		{
			Core::Vector<Core::String> segments;
			size_t from = 0;
			for (size_t colon = qualified.find("::"); colon != Core::String::npos; colon = qualified.find("::", from))
			{
				segments.Push(qualified.substr(from, colon - from));
				from = colon + 2;
			}
			segments.Push(qualified.substr(from));
			return segments;
		}

		Core::String Joined(const Core::Vector<Core::String>& segments)
		{
			Core::String text;
			for (const Core::String& segment : segments) text += (text.empty() ? "" : "::") + segment;
			return text;
		}

		// `Pass::Glass` or `Render::Pass::Glass` (or `Glass` of an unscoped enum) naming one of the enum's enumerators, whose
		// value the argument folded to: what is written before the enumerator ends the enum's qualified name.
		bool NamesEnumerator(const EnumType& en, const Core::Vector<Core::String>& written, int64 value)
		{
			if (written.Empty() || (written.Count() == 1 && en.IsScoped())) return false;
			const Core::Vector<Core::String> name = Segments(QualifiedName(&en));
			const uint32 qualifiers = written.Count() - 1;
			if (qualifiers > name.Count()) return false;
			for (uint32 i = 0; i < qualifiers; ++i)
				if (written[i] != name[name.Count() - qualifiers + i]) return false;
			const EnumType::MemberInfo* member = en.GetMemberByName(written.Back());
			return member && member->m_Value == value;
		}

		// `(Render::Pass::Opaque, Render::Pass::Glass, ...)`: the enumerators an error offers.
		Core::String Enumerators(const EnumType& en)
		{
			constexpr uint32 kShown = 3;
			Core::String shown;
			for (uint32 i = 0; i < en.GetMembers().Count() && i < kShown; ++i)
				shown += (shown.empty() ? "" : ", ") + QualifiedName(&en) + "::" + en.GetMembers()[i].mName;
			if (en.GetMembers().Count() > kShown) shown += ", ...";
			return shown.empty() ? shown : " (" + shown + ")";
		}

		ArgumentFit FitInteger(const ReflectedArgument& argument, const AnnotationParameter& parameter)
		{
			if (argument.value.kind != ConstantValue::Kind::Int64) return WrongKind(argument);
			if (!Holds(parameter.type->GetKind(), argument.value.intValue))
				return Refused(DescribeArgument(argument) + " does not fit in " + DescribeParameter(parameter));
			return Fits(argument);
		}

		ArgumentFit FitFloat(const ReflectedArgument& argument)
		{
			if (argument.value.kind == ConstantValue::Kind::Float64) return Fits(argument);
			if (argument.value.kind == ConstantValue::Kind::Int64) return Fits(Constant(ConstantValue::Float(double(argument.value.intValue))), true);
			return WrongKind(argument);
		}

		ArgumentFit FitEnum(const ReflectedArgument& argument, const AstNode* written, const AnnotationParameter& parameter)
		{
			const auto* en = static_cast<const EnumType*>(parameter.type);
			const Core::Vector<Core::String> name = WrittenNameSegments(written);
			if (argument.value.kind == ConstantValue::Kind::Int64 && NamesEnumerator(*en, name, argument.value.intValue))
				return Fits(argument);
			if (argument.type || argument.value.kind != ConstantValue::Kind::Int64) return WrongKind(argument);
			const Core::String said = name.Empty() ? DescribeArgument(argument) : Joined(name);
			return Refused(said + " is none of its enumerators" + Enumerators(*en));
		}
	}

	ArgumentFit FitArgument(const ReflectedArgument& argument, const AstNode* written, const AnnotationParameter& parameter)
	{
		if (parameter.kind == AnnotationParameterKind::Type) return argument.type ? Fits(argument) : WrongKind(argument);
		if (argument.type) return WrongKind(argument);
		switch (parameter.kind)
		{
		case AnnotationParameterKind::Bool: return argument.value.kind == ConstantValue::Kind::Bool ? Fits(argument) : WrongKind(argument);
		case AnnotationParameterKind::Integer: return FitInteger(argument, parameter);
		case AnnotationParameterKind::Float: return FitFloat(argument);
		case AnnotationParameterKind::String: return argument.value.kind == ConstantValue::Kind::String ? Fits(argument) : WrongKind(argument);
		case AnnotationParameterKind::Enum: return FitEnum(argument, written, parameter);
		default: return WrongKind(argument);
		}
	}

	Core::Vector<Core::String> WrittenNameSegments(const AstNode* written)
	{
		Core::Vector<Core::String> segments;
		if (!written) return segments;
		switch (written->Type())
		{
		case NodeType::IdentifierExpression:
			segments.Push(static_cast<const IdentifierExpressionNode*>(written)->GetName());
			break;
		case NodeType::StaticAccess: {
			const auto* access = static_cast<const StaticAccessNode*>(written);
			segments = WrittenNameSegments(access->GetBaseType().Get());
			if (!segments.Empty()) segments.Push(access->GetMemberNameToken().GetText());
			break;
		}
		case NodeType::NamespaceQualifiedName:
			for (const auto& segment : static_cast<const NamespaceQualifiedNameNode*>(written)->GetSegments()) segments.Push(segment.nameToken.GetText());
			break;
		case NodeType::QualType: {
			const auto* type = static_cast<const QualTypeNode*>(written);
			if (type->IndirectionCount() == 0 && !type->IsLeadingConst()) segments = WrittenNameSegments(type->BaseName().Get());
			break;
		}
		default: break;
		}
		return segments;
	}
} // namespace C4Compiler
