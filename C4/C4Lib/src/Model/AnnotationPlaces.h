#pragma once
#include "Core/Core.h"
#include <span>
#include <string_view>

namespace Scripting
{
	struct FunctionRecord;

	/// Where an annotation is written: what `annotation Range(...) on field, ...;` names (docs/language/12 section 4.1).
	enum class AnnotationPlace : uint8 { Type, Enum, Union, Field, Method, Function, Parameter, Variable };

	/// A set of places, one bit each.
	class AnnotationPlaces
	{
	public:
		static AnnotationPlaces Everywhere() { return AnnotationPlaces(kAll); }
		static AnnotationPlaces None() { return AnnotationPlaces(0); }

		void Add(AnnotationPlace place) { m_Bits |= Bit(place); }
		void Add(AnnotationPlaces places) { m_Bits |= places.m_Bits; }
		bool Has(AnnotationPlace place) const { return (m_Bits & Bit(place)) != 0; }
		bool IsEverywhere() const { return m_Bits == kAll; }
		bool IsEmpty() const { return m_Bits == 0; }

		/// `a field or a parameter`, as an error says where an annotation goes.
		Core::String Describe() const;

	private:
		static constexpr uint16 kAll = 0xFF;
		explicit AnnotationPlaces(uint16 bits) : m_Bits(bits) {}
		static uint16 Bit(AnnotationPlace place) { return uint16(1u << uint8(place)); }

		uint16 m_Bits;
	};

	/// The place's name as a declaration writes it after `on` (`field`).
	const char* AnnotationPlaceName(AnnotationPlace place);
	/// `a field`, as an error names a place.
	const char* AnnotationPlaceNoun(AnnotationPlace place);
	/// The place `name` writes; false when it is none.
	bool ParseAnnotationPlace(std::string_view name, AnnotationPlace& out);
	/// Every place's name, `type, enum, ...`, for an error about an unknown one.
	Core::String AnnotationPlaceNames();
	/// Every place, in the order the docs list them.
	std::span<const AnnotationPlace> AllAnnotationPlaces();

	/// A method (in a type, static or not, or an extension method) or a free function.
	AnnotationPlace PlaceOfFunction(const FunctionRecord& function);
} // namespace Scripting
