#pragma once
#include "Model/ConstantValue.h"
#include "Model/QueryResult.h"
#include "Core/Core.h"

namespace Scripting
{
	class ProgramModel;
	class Type;
	struct Scope;
	struct AstNode;
	struct QualTypeNode;
	struct TemplateDefinitionNode;
	struct TemplateParameterNode;
	struct TemplateArgumentListNode;

	/// One argument of a template (docs/language/08): a type, or a compile-time value. Unset while a
	/// deduction has not decided it.
	struct TemplateArg
	{
		const Type* type = nullptr;
		ConstantValue value;
		bool IsType() const { return type != nullptr; }
		bool IsSet() const { return type != nullptr || !value.IsNone(); }
		bool operator==(const TemplateArg& other) const;
		Core::String ToString() const;
	};

	/// Arguments in parameter order; entry i belongs to parameter i. A parameter pack (`...Ts`, D-19) is the
	/// last parameter and takes every entry past the fixed ones.
	using TemplateArgs = Core::Vector<TemplateArg>;

	struct InstantiationBindings;
	struct SymbolInfo;

	/// True for a parameter that takes a type (`T`, `class T`), false for one that takes a value (`int32 N`).
	bool TakesType(const TemplateParameterNode& param);
	/// The index of a template's parameter pack, or -1 (D-19).
	int PackIndex(const TemplateDefinitionNode& params);
	/// The number of parameters before the pack: all of them when there is none.
	uint32 FixedCount(const TemplateDefinitionNode& params);
	/// The bindings a body made from the template lowers with: each parameter, a pack with all its elements.
	InstantiationBindings BindingsFor(const ProgramModel& model, const TemplateDefinitionNode& params, const TemplateArgs& args);

	/// The cache identity of an argument list: the resolved types and values, never their spelling, so two
	/// spellings of one instantiation share it.
	Core::String ArgumentsKey(const TemplateArgs& args);

	/// Turns what a use writes into the arguments of a template, in parameter order (BUG-066: by position,
	/// never "types first, then values").
	class TemplateArgumentResolver
	{
	public:
		/// The arguments written at a use (`Box<int32, 4>`): a type for a type parameter; for a value
		/// parameter any constant expression, or a name that folds to one. Names resolve in `lookup` first
		/// (an enclosing instantiation's bindings), then from `context` - the scope the use is written in -
		/// outward. Unwritten trailing entries stay unset.
		static QueryResult<TemplateArgs> Explicit(const ProgramModel& model, const TemplateDefinitionNode& params,
			const TemplateArgumentListNode* written, uint32 unitId, Scope* lookup, const Scope* context, const Core::String& templateName);

		/// Fills the entries `given` leaves unset with the parameters' defaults and binds every parameter in
		/// `binding` (T := type, N := value), in order, so a default may name an earlier parameter. A default
		/// resolves in `binding`, which sits on the template's declaring scope. Fails naming the first parameter
		/// that gets no argument.
		static QueryResult<TemplateArgs> CompleteAndBind(const ProgramModel& model, const TemplateDefinitionNode& params,
			TemplateArgs given, uint32 unitId, Scope* binding, const Core::String& templateName);
		/// Whether `given` writes every parameter before the pack, so CompleteAndBind would return it as it is.
		static bool Complete(const TemplateDefinitionNode& params, const TemplateArgs& given);

		/// Registers `param := arg` in `binding`: a type alias, or a named constant.
		static void Bind(Scope* binding, const TemplateParameterNode& param, const TemplateArg& arg);
		/// Binds every parameter: the fixed ones to their arguments, a pack to all the arguments past them.
		static void BindAll(const ProgramModel& model, Scope* binding, const TemplateDefinitionNode& params, const TemplateArgs& args);
		/// What one argument means where it is named: a type, or a named constant.
		static SymbolInfo SymbolOf(const TemplateArg& arg);
		/// A pack of the arguments from `from` on, kept by the model.
		static const std::vector<SymbolInfo>* PackOf(const ProgramModel& model, const TemplateArgs& args, uint32 from);
		/// An error when a template has a pack that is not its last parameter, or more than one.
		static QueryResult<void> CheckPack(const TemplateDefinitionNode& params, const Core::String& templateName);
		/// Registers the pack parameter `pack` as all of `args` from `from` on.
		static void BindPack(const ProgramModel& model, Scope* binding, const TemplateParameterNode& pack, const TemplateArgs& args, uint32 from);
		/// The symbol a pack parameter's name means: the arguments from `from` on are its elements.
		static SymbolInfo PackSymbol(const ProgramModel& model, const TemplateParameterNode& pack, const TemplateArgs& args, uint32 from);

		/// What `written` denotes as a type (`takesType`) or as a constant value, resolved in `lookup`, then from
		/// `context` (null: from `lookup`'s own chain).
		static QueryResult<TemplateArg> ResolveWritten(const ProgramModel& model, bool takesType, const AstNode* written, uint32 unitId,
			Scope* lookup, const Scope* context);

	private:
		static QueryResult<TemplateArg> Resolve(const ProgramModel& model, const TemplateParameterNode& param,
			const AstNode* written, uint32 unitId, Scope* lookup, const Scope* context, const Core::String& templateName);
		static QueryResult<TemplateArg> ResolveValue(const ProgramModel& model, const AstNode* written, uint32 unitId, Scope* lookup,
			const Scope* context);
	};
} // namespace Scripting
