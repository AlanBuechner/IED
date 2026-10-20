#pragma once
#include <windows.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace C4DAP::Target
{
	/// A type in a module's debug information.
	struct TypeRef
	{
		uint64_t module = 0;
		uint32_t id = 0;
		bool Valid() const { return module != 0; }
	};

	enum class TypeShape { Void, Basic, Pointer, Reference, Struct, Union, Enum, Array, Function, Unknown };
	enum class BasicKind { Signed, Unsigned, Float, Bool, Char8, Char16, Char32, Other };

	struct TypeFacts
	{
		TypeShape shape = TypeShape::Unknown;
		std::string name;   // a named type's name ("Point", "Ns::Mode", an alias); empty for the others
		uint64_t size = 0;
		BasicKind basic = BasicKind::Other;
		TypeRef element;    // a pointer's or reference's target, an array's element, an enum's underlying type
		uint64_t count = 0; // an array's elements
	};

	struct MemberFacts
	{
		std::string name;
		TypeRef type;
		uint64_t offset = 0;
		bool isBase = false;
	};

	struct Enumerator
	{
		std::string name;
		int64_t value = 0;
	};

	struct SourcePosition
	{
		std::string path;
		uint32_t line = 0;
		bool Known() const { return line != 0; }
	};

	struct FunctionFacts
	{
		std::string name;
		uint64_t start = 0;
		uint64_t size = 0;
	};

	/// Where a variable's storage is, as its symbol says.
	struct SymbolStorage
	{
		enum class Kind { Address, RegisterRelative, FrameRelative, Register };
		Kind kind = Kind::Address;
		uint32_t reg = 0;   // a CodeView register (CV_AMD64_*)
		int64_t offset = 0; // the address itself, or the offset from the register or the frame
	};

	struct VariableSymbol
	{
		std::string name;
		TypeRef type;
		SymbolStorage storage;
		bool parameter = false;
	};

	/// DbgHelp for one debuggee: its modules, lines, functions, variables and types. DbgHelp is single-threaded,
	/// so only the engine's thread uses it.
	class Symbols
	{
	public:
		bool Open(HANDLE process);
		void Close();
		void ModuleLoaded(HANDLE file, uint64_t base);
		void ModuleUnloaded(uint64_t base);

		/// Every address of `line` in `path` (each template instance, each piece of a `for` header). When the line
		/// has no code, those of the next line that has some; `bound` is the line they belong to (0: none).
		std::vector<uint64_t> AddressesOfLine(const std::string& path, uint32_t line, uint32_t& bound) const;
		SourcePosition PositionOf(uint64_t address) const;
		/// `address` when a line's code starts there, else where the next line's code starts. Code with no line of
		/// its own (a catch funclet's prologue) belongs to the line before it in the line table, which is not its.
		uint64_t NextStatementFrom(uint64_t address) const;
		std::optional<FunctionFacts> FunctionOf(uint64_t address) const;
		/// The entry addresses of the functions called `name` that have source lines.
		std::vector<uint64_t> FunctionsNamed(const std::string& name) const;
		/// A function the program links without debug information (the runtime's), by its public symbol.
		std::optional<uint64_t> PublicFunction(const std::string& name) const;
		/// The parameters and locals in scope at `address`, parameters first.
		std::vector<VariableSymbol> VariablesAt(uint64_t address) const;
		std::optional<VariableSymbol> Global(const std::string& name) const;

		TypeFacts Describe(TypeRef type) const;
		std::vector<MemberFacts> Members(TypeRef type) const;
		std::vector<Enumerator> Enumerators(TypeRef type) const;

	private:
		std::vector<uint64_t> ExactLine(const std::wstring& path, uint32_t line) const;
		TypeRef WithoutAliases(TypeRef type) const;
		uint32_t Tag(TypeRef type) const;
		std::vector<uint32_t> Children(TypeRef type) const;

		HANDLE m_Process = nullptr;
		std::vector<uint64_t> m_Modules; // a source-line search looks in each by its base
	};
} // namespace C4DAP::Target
