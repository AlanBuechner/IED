#pragma once
#include <optional>
#include "Backend/HIR/LoweringContext.h"
#include "Backend/HIR/Construction.h"

namespace Scripting
{
	// Forward-declared AST node types used in the per-node method signatures; ExprLowerer.cpp
	// includes their full definitions. (The lowerer only stores pointers to them.)
	struct LiteralNode; struct IdentifierExpressionNode; struct MemberAccessNode;
	struct BinaryExpressionNode; struct UnaryExpressionNode; struct AssignmentExpressionNode;
	struct FunctionCallNode; struct CastExpressionNode; struct TernaryExpressionNode;
	struct ParenthesizedExpressionNode; struct ArrayIndexNode; struct TypeCheckNode;
	struct SafeCastNode;
	struct StaticAccessNode; struct NewExpressionNode; struct BracedInitializerNode;
	struct TypeOfExpressionNode; struct LambdaNode; struct PackExpansionNode; struct ValueListNode;
	struct NamespaceQualifiedNameNode; struct TemplateArgumentListNode;
	class ArrayType;

	// =====================================================================
	// EXPRESSION LOWERER (05, expression rows) - one method per AST node type. Every method:
	// resolve operands left-to-right (source order), ask the model through ctx.queries(), emit HIR
	// nodes into the arena, return a Value. A failure reports once via ctx.Fail and aborts this
	// function - there is no other error path. Lvalues are arena descriptors; LoadIfNeeded is the
	// single point where one becomes an rvalue (a LoadNode). This class owns ONLY expressions;
	// statements live in StmtLowerer. The two meet at the top: LowerFunction calls both.
	// =====================================================================
	class ExprLowerer
	{
	public:
		explicit ExprLowerer(LoweringContext& ctx) : m_Ctx(ctx) {}
		// Walk an lvalue chain (field/deref/indexop) reporting the first node that makes a WRITE illegal:
		// ephemeral (rooted in a by-value call temp - the store would be discarded) or read-only (ReadOnlyReason;
		// through a `const T*`). Returns true and fills `why` when a store must be rejected; false means the
		// location is writable. Reads are never blocked - only stores call this.
		bool ChainBlocksWrite(const LValue* lv, Core::String& why) const;

		QueryResult<Value> ResolveExpr(const AstNode* node);
		/// `node` as the value of a `target`: where a funcptr or func is expected, a function template's name is
		/// the address of the instance that signature deduces (D-17: `funcptr<int32(int32)> f = Identity;`).
		QueryResult<Value> ResolveFor(const AstNode* node, const Type* target);
		/// `[e0, e1]` as the return list `list` (D-34): each value converted to its element.
		QueryResult<Value> ResolveValueList(const ValueListNode* n, const ObjectType* list);
		/// `[e0, e1]` as the return list of its values' own types (the right side of a destructuring).
		QueryResult<Value> ResolveValueList(const ValueListNode* n);
		/// `targetNode = value` where `value` is already lowered (a destructured element, D-34): the store, or the
		/// target's assignment when it has a lifecycle.
		QueryResult<Value> AssignTo(const AstNode* site, const AstNode* targetNode, const Value& value);
		/// A braced initializer list `{e0, e1, ...}` lowered against a known target __init_list<T>:
		/// each element is materialized (converted to T) and the result is an InitListNode of that
		/// type. Codegen builds the {ptr, size} aggregate. Used by call arguments AND local decls.
		QueryResult<Value> ResolveInitList(const BracedInitializerNode* n, const Type* target);
		QueryResult<void> AppendInitListElements(const AstNode* entry, const Type* elemType, std::vector<const HIRNode*>& elements);
		/// A braced list that initializes a value of type `target`: an __init_list, a construction of an object
		/// type (`T{...}`), or an array.
		QueryResult<Value> ResolveBracedAs(const BracedInitializerNode* brace, const Type* target);
		/// A string literal that initializes the character array `array`: its units in order, a terminator where
		/// there is room (C's rule), zeros after it.
		QueryResult<Value> ResolveTextAs(const LiteralNode* text, const ArrayType* array);
		QueryResult<void> MaterializeBracedArguments(const std::vector<const BracedInitializerNode*>& braced,
			const Core::Vector<const Type*>& params, std::vector<const HIRNode*>& argNodes);
		/// The arguments of a construction in order, each pack expansion standing for its copies.
		QueryResult<std::vector<ConstructionArgument>> ConstructionArguments(const Core::Vector<Ref<AstNode>>& nodes);
		/// The binary operator `tok` on two resolved operands: an operator method of either, else the built-in
		/// operation.
		QueryResult<Value> ApplyBinary(const AstNode* n, const Core::String& tok, Value lhs, Value rhs);
		/// `lhs && rhs` / `lhs || rhs`: `rhs` runs only when it decides the result.
		QueryResult<Value> ApplyShortCircuit(const AstNode* n, bool isAnd, const Value& lhs, const Value& rhs);

