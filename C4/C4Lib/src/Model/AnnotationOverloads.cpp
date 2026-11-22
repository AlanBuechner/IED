#include "Model/AnnotationOverloads.h"
#include "Model/AnnotationArguments.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include <tuple>

namespace C4Compiler
{
	namespace
	{
		using MatchResult = QueryResult<AnnotationMatch>;

		struct Attempt
		{
			const AnnotationSignature* signature = nullptr;
			bool fits = false;
			uint32 conversions = 0;
			Core::Vector<ReflectedArgument> arguments;
			Core::String why;
			SourceLocation at;

			std::tuple<uint32, bool> Rank() const { return { conversions, signature->HasPack() }; }
		};

		Core::String Plural(uint32 count, const char* noun) { return std::to_string(count) + " " + noun + (count == 1 ? "" : "s"); }

		Core::String ParameterNames(const AnnotationSignature& signature)
		{
			Core::String names;
			for (const AnnotationParameter& parameter : signature.parameters)
				names += (names.empty() ? "" : ", ") + (parameter.name.empty() ? parameter.ToString() : parameter.name) + (parameter.isPack ? "..." : "");
			return names;
		}

		Core::String CountWhy(const AnnotationSignature& signature, uint32 written)
		{
			const uint32 required = signature.RequiredCount();
			const uint32 fixed = signature.FixedCount();
			if (fixed == 0 && !signature.HasPack()) return "takes no arguments, not " + std::to_string(written);
			Core::String takes;
			if (signature.HasPack()) takes = "takes at least " + Plural(required, "argument");
			else if (required == fixed) takes = "takes " + Plural(fixed, "argument");
			else takes = "takes " + std::to_string(required) + " to " + Plural(fixed, "argument");
			return takes + " (" + ParameterNames(signature) + "), not " + std::to_string(written);
		}

		Core::String ParameterWhy(const AnnotationParameter& parameter, uint32 index, const Core::String& why)
		{
			const Core::String named = parameter.name.empty() ? Core::String() : " ('" + parameter.name + "')";
			return "argument " + std::to_string(index + 1) + named + " is " + DescribeParameter(parameter) + ": " + why;
		}

		Attempt Try(const AnnotationSignature& signature, const AnnotationItem& item, const Core::Vector<ReflectedArgument>& folded)
		{
			Attempt attempt;
			attempt.signature = &signature;
			attempt.at = item.startLocation;
			const uint32 written = folded.Count();
			if (written < signature.RequiredCount() || (!signature.HasPack() && written > signature.parameters.Count()))
			{
				attempt.why = CountWhy(signature, written);
				return attempt;
			}
			for (uint32 i = 0; i < written; ++i)
			{
				const AstNode* argument = i < item.GetArguments().Count() ? item.GetArguments()[i].Get() : nullptr;
				const AnnotationParameter& parameter = signature.ParameterFor(i);
				const ArgumentFit fit = FitArgument(folded[i], argument, parameter);
				if (!fit.fits)
				{
					attempt.why = ParameterWhy(parameter, i, fit.why);
					if (argument) attempt.at = argument->startLocation;
					return attempt;
				}
				attempt.conversions += fit.converted ? 1 : 0;
				attempt.arguments.Push(fit.value);
			}
			for (uint32 i = written; i < signature.FixedCount(); ++i) attempt.arguments.Push(signature.parameters[i].defaultValue);
			attempt.fits = true;
			return attempt;
		}

		bool TakesCount(const AnnotationSignature& signature, uint32 arguments)
		{
			return arguments >= signature.RequiredCount() && (signature.HasPack() || arguments <= signature.parameters.Count());
		}

