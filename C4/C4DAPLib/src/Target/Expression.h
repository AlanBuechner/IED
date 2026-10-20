#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace C4DAP::Target
{
	/// One step from a value to another: `.field`, `->field` or `[index]`.
	struct PathStep
	{
		enum class Kind { Field, PointerField, Index };
		Kind kind = Kind::Field;
		std::string field;
		uint64_t index = 0;
	};

	/// What `evaluate` reads (docs/tools/03-editor-lsp-dap.md): a variable (`Ns::name` too), then any chain of
	/// steps, under any number of leading `*`.
	struct Expression
	{
		uint32_t dereferences = 0;
		std::string variable;
		std::vector<PathStep> path;
	};

	/// The expression `text` spells, or the reason it is not one.
	std::optional<Expression> ParseExpression(const std::string& text, std::string& error);
} // namespace C4DAP::Target
