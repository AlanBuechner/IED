#pragma once
#include "Core/Core.h"
#include <string>
#include <vector>

namespace Scripting
{
	/// The rows of the reflection wire format (C4Runtime/ReflectABI.h), and a table of pointers.
	enum class ImageShape : uint8 { AnnotationArg, Annotation, Param, Field, Method, EnumValue, TypeInfo, Variable, Function, Image, Pointer };

	/// One field of a row: a number (its width is the field's, from the row's shape), a text, null, a table or a symbol.
	struct ImageValue
	{
		enum class Kind : uint8 { Integer, Real, Text, Null, Table, Symbol };
		Kind kind = Kind::Null;
		uint64 integer = 0; // the bits of an Integer; the index of a Text, Table or Symbol
		double real = 0;
	};

	using ImageRow = std::vector<ImageValue>;

	/// A table a row points at: its rows, of one shape (a Pointer table's rows have one value each).
	struct ImageTable
	{
		ImageShape shape;
		std::vector<ImageRow> rows;
	};

	/// A function or global of the program the image points at, by its name in the program.
	struct ImageSymbol
	{
		std::string name;
		bool function = false;
		bool hidden = false; // a local of a split program (`.local`)
	};

	/// The reflection image as plain values (D-82): what the model says, gathered where the model may be read, made into
	/// LLVM constants anywhere (ReflectionTables). A table only points at tables made before it. Texts and symbols are
	/// kept as they come; where the image is built, the same text or name is one constant.
	class ImageDescription
	{
	public:
		bool Empty() const { return m_Image.empty(); }

		ImageValue Integer(uint64 value) const { return { ImageValue::Kind::Integer, value }; }
		ImageValue Real(double value) const { ImageValue v; v.kind = ImageValue::Kind::Real; v.real = value; return v; }
		ImageValue Null() const { return {}; }
		ImageValue Text(std::string text);
		ImageValue Symbol(std::string name, bool function, bool hidden);
		/// A table of `rows`, or null for none.
		ImageValue Table(ImageShape shape, std::vector<ImageRow> rows);
		void SetImage(ImageRow image) { m_Image = std::move(image); }

		const std::vector<std::string>& Texts() const { return m_Texts; }
		const std::vector<ImageSymbol>& Symbols() const { return m_Symbols; }
		const std::vector<ImageTable>& Tables() const { return m_Tables; }
		const ImageRow& Image() const { return m_Image; }

	private:
		std::vector<std::string> m_Texts;
		std::vector<ImageSymbol> m_Symbols;
		std::vector<ImageTable> m_Tables;
		ImageRow m_Image; // c4_reflect_image; empty when there is nothing to register
	};
} // namespace Scripting