		/// The value of `callee`'s default for parameter `index` (which has one), computed for this call.
		QueryResult<Value> DefaultArgument(const FunctionRecord& callee, uint32 index);
		/// `new (storage) T(args)` once `storage` (a T*) and the arguments are lowered: the object zeroed with its vptrs,
		/// then started as `T(args)` starts one; the value is the pointer. `site` (null in code the compiler writes) is
		/// where a failure is reported.
		QueryResult<Value> PlacementNew(const AstNode* site, const ObjectType* type, const HIRNode* storage, const std::vector<ConstructionArgument>& args);
	private:
		QueryResult<Value> ResolveBracedObject(const AstNode* site, const ObjectType* obj, const BracedInitializerNode* brace);
		friend class LambdaLowerer; // builds a closure object through ResolveConstruct

		QueryResult<Value> ResolveLiteral(const LiteralNode* n);
		QueryResult<Value> NumberConstant(const LiteralNode* n);
		Value Constant(const Type* type, const ConstantValue& value);
		QueryResult<Value> ResolveIdentifier(const IdentifierExpressionNode* n);
		/// A function's name as a value: the address of a free function or a static method (an instance method has none).
		QueryResult<Value> DecayedFunction(const AstNode* n, const Core::String& name, const FunctionRecord& rec, const FunctionType* sig);
		/// `owner`'s method `member` as a value (DecayedFunction); NotFound when `owner` has no method of that name.
		QueryResult<Value> MethodValue(const AstNode* n, const ObjectType* owner, const Core::String& member);
		/// The function `name` (plain or namespace-qualified), or the instance `tmplArgs` name of the function template
		/// `name`, as a value (DecayedFunction); NotFound when none has that name.
		QueryResult<Value> FunctionValue(const AstNode* n, const Core::String& name, const TemplateArgumentListNode* tmplArgs = nullptr);
		/// `F<args>` or `Ns::F<args>` as a value: that instance's address.
		QueryResult<Value> ResolveInstanceName(const NamespaceQualifiedNameNode* n);
		QueryResult<Value> ResolveMemberAccess(const MemberAccessNode* n);
		QueryResult<Value> ResolveReferenceCast(const CastExpressionNode* n, const Value& operand, const ReferenceType* target);
		/// A cast to a packed vector (D-62), checked already: a broadcast, the same bits (`keepsBits`, reinterpret_cast),
		/// or a lane-by-lane conversion.
		QueryResult<Value> VectorCast(const HIRNode* value, const SwizzleType* target, bool keepsBits);
		QueryResult<Value> ResolveCheckedReferenceCast(const AstNode* n, const Value& operand, const ReferenceType* target);
		QueryResult<const HIRNode*> ObjectOrBadCast(const AstNode* n, const HIRNode* pointer, const Type* referent);
		// `obj.Base::member`: the object as its `Base` sub-object, which must be its type or one of its bases.
		struct QualifiedObject { const ObjectType* type = nullptr; const LValue* location = nullptr; };
		QueryResult<QualifiedObject> QualifiedSubobject(const MemberAccessNode* n, const Type* objectType, const LValue* object);
		QueryResult<Value> ResolveBinary(const BinaryExpressionNode* n);
		/// `(Mode)-1`: a parenthesized type before an operator that is also a prefix one is that operator's left
		/// operand, never a cast (docs/language/10 section 4); the message for it, or "" when the operand is no type.
		Core::String TypeAsOperand(const BinaryExpressionNode* n, const Core::String& tok) const;
		QueryResult<Value> ResolveShortCircuit(const AstNode* n, bool isAnd);
		QueryResult<Value> EmitOperatorCall(const AstNode* n, const OperatorBinding& b, const Value& lhs, const Value& rhs);
		QueryResult<Value> ResolveUnary(const UnaryExpressionNode* n);
		/// A built-in operator with a `packed T[N]` operand (D-36): lane by lane, a scalar operand broadcast to
		/// every lane; a comparison gives a `packed bool[N]`.
		QueryResult<Value> VectorBinary(const AstNode* n, IntrinsicOp op, bool isCompare, const Value& lhs, const Value& rhs);
		/// `operand` read as a packed run of type `other` when it is a pure packed vector object of the same lanes, else null.
		std::optional<Value> SameLanesAs(const Value& operand, const Type* other);
		/// The value an object or array target of an assignment takes from `value` (a derived object's base part).
		QueryResult<Value> AssignedPart(const AstNode* n, const Type* target, const Value& value);
		/// `value` + 1 or - 1 for `++` / `--`: a pointer moves by one element.
		const HIRNode* Stepped(const HIRNode* value, bool up);
		// Assignments (ExprAssignment.cpp).
		QueryResult<Value> ResolveAssignment(const AssignmentExpressionNode* n);
		QueryResult<void> CheckWritable(const AstNode* n, const Value& target);
		bool LanesTakeBraces(const Value& target, const BracedInitializerNode& brace) const;
		QueryResult<Value> AssignLanes(const AstNode* n, const Value& target, const BracedInitializerNode& brace);
		QueryResult<Value> AssignValue(const AstNode* n, const Value& target, const Value& value);
		/// The value a plain `=` stores into `target`: converted, or loaded through a pointer to the object.
		QueryResult<const HIRNode*> AssignedValue(const AstNode* n, const Value& target, const Value& value);
		QueryResult<Value> CompoundAssignOnce(const AstNode* n, const Core::String& tok, const Value& target, const Value& value);
		QueryResult<Value> CompoundAssign(const AstNode* n, const Core::String& tok, const Value& target, const Value& value);
		QueryResult<Value> StoreAssigned(const AstNode* n, const Core::String& tok, const Value& target, const HIRNode* toStore);
		QueryResult<Value> ResolveCall(const FunctionCallNode* n);
		/// What a call's callee names: a function (a method when there is a receiver) or a value called through.
		struct CallTarget
		{
			Core::String name;
			const Type* receiverType = nullptr;
			const LValue* receiverLoc = nullptr;
			const HIRNode* indirect = nullptr;          // a funcptr or func called through
			const SignatureType* signature = nullptr;
			bool qualified = false;                     // `Type::Method(...)`: one implementation, never dispatched
		};
		QueryResult<CallTarget> ResolveCallTarget(const AstNode* callee);
		QueryResult<CallTarget> NamedCallTarget(const IdentifierExpressionNode* id);
		QueryResult<CallTarget> MemberCallTarget(const MemberAccessNode* ma);
		/// `Shape.Twice(7)`: a method of a type is called as `Shape::Twice(7)` (a static field takes either spelling).
		QueryResult<void> CheckTypeReceiver(const MemberAccessNode* ma);
		QueryResult<CallTarget> StaticCallTarget(const StaticAccessNode* sa);
		QueryResult<CallTarget> ValueCallTarget(const AstNode* callee);
		/// Whether `name` (plain or qualified) names a variable here: a call of it calls the variable's value.
		bool NamesVariable(const Core::String& name);
		QueryResult<void> CallThrough(const AstNode* callee, const Value& value, CallTarget& target);
		/// `Ns::Fn` (or `A::B::C`) from the static-access chain a qualified call target parses as.
		static bool QualifiedCallName(const StaticAccessNode* sa, Core::String& out);
		/// A call's arguments in order, parallel: a braced list `{...}` has no type of its own, so it is typeless
		/// (nullptr) until it is materialized against its parameter's __init_list<T>; `values` keeps each
		/// argument as written (an lvalue stays one) so a `T&` parameter can take its address.
		struct CallArgumentList
		{
			Core::Vector<const Type*> types;
			std::vector<const HIRNode*> nodes;
			std::vector<Value> values;
			std::vector<const BracedInitializerNode*> braced;

