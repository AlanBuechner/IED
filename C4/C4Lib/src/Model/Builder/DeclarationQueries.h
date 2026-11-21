#pragma once
#include "Model/Builder/ModelBuilder.h"
#include <unordered_map>
#include <unordered_set>

namespace Scripting
{
	/// Pass 2 of the model build (docs/compiler/02-program-model.md, Build passes): every declaration pass 1 left pending
	/// is a query, asked at most once and answered from its inputs alone (the declaration's node, its unit and its
	/// scope). Resolving a declaration asks for what it names first - an alias's target, an enum's values, a
	/// variable's type, an object's parts - so nothing waits for a later round, and one asked for while it is
	/// resolving depends on itself: the answer is CyclicDependency with the cycle. A failure is final and kept.
	///
	/// While one of these lives it is the model's pending declarations (ProgramModel::Pending), which is how the
	/// type resolver, the constant evaluator and the name queries ask.
	class DeclarationQueries
	{
	public:
		DeclarationQueries(ModelBuilder& builder, ModelBuilder::Context& ctx);
		~DeclarationQueries();
		DeclarationQueries(const DeclarationQueries&) = delete;
		DeclarationQueries& operator=(const DeclarationQueries&) = delete;

		// Each resolves the declaration unless it was asked for before; a failure says why it does not resolve.
		QueryResult<void> Alias(const AliasType& alias);
		QueryResult<void> Enum(const EnumType& type);
		QueryResult<void> Union(const UnionType& type);
		/// A global's or a static data member's type.
		QueryResult<void> Variable(const SymbolInfo& variable);
		/// An object's fields, lane views and bases, attached in declaration order.
		QueryResult<void> Parts(const ObjectType& object);
		/// The methods an object declares in its body.
		QueryResult<void> Methods(const ObjectType& object);
		/// Everything a value of `type` holds - its elements, its bases, its fields' objects, its enums' underlying
		/// types - so it can be measured (sizeof, alignof, offsetof).
		QueryResult<void> Complete(const Type* type);
		/// The free functions named `name` (`Square`, or `Ops::Twice` by its last part), resolved before their turn: a
		/// constant in an enum value or an array extent that calls a `[compile_time]` function finds its records (D-91).
		/// The answer is the first cycle through one of them (the constant needs a function whose signature needs the
		/// constant), or, when none of them resolves, the first failure; a function's failure is also said where it is
		/// declared.
		QueryResult<void> Functions(const Core::String& name);

		/// Asks for every pending declaration, in the order pass 1 registered them.
		void ResolveAll();

		/// A link in the chain of what is resolving that is no pending declaration (a constant's initializer
		/// folding), so a cycle through it names it.
		class Step
		{
		public:
			Step(DeclarationQueries* queries, const void* key, const Core::String& name);
			~Step();
			Step(const Step&) = delete;
			Step& operator=(const Step&) = delete;
		private:
			DeclarationQueries* m_Queries;
		};

	private:
		using Resolution = ModelBuilder::Resolution;
		using Stage = ModelBuilder::Stage;

		// One link of the chain of what is resolving: `what` reads "the alias" in "the alias 'A' depends on itself".
		struct Link
		{
			const void* key = nullptr;
			const char* what = "";
			Core::String name;
		};

		// The pending work of one object, as places in the builder's lists (declaration order).
		struct ObjectWork
		{
			Core::Vector<uint32> fields;
			Core::Vector<uint32> swizzles;
			Core::Vector<uint32> bases;
			Core::Vector<uint32> methods;
			Resolution parts;
			Resolution declaredMethods;
		};

		void Index();
		template <typename Resolve>
		QueryResult<void> Ask(Resolution& resolution, const Link& link, Resolve resolve);
		QueryResult<void> DependsOnItself(const Link& link);
		QueryResult<void> FailureOf(const Resolution& resolution, const Link& link);

		QueryResult<void> Field(uint32 index);
		QueryResult<void> Method(uint32 index);
		QueryResult<void> ResolveParts(ObjectWork& work);
		QueryResult<void> ResolveMethods(ObjectWork& work);
		QueryResult<void> CompleteValue(const Type* type, std::unordered_set<const Type*>& visited);

		ModelBuilder& m_Builder;
		ModelBuilder::Context& m_Ctx;
		Core::Vector<Link> m_Resolving;
		Core::String m_LastCycle;
		std::unordered_map<const AliasType*, uint32> m_Aliases;
		std::unordered_map<const EnumType*, uint32> m_Enums;
		std::unordered_map<const UnionType*, uint32> m_Unions;
		std::unordered_map<const SymbolInfo*, uint32> m_Variables;
		std::unordered_map<const SymbolInfo*, uint32> m_StaticMembers; // a static data member's symbol -> its field
		std::unordered_map<const ObjectType*, ObjectWork> m_Objects;
	};
} // namespace Scripting
