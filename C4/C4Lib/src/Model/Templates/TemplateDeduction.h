#pragma once
#include "Model/Templates/TemplateArguments.h"
#include <vector>

namespace Scripting
{
	struct NamespaceQualifiedNameNode;
	struct FunPtrTypeNode;
	class ObjectType;

	/// Template argument deduction (D-17): each function parameter's type as written (`T`, `const T&`,
	/// `T*`, `T[N]`, `Box<T>`, `funcptr<R(A)>`) is matched against the call's argument type, deciding the
	/// template parameters it names. A by-value `T` takes an array argument as a pointer to its element (the
	/// C++ decay); `Box<T>` also matches an object deriving from a `Box` instance. A parameter type that
	/// names no template parameter, or one that does not fit the argument, decides nothing (overload
	/// resolution judges the conversion later). Two different answers for one parameter are an error.
	class TemplateDeduction
	{
	public:
		/// `seed` holds the arguments written explicitly at the call (a leading prefix, the rest unset); those
		/// parameters are not deduced, as in C++.
		TemplateDeduction(const ProgramModel& model, const TemplateDefinitionNode& params, TemplateArgs seed);

		QueryResult<void> FromArgument(const QualTypeNode& parameter, const Type* argument);
		/// Element `element` of a function parameter pack (`Ts... args`): its pattern against that argument (D-19).
		QueryResult<void> FromPackArgument(const QualTypeNode& parameter, const Type* argument, uint32 element);
		/// An extension method's receiver: `extends Box<T>` against the type it is called on.
		QueryResult<void> FromReceiver(const NamespaceQualifiedNameNode& extended, const Type* receiver);
		/// One argument of a specialization's pattern (`T*` in `Box<T*>`) against the argument it is for.
		QueryResult<void> FromTemplateArgument(const AstNode* pattern, const TemplateArg& argument) { return MatchTemplateArgument(pattern, argument); }
		/// Element `element` of a pack expansion in a pattern (`Rest...` in `Tuple<T, Rest...>`) against its argument.
		QueryResult<void> FromPackTemplateArgument(const AstNode* pattern, const TemplateArg& argument, uint32 element);

		/// The fixed parameters' arguments, then the pack's elements (none when nothing decided it).
		TemplateArgs Arguments() const;

	private:
		QueryResult<void> MatchLayers(const QualTypeNode& pattern, uint32 layers, const Type* argument, bool decay);
		QueryResult<void> MatchBase(const QualTypeNode& pattern, const Type* argument, bool decay);
		QueryResult<void> MatchNamed(const NamespaceQualifiedNameNode& name, const Type* argument, bool decay);
		QueryResult<void> MatchInstance(const NamespaceQualifiedNameNode& name, const ObjectType* object);
		QueryResult<void> MatchTemplateArgument(const AstNode* pattern, const TemplateArg& argument);
		QueryResult<void> MatchFunPtr(const FunPtrTypeNode& pattern, const Type* argument);
		QueryResult<void> MatchValue(const AstNode* pattern, const ConstantValue& value);
		QueryResult<void> Decide(uint32 index, const TemplateArg& argument);
		int ParameterIndex(const Core::String& name) const;

		const ProgramModel& m_Model;
		const TemplateDefinitionNode& m_Params;
		TemplateArgs m_Args;          // the fixed parameters
		std::vector<bool> m_Written;  // fixed parameter i was written explicitly
		int m_PackIndex = -1;         // the pack parameter, if any
		TemplateArgs m_Pack;          // its elements
		bool m_PackWritten = false;   // the call wrote them (explicit arguments past the fixed ones)
		uint32 m_Element = 0;         // the pack element the match in progress decides
	};
} // namespace Scripting
