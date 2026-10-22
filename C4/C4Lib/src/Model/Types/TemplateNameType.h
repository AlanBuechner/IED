#pragma once
#include "Model/Types/Type.h"
#include "Model/QueryResult.h"
#include <functional>

namespace Scripting
{
	struct FunctionRecord;
	class SignatureType;

	/// The type of a function template's name passed as an argument without its template arguments
	/// (`Apply(Identity, 5)`, D-17): it binds only to a funcptr or func parameter, whose signature deduces them,
	/// as the address of that instance. Overload resolution asks `Instance` whether a parameter fits.
	class TemplateNameType : public Type
	{
	public:
		static constexpr TypeKind kTypeKind = TypeKind::TemplateName;
		using InstanceFor = std::function<QueryResult<const FunctionRecord*>(const SignatureType& target)>;

		TemplateNameType(const Core::String& name, InstanceFor instanceFor)
			: Type(TypeKind::TemplateName), m_Name(name), m_InstanceFor(std::move(instanceFor)) {}

		Core::String GetName() const override { return "template " + m_Name; }
		/// The instance whose signature is exactly `target`'s.
		QueryResult<const FunctionRecord*> Instance(const SignatureType& target) const { return m_InstanceFor(target); }

	private:
		Core::String m_Name;
		InstanceFor m_InstanceFor;
	};
} // namespace Scripting