		// Whether a literal written as an argument could be one `parameter` takes; anything else could be.
		bool LiteralFits(const AstNode* written, const AnnotationParameter& parameter)
		{
			const auto* literal = dynamic_cast<const LiteralNode*>(written);
			if (!literal) return true;
			switch (literal->GetKind())
			{
			case LiteralNode::LiteralKind::String: return parameter.kind == AnnotationParameterKind::String;
			case LiteralNode::LiteralKind::Boolean: return parameter.kind == AnnotationParameterKind::Bool;
			case LiteralNode::LiteralKind::Number:
				return parameter.kind == AnnotationParameterKind::Integer || parameter.kind == AnnotationParameterKind::Float;
			default: return true;
			}
		}

		MatchResult Matched(Attempt& attempt)
		{
			AnnotationMatch match;
			match.signature = attempt.signature;
			match.arguments = std::move(attempt.arguments);
			return MatchResult::Success(std::move(match));
		}

		MatchResult NoneFits(const AnnotationItem& item, const Core::Vector<Attempt>& attempts)
		{
			const Core::String name = "[" + item.GetOwnName() + "]";
			if (attempts.Count() == 1) return MatchResult::Fail(QueryError::NotAValue, name + " " + attempts[0].why, attempts[0].at);
			Core::String tried;
			for (const Attempt& attempt : attempts) tried += (tried.empty() ? "" : "; ") + attempt.signature->ToString() + " " + attempt.why;
			return MatchResult::Fail(QueryError::NotAValue, "no " + name + " takes these arguments (" + tried + ")", item.startLocation);
		}

		MatchResult Ambiguous(const AnnotationItem& item, const Core::Vector<const Attempt*>& tied)
		{
			Core::String between;
			for (uint32 i = 0; i < tied.Count(); ++i) between += Core::String(i == 0 ? "" : i + 1 == tied.Count() ? " and " : ", ") + tied[i]->signature->ToString();
			return MatchResult::Fail(QueryError::Ambiguous, "[" + item.GetOwnName() + "] with these arguments is ambiguous: " + between + " each take them",
				item.startLocation);
		}
	}

	MatchResult ChooseAnnotationOverload(const DeclaredAnnotations::Overloads& overloads, const AnnotationItem& item, AnnotationPlace place,
		const Core::Vector<ReflectedArgument>& folded)
	{
		AnnotationPlaces places = AnnotationPlaces::None();
		Core::Vector<Attempt> attempts;
		for (const AnnotationSignature* signature : overloads)
		{
			places.Add(signature->places);
			if (signature->places.Has(place)) attempts.Push(Try(*signature, item, folded));
		}
		if (attempts.Empty())
			return MatchResult::Fail(QueryError::NotAValue, "[" + item.GetOwnName() + "] goes on " + places.Describe() + ", not on " + AnnotationPlaceNoun(place),
				item.startLocation);

		Attempt* best = nullptr;
		for (Attempt& attempt : attempts)
			if (attempt.fits && (!best || attempt.Rank() < best->Rank())) best = &attempt;
		if (!best) return NoneFits(item, attempts);

		Core::Vector<const Attempt*> tied;
		for (const Attempt& attempt : attempts)
			if (attempt.fits && attempt.Rank() == best->Rank()) tied.Push(&attempt);
		if (tied.Count() > 1) return Ambiguous(item, tied);
		return Matched(*best);
	}

	const AnnotationSignature* LikelyOverload(const DeclaredAnnotations::Overloads& overloads, uint32 arguments)
	{
		for (const AnnotationSignature* signature : overloads)
			if (TakesCount(*signature, arguments)) return signature;
		return overloads.Empty() ? nullptr : overloads[0];
	}

	const AnnotationSignature* LikelyOverload(const DeclaredAnnotations::Overloads& overloads, const AnnotationItem& item)
	{
		const auto& arguments = item.GetArguments();
		for (const AnnotationSignature* signature : overloads)
		{
			if (!TakesCount(*signature, arguments.Count())) continue;
			bool fits = true;
			for (uint32 i = 0; i < arguments.Count() && fits; ++i) fits = LiteralFits(arguments[i].Get(), signature->ParameterFor(i));
			if (fits) return signature;
		}
		return LikelyOverload(overloads, arguments.Count());
	}
} // namespace C4Compiler
