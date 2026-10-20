#pragma once
#include "Model/ProgramModel.h"

namespace Scripting
{
	/// Whether an object of a type can exist on its own (D-29). An `abstract` type, an interface, or a type with
	/// a virtual method nothing implements is only ever a base: it is used through pointers and references.
	namespace Abstractness
	{
		/// Why no object of `type` can be created, or empty when one can.
		Core::String WhyAbstract(const ProgramModel& model, const ObjectType* type);

		/// The same question for a value of `type` (a local, a field, a by-value parameter): an array's element
		/// type counts; a pointer or a reference is not an object of its pointee.
		Core::String WhyNoValue(const ProgramModel& model, const Type* type);

		/// `what` (a local, a field, a parameter) cannot be a value of its abstract type: the message, or empty.
		Core::String ByValue(const ProgramModel& model, const Core::String& what, const Type* type);

		/// A by-value parameter or return value of `fn` whose type is abstract: the message, or empty.
		Core::String Signature(const ProgramModel& model, const FunctionRecord& fn);

		/// Creating an object of `type` (`T(...)`, `new T`): the message when it is abstract, or empty.
		Core::String Creation(const ProgramModel& model, const Type* type);
	}
} // namespace Scripting
