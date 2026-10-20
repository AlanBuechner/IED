#pragma once
#include "Model/Templates/TemplateArguments.h"
#include <vector>

namespace Scripting
{
	struct PackExpansionNode;

	/// A pack an expansion's pattern names (D-19), and what the name means in each element.
	struct NamedPack
	{
		Core::String name;
		const std::vector<SymbolInfo>* elements = nullptr;
	};

	/// Pack expansions (docs/language/08 6): `pattern...` stands for the pattern once per element of the packs
	/// it names, with each pack's name meaning its element i in the i-th copy.
	namespace Packs
	{
		/// The packs `pattern` names - looked up in `lookup` and then in the lowering scopes - skipping any
		/// expansion nested in it. An error when it names none, or packs of different lengths.
		QueryResult<std::vector<NamedPack>> In(const ProgramModel& model, const AstNode* pattern, const Scope* lookup);

		/// True when `pattern` names a pack outside any expansion nested in it.
		bool Names(const ProgramModel& model, const AstNode* pattern, const Scope* lookup);

		uint32 Length(const std::vector<NamedPack>& packs);

		/// The pack `name` means - in `lookup`, then in the lowering scopes - or null when it names no pack.
		const SymbolInfo* Find(const ProgramModel& model, const Core::String& name, const Scope* lookup);

		/// A scope under `parent` where each pack's name means its element `index`.
		Scope* ElementScope(const ProgramModel& model, const std::vector<NamedPack>& packs, uint32 index, Scope* parent);

		/// The template arguments an expansion in a template argument list stands for (`Ts...`, `Box<Ts>...`); names
		/// in the pattern resolve as Explicit's do.
		QueryResult<TemplateArgs> ExpandArguments(const ProgramModel& model, const PackExpansionNode& expansion, bool takesType,
			uint32 unitId, Scope* lookup, const Scope* context);
	}
} // namespace Scripting
