#pragma once
#include "Backend/HIR/LoweringContext.h"

namespace Scripting
{
	/// Member visibility at a use site (D-28, docs/language/05 5.7). The code asking is the function being
	/// lowered: a method sees the private members of its own type and of every type it is nested in, and the
	/// protected members of those types' bases. Free functions and extension methods are outside code.
	namespace AccessCheck
	{
		enum class MemberKind { Field, Method, Constructor };

		struct Member
		{
			const ObjectType* owner = nullptr; // the type that declares the member
			AccessModifier access = AccessModifier::Public;
			MemberKind kind = MemberKind::Field;
			Core::String name;
		};

		QueryResult<void> Check(const LoweringContext& ctx, const Member& member);

		/// A call of `callee`: its declaring type's access rule, when it is a member.
		QueryResult<void> CheckCall(const LoweringContext& ctx, const FunctionRecord& callee);

		/// A use of `field`, declared by `owner`.
		QueryResult<void> CheckField(const LoweringContext& ctx, const ObjectType* owner, const ObjectType::FieldInfo& field);

		/// Default-constructing a `type` (an array's elements): the access of the create that runs.
		QueryResult<void> CheckDefaultCreate(LoweringContext& ctx, const Type* type);
	}
} // namespace Scripting
