#pragma once
#include "Backend/HIR/ModelQueries.h"
#include "Model/LifecycleMethods.h"
#include <vector>

namespace Scripting
{
	/// What a type needs to come into existence, be copied and go away (docs/language/06): read from its own
	/// lifecycle methods, its base and its fields. A type that needs none of it is trivial - zeroed storage
	/// is a valid object and bytes copy, assign and relocate it.
	namespace LifecycleFacts
	{
		/// The lifecycle methods a type declares itself, as the model remembers them (ProgramModel::LifecycleMethodsOf).
		using OwnMethods = LifecycleMethods;
		const OwnMethods& Own(const ModelQueries& q, const ObjectType* type);

		/// The class base (the one non-interface base), or null.
		const ObjectType* ClassBase(const ObjectType* type);

		bool NeedsConstruct(const ModelQueries& q, const Type* t);
		bool NeedsDestroy(const ModelQueries& q, const Type* t);
		bool NeedsCopy(const ModelQueries& q, const Type* t);

		/// A copy of a type that destroys something but declares no copy is a second owner of the same
		/// resource: it is an error with the fix (add a copy operator, or relocate with take()).
		QueryResult<void> CheckCopyable(const ModelQueries& q, const Type* t);

		/// Calls `visit` for each non-static field of `type`, in declaration order. Anonymous-union members
		/// are skipped: a union holds no lifecycle.
		void ForEachField(const ObjectType* type, const std::function<void(const ObjectType::FieldInfo&)>& visit);

		/// Calls `visit` with the first member of each anonymous-union region of `type`, in declaration order: the
		/// region is that member's offset less its m_UnionIntraOffset, m_UnionRegionSize bytes long.
		void ForEachUnionRegion(const ObjectType* type, const std::function<void(const ObjectType::FieldInfo&)>& visit);

		/// Makes the compiler-written create, delete and copy of every object type from the model's `from`th on that
		/// needs one and declares none (what Lifecycle::DefaultCreate, Destroy and Copy would make on first use), in the
		/// model's order of types, so lowerings on several threads find them made; returns how many types there are.
		/// One that nothing calls is never emitted, and one a type needs later is made on first use.
		uint32 MakeCompilerWritten(ProgramModel& model, uint32 from);
	}
} // namespace Scripting
