#pragma once
#include "Model/Builder/ModelBuilder.h"

namespace Scripting
{
	class DeclarationQueries;

	/// How one pending declaration resolves (pass 2, docs/compiler/02-program-model.md): a variable's type, an enum's
	/// underlying type and values, an alias's target, a union's fields, and an object's fields, lane views, bases and
	/// in-class method declarations (ObjectMemberResolver.cpp). DeclarationQueries asks for each once; a failure is why
	/// the declaration does not resolve, kept for the report. Free functions are FreeFunctionResolver's.
	class ModelResolver
	{
	public:
		ModelResolver(ModelBuilder& builder, ModelBuilder::Context& ctx, DeclarationQueries& queries)
			: m_Builder(builder), m_Ctx(ctx), m_Queries(queries) {}

		QueryResult<void> ResolveVariable(ModelBuilder::PendingVar& v);
		QueryResult<void> ResolveEnum(ModelBuilder::PendingEnum& e);
		QueryResult<void> ResolveAlias(ModelBuilder::PendingAlias& a);
		QueryResult<void> ResolveUnion(ModelBuilder::PendingUnion& u);

		/// The field's type, in its class's scope; a static member's symbols take it.
		QueryResult<void> ResolveFieldType(ModelBuilder::PendingField& f);
		/// A resolved field joins its object: one field per declarator.
		void AttachField(const ModelBuilder::PendingField& f);
		/// The lane views of one object (their places in the builder's list, in order): the views of one anonymous
		/// union that share a lane type are one SIMD region. Each view's resolution says how it went.
		void ResolveLaneViews(const Core::Vector<uint32>& places);
		QueryResult<void> ResolveBase(ModelBuilder::PendingBase& b);
		QueryResult<void> ResolveMethod(ModelBuilder::PendingMethodDecl& d);

		/// The one pass that opens bodies: every template specialization a function body names in a type position is
		/// instantiated, so those types are concrete before any body is lowered. An instance's methods are bodies
		/// too, visited as they are made.
		static void InstantiateBodyReferences(ProgramModel& model);

	private:
		/// The values of `e`'s members in order, or why one does not fold.
		QueryResult<Core::Vector<int64>> MemberValues(const ModelBuilder::PendingEnum& e) const;
		void ReportMembersOutOfRange(const ModelBuilder::PendingEnum& e, const Core::Vector<int64>& values);
		static QueryResult<void> AttachBase(const ObjectType& derived, const Type* base);
		Scope* ClassScope(const ObjectType* object) const;
		/// The run of packed views starting at `places[first]` that are one region: its last place's index.
		uint32 LastViewOfRegion(const Core::Vector<uint32>& places, uint32 first, const Type* lane) const;
		QueryResult<const Type*> LaneTypeOf(const ModelBuilder::PendingSwizzle& view) const;
		void AddRegion(const Core::Vector<uint32>& places, uint32 first, uint32 last, const Type* lane);
		QueryResult<FunctionType::ParameterInfo> MethodParameter(const ModelBuilder::PendingMethodDecl& d, uint32 index, Scope* classScope) const;

		ModelBuilder& m_Builder;
		ModelBuilder::Context& m_Ctx;
		DeclarationQueries& m_Queries;
	};
} // namespace Scripting