			/// Room for `count` arguments in each list: a call's are made one by one.
			void Reserve(uint32 count)
			{
				types.Reserve(count);
				nodes.reserve(count);
				values.reserve(count);
				braced.reserve(count);
			}
		};
		QueryResult<CallArgumentList> ResolveCallArguments(const Core::Vector<Ref<AstNode>>& nodes);
		void AppendCallArgument(CallArgumentList& list, const Value& value);
		/// Each function template's name among the arguments becomes the address of the instance its
		/// parameter's signature deduces; one with no funcptr or func parameter is an error.
		QueryResult<void> BindTemplateNames(const AstNode* site, CallArgumentList& list, const Core::Vector<const Type*>& params);
		static bool HasTemplateNames(const CallArgumentList& list);
		QueryResult<Value> CallIndirect(const FunctionCallNode* n, const CallTarget& target, CallArgumentList& args);
		QueryResult<const FunctionType*> ResolveCallee(const FunctionCallNode* n, const CallTarget& target, CallArgumentList& args, const FunctionRecord*& rec);
		QueryResult<const FunctionType*> RetryWithNullPointers(const Core::String& name, CallArgumentList& args, const FunctionRecord*& rec,
			const QueryResult<const FunctionType*>& unmatched);
		static bool IsZeroLiteral(const Type* type, const HIRNode* node);
		QueryResult<Value> CallWithoutFunction(const FunctionCallNode* n, const CallTarget& target, CallArgumentList& args,
			const QueryResult<const FunctionType*>& unmatched);
		const ObjectType* ConstructedType(const FunctionCallNode* n, const Core::String& name);
		QueryResult<Value> ConstructorCall(const AstNode* callee, const ObjectType* type, const CallArgumentList& args);
		QueryResult<Value> CallFunction(const FunctionCallNode* n, const CallTarget& target, CallArgumentList& args, const FunctionRecord& rec,
			const FunctionType& sig);
		QueryResult<void> BindCallArguments(const FunctionCallNode* n, CallArgumentList& args, const FunctionType& sig, bool foreign);
		QueryResult<void> AppendDefaultArguments(const AstNode* callee, const Core::String& name, std::vector<const HIRNode*>& nodes,
			const FunctionRecord& rec, const FunctionType& sig);
		QueryResult<const LValue*> CallReceiver(const FunctionCallNode* n, const CallTarget& target, const FunctionRecord& rec);
		/// A function template's name written as a value without its arguments (`Identity`, `Math::Max`), when no
		/// local, variable or function of that name exists; null otherwise.
		const TemplateNameType* TemplateNameOf(const AstNode* node);
		QueryResult<Value> TemplateAddress(const AstNode* site, const TemplateNameType& named, const SignatureType& target);
		/// An argument's values: one, or the copies of a pack expansion (`args...`, `Wrap(args)...`).
		QueryResult<std::vector<Value>> ResolveArgument(const AstNode* node);
		QueryResult<std::vector<Value>> ResolveExpansion(const PackExpansionNode* n);
		// What a value callee calls: a funcptr / func target, or an object whose call operator is the method.
		struct CalleeValue
		{
			const HIRNode* target = nullptr;
			const SignatureType* signature = nullptr;
			const ObjectType* object = nullptr;
			const LValue* objectLocation = nullptr;
		};
		QueryResult<CalleeValue> CalleeFromValue(const Value& v);
		static bool IsCallableValue(const Type* t);
		bool IsCallableField(const Type* owner, const Core::String& name) const;
		QueryResult<Value> ResolveCast(const CastExpressionNode* n);
		QueryResult<Value> ResolveTernary(const TernaryExpressionNode* n);
		QueryResult<Value> ResolveParen(const ParenthesizedExpressionNode* n);
		QueryResult<Value> ResolveArrayIndex(const ArrayIndexNode* n);
		QueryResult<Value> ResolveTypeCheck(const TypeCheckNode* n);
		QueryResult<Value> ResolveSafeCast(const SafeCastNode* n);
		/// How a receiver may be used (D-31): a read-only location calls only const methods.
		ReceiverAccess AccessOf(const Value& v) const;
		ReceiverAccess ImplicitReceiverAccess() const;
		QueryResult<Value> ResolveStaticAccess(const StaticAccessNode* n);
		/// The object type a `Type::member` qualifier names, or null when it names none (a namespace, an enum).
		QueryResult<const Type*> QualifierType(const AstNode* base) const;
		const Type* MemberType(const ObjectType* owner, const Core::String& name) const;
		const SymbolInfo* MemberConstant(const ObjectType* owner, const Core::String& name) const;
		QueryResult<Value> EnumMemberValue(const EnumType* en, const Core::String& member);
		QueryResult<Value> StaticFieldValue(const ObjectType* owner, const Core::String& member);
		// `E::Member` of an enum declared in the body being lowered; null when `qualifier` names no such enum.
		QueryResult<Value> ResolveNew(const NewExpressionNode* n);
		QueryResult<Value> ResolveConstruct(const AstNode* callee, const ObjectType* obj, const std::vector<ConstructionArgument>& args, bool braced);
		QueryResult<Value> NewScalar(const AstNode* n, NewNode* nw, const std::vector<ConstructionArgument>& args);
		/// `new T(args)` of an object type at `nw` (its storage, or none for the managed heap).
		QueryResult<Value> NewObject(const AstNode* site, NewNode* nw, const ObjectType* obj, const std::vector<ConstructionArgument>& args, bool braced);
		Value NewHolding(NewNode* nw, const HIRNode* value);
		const HIRNode* AfterPrelude(const std::vector<const HIRNode*>& prelude, const HIRNode* value, bool fresh);
		QueryResult<const HIRNode*> PlacementStorage(const NewExpressionNode* n, const Type* ptrT);
		/// Compile-time type query: sizeof(T) / __uuidof(T) -> an int64 ConstantNode (the byte size).
		QueryResult<Value> ResolveTypeOf(const TypeOfExpressionNode* n);
		/// `sizeof...(Ts)`: the element count of a pack, an int64 constant.
		QueryResult<Value> ResolveSizeOfPack(const TypeOfExpressionNode* n);
		QueryResult<Value> ResolveUuidOf(const TypeOfExpressionNode* n);
		QueryResult<Value> ResolveNameOf(const TypeOfExpressionNode* n);
		QueryResult<const Type*> QueryOperandType(const TypeOfExpressionNode* n);
		QueryResult<const Type*> UnevaluatedType(const AstNode* operand);
		// A lambda expression: a closure object (LambdaLowerer).
		QueryResult<Value> ResolveLambda(const LambdaNode* n);
		// `this` as a location: the receiver, or the object a lambda captured.
		bool IsThisObject(const LValue* lv) const { return lv->kind == LValue::Kind::This || lv == m_Ctx.capturedThis; }
		Value ThisAsPointer(const Value& value);
		// The type a plain name in the body's own layers stands for: a template parameter's argument or a local
		// type; null for anything else.
		const Type* BoundTypeNamed(const AstNode* base) const;

