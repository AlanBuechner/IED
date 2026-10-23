#pragma once
#include <vector>
#include "Model/SymbolTable.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/FunctionType.h"

namespace Scripting
{
	class ProgramModel;
	class TypeStore;
	struct AstNode;

	/// One capture of a lambda (docs/language/07, D-22): a closure field.
	struct ClosureCapture
	{
		enum class Kind
		{
			Value,      // a copy of a variable or of an init-capture expression
			Reference,  // the address of a variable (or of an init-capture lvalue)
			This,       // the enclosing object's address
			ThisCopy    // a copy of the enclosing object
		};
		Kind kind = Kind::Value;
		Core::String name;        // what the body calls it ("this" for This / ThisCopy)
		const Type* type = nullptr; // the captured value's type (the referent of a reference capture)
		uint32 field = 0;         // index into the closure type's fields
		bool constReferent = false; // Reference / This: the pointer points at const (a read-only variable, `this` in a const method)
	};

	/// What a lambda expression is made of: everything the model needs to build its closure type.
	struct ClosureSpec
	{
		Core::Vector<ClosureCapture> captures; // field indices are assigned by CreateClosure
		const Type* returned = nullptr;                   // the call method's signature: what it returns,
		Core::Vector<FunctionType::ParameterInfo> params; // and its parameters, named
		Core::Vector<Core::String> paramNames;
		std::vector<bool> paramIsConst;
		const AstNode* body = nullptr;          // the lambda's body: with `owner`, what its closure is kept by
		const Scope* scope = nullptr;           // type lookup context of the body
		uint32 unitId = 0;
		const ObjectType* enclosingType = nullptr; // the object `this` names in the body (null outside a method)
		bool isMutable = false;
		Core::String owner;                     // the symbol of the code that writes the lambda: it is named for it
		Core::String ownerName;                 // that code as people read it: `Profile::StartSession`
	};

	/// A lambda's closure type: an object whose fields are the captures, with an `[operator("call")]` method
	/// whose body is the lambda's. A lambda that captures nothing has a static call method: its address is a
	/// funcptr.
	struct Closure
	{
		const ObjectType* type = nullptr;
		const FunctionRecord* call = nullptr;
		Core::Vector<ClosureCapture> captures;
		const ObjectType* enclosingType = nullptr;
		bool isMutable = false;
		Core::String displayName;   // "lambda in Profile::StartSession", "lambda #2 in ...": the census shows it

		bool CapturesNothing() const { return captures.Empty(); }
		/// Whether the closure is one pointer (`[this]`, `[&x]`): a func then holds that pointer as its
		/// environment instead of a copy of the closure.
		bool IsOnePointer() const;
		const ClosureCapture* ThisCapture() const;
	};

	/// Builds closure types for the model (friend: it registers the type, its fields and its call method).
	class ClosureTypes
	{
	public:
		static const Closure* Create(ProgramModel& model, const ClosureSpec& spec);

	private:
		static ObjectType* NewClosureType(const Core::String& name, Core::Vector<ClosureCapture>& captures, TypeStore& types);
		static const FunctionRecord* AddCallMethod(ProgramModel& model, ObjectType* type, const ClosureSpec& spec, const FunctionType* signature,
			bool isStatic);
	};
}
