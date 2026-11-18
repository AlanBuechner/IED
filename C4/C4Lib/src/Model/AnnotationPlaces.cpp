#include "Model/AnnotationPlaces.h"
#include "Model/SymbolTable.h"

namespace Scripting
{
	namespace
	{
		struct PlaceText
		{
			AnnotationPlace place;
			const char* name;
			const char* noun;
		};

		constexpr PlaceText kPlaces[] = {
			{ AnnotationPlace::Type, "type", "a type" },
			{ AnnotationPlace::Enum, "enum", "an enum" },
			{ AnnotationPlace::Union, "union", "a union" },
			{ AnnotationPlace::Field, "field", "a field" },
			{ AnnotationPlace::Method, "method", "a method" },
			{ AnnotationPlace::Function, "function", "a function" },
			{ AnnotationPlace::Parameter, "parameter", "a parameter" },
			{ AnnotationPlace::Variable, "variable", "a variable" },
		};

		const PlaceText& TextOf(AnnotationPlace place) { return kPlaces[uint8(place)]; }
	}

	const char* AnnotationPlaceName(AnnotationPlace place) { return TextOf(place).name; }
	const char* AnnotationPlaceNoun(AnnotationPlace place) { return TextOf(place).noun; }

	bool ParseAnnotationPlace(std::string_view name, AnnotationPlace& out)
	{
		for (const PlaceText& text : kPlaces)
			if (name == text.name)
			{
				out = text.place;
				return true;
			}
		return false;
	}

	Core::String AnnotationPlaceNames()
	{
		Core::String names;
		for (const PlaceText& text : kPlaces) names += Core::String(names.empty() ? "" : ", ") + text.name;
		return names;
	}

	Core::String AnnotationPlaces::Describe() const
	{
		Core::Vector<const char*> nouns;
		for (const PlaceText& text : kPlaces)
			if (Has(text.place)) nouns.Push(text.noun);
		Core::String said;
		for (uint32 i = 0; i < nouns.Count(); ++i)
		{
			if (i > 0) said += i + 1 == nouns.Count() ? " or " : ", ";
			said += nouns[i];
		}
		return said;
	}

	AnnotationPlace PlaceOfFunction(const FunctionRecord& function)
	{
		const bool inType = function.scope && function.scope->GetKind() == Scope::Kind::Class;
		return function.containingType || function.isExtensionMethod || inType ? AnnotationPlace::Method : AnnotationPlace::Function;
	}
} // namespace Scripting