		// `p as T*`, `dynamic_cast<T*>(p)` and `p is T` on pointers to objects: an upcast converts, a
		// downcast or cross-cast of a polymorphic object is a DynamicCastNode.
		QueryResult<Value> ResolveDynamicCast(const AstNode* n, const Value& operand, const Type* to, bool testOnly);
		// The global behind a static data member declared in `owner`.
		const SymbolInfo* StaticMemberSymbol(const ObjectType* owner, const Core::String& name) const;

		// The object whose members/methods are accessed THROUGH `base`, plus the location to hang them off.
		// A pointer base is DEREFERENCED (p->x / p->m() access *p, not p): it builds a Deref lvalue over
		// the materialized pointer. A reference/alias base is transparent (UnwrapToValue resolves it); a
		// plain object base passes through. THE single place "access through indirection" lives - member
		// access and method calls both use it, so neither rolls its own unwrap loop.
		void DerefForAccess(const Value& base, const Type*& outType, const LValue*& outLoc) const;
		/// '.' reaches a member of an object, '->' one through a pointer; `this` takes either.
		QueryResult<void> CheckAccessOperator(const MemberAccessNode* n, const Value& base) const;

		LoweringContext& m_Ctx; // the per-function state (slots, arena, queries)
	};
} // namespace Scripting
