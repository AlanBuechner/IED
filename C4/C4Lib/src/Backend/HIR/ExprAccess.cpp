#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/ReferenceBinding.h"
#include "Backend/HIR/ConstantOperands.h"
#include <tuple>
#include "Backend/HIR/ReadOnly.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Backend/HIR/LambdaLowerer.h"
#include "Backend/HIR/FoldLowerer.h"
#include "Backend/HIR/PackElements.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/VarArgs.h"
#include "Backend/HIR/BuiltinOperators.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Model/Types/ClassHierarchy.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/ArrayIndexNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Model/Types/ReturnLists.h"
#include "Model/Types/QualifiedName.h"
#include "Backend/HIR/PackedViews.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Model/TypeHandle.h"

namespace Scripting
{
	void ExprLowerer::DerefForAccess(const Value& base, const Type*& outType, const LValue*& outLoc) const
	{
		outType = TypeHandle::UnwrapToValue(base.type); // transparent: alias/reference -> value
		outLoc = base.isLValue ? base.lvalue : nullptr; // rvalue base: codegen loads it first
		if (outType->GetKind() == TypeKind::Pointer)
		{
			// A pointer is a first-class value: p->x / p->m() access *p, not p. Build a Deref lvalue over
			// the materialized pointer and point the member/method at it.
			LValue deref; deref.kind = LValue::Kind::Deref;
			deref.type = TypeHandle::GetPointedType(outType);
			deref.addr = m_Ctx.LoadIfNeeded(base);
			outLoc = m_Ctx.NewLValue(deref);
			outType = TypeHandle::UnwrapToValue(deref.type); // alias -> object
		}
	}

	QueryResult<void> ExprLowerer::CheckAccessOperator(const MemberAccessNode* n, const Value& base) const
	{
		const Type* value = TypeHandle::UnwrapToValue(base.type);
		const bool pointer = value && value->GetKind() == TypeKind::Pointer;
		const bool isThis = base.isLValue && IsThisObject(base.lvalue);
		if (pointer == n->IsArrowAccess() || isThis || !value) return {};
		const Core::String& member = n->GetMemberNameToken().GetText();
		if (pointer)
			return QueryResult<void>::Fail(QueryError::NotAValue, "Cannot use '.' on a pointer of type '" + value->GetName() +
				"'. Dereference it with '->' instead (e.g. `ptr->" + member + "`).");
		return QueryResult<void>::Fail(QueryError::NotAValue, "Cannot use '->' on '" + value->GetName() +
			"', which is not a pointer; use '.' instead (e.g. `value." + member + "`).");
	}

	// Walk an lvalue chain (field/deref/indexop) reporting the first node that makes a WRITE illegal:
	// ephemeral (rooted in a by-value call temp - the store would be discarded) or constRooted (passes
	// through a `const T*`). Returns true and fills `why` when a store must be rejected; false means the
	// location is writable. Reads are never blocked - only stores call this.
	ReceiverAccess ExprLowerer::AccessOf(const Value& v) const
	{
		return v.isLValue && IsReadOnly(m_Ctx, v.lvalue) ? ReceiverAccess::ReadOnly : ReceiverAccess::Writable;
	}

	ReceiverAccess ExprLowerer::ImplicitReceiverAccess() const
	{
		// An unqualified method call names `this` (or a lambda's captured object) when there is one.
		const bool hasThis = m_Ctx.capturedThis || (m_Ctx.fn && m_Ctx.fn->containingType && !m_Ctx.fn->isStatic && m_Ctx.enclosingType);
		return hasThis ? AccessOf(Value::L(m_Ctx.ReceiverLValue())) : ReceiverAccess::Writable;
	}

	bool ExprLowerer::ChainBlocksWrite(const LValue* lv, Core::String& why) const
	{
		for (const LValue* p = lv; p; )
		{
			if (p->ephemeral) { why = "access chain is rooted in a function-call result that returns by value"; return true; }
			p = (p->kind == LValue::Kind::Field || p->kind == LValue::Kind::Base || p->kind == LValue::Kind::IndexOp) ? p->base : nullptr;
		}
		why = ReadOnlyReason(m_Ctx, lv);
		return !why.empty();
	}

	QueryResult<Value> ExprLowerer::ResolveMemberAccess(const MemberAccessNode* n)
	{
		const Core::String& memberName = n->GetMemberNameToken().GetText();

		// A TYPE base (`Tally.total`, `Ns::Box::MakeVal`): the member is a STATIC data member or
		// static method of the type, not an instance. ResolveExpr would fail on a bare type name, so
		// handle it before that. The base is either a plain identifier or a qualified name.
		{
			const AstNode* baseNode = n->GetBaseObject().Get();
			Core::String typePath;
			if (const auto* id = dynamic_cast<const IdentifierExpressionNode*>(baseNode))
				typePath = id->GetName();
			else if (const auto* qn = dynamic_cast<const NamespaceQualifiedNameNode*>(baseNode))
				for (uint32 i = 0; i < qn->SegmentCount(); ++i) { if (i) typePath += "::"; typePath += qn->GetSegments()[i].nameToken.GetText(); }

			if (!typePath.empty())
			{
				auto tr = m_Ctx.queries().QueryType(typePath, m_Ctx.unitId, m_Ctx.scope);
				const ObjectType* obj = tr.Ok() ? dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(tr.value)) : nullptr;
				if (obj)
				{
					// Static data member: one global registered in its declaring type's class scope.
					ClassHierarchy::FieldHit sf = ClassHierarchy::FindField(obj, memberName);
					if (sf.field && sf.field->m_IsStatic)
						if (const SymbolInfo* sym = StaticMemberSymbol(sf.owner, memberName))
						{
							LValue lv; lv.kind = LValue::Kind::Global; lv.type = sf.field->m_Type; lv.sym = sym;
							return Value::L(m_Ctx.NewLValue(lv));
						}
				}
			}
		}

		auto base = ResolveExpr(n->GetBaseObject().Get());
		if (!base.Ok()) return base;
		if (auto spelled = CheckAccessOperator(n, base.value); !spelled.Ok())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(spelled.error, spelled.message));
		const Type* baseType = nullptr; const LValue* baseLoc = nullptr;
		DerefForAccess(base.value, baseType, baseLoc);
		// Member access needs an ADDRESS for the base. A by-value aggregate call result (e.g. `GetVal().f`)
		// is an SSA value with no addressable location: mark the call to materialize its return into a temp
		// and treat that temp as the base (a Deref lvalue over it), so field GEPs land in real storage.
		if (baseLoc == nullptr && base.value.IsRValue() && baseType)
			baseLoc = Lifecycle(m_Ctx).AddressableObject(base.value, baseType);
		if (n->IsQualified())
		{
			auto viewed = QualifiedSubobject(n, baseType, baseLoc);
			if (!viewed.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(viewed.error, viewed.message));
			baseType = viewed.value.type;
			baseLoc = viewed.value.location;
		}
		auto q = m_Ctx.queries().QueryMember(baseType, memberName);
		if (!q.Ok())
		{
			// A packed view (v.x / v.xz / v.data / o.m0.x): not an ordinary field. Checked BEFORE reporting so a
			// valid view does not leave a spurious "no member" diagnostic behind.
			if (const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(baseType)))
			{
				const Core::String& name = n->GetMemberNameToken().GetText();
				if (const LValue* view = PackedViewLValue(m_Ctx, obj, baseLoc, name)) return Value::L(view);
				// A STATIC data member (Tally.total): one global registered in its declaring type's class scope.
				ClassHierarchy::FieldHit sf = ClassHierarchy::FindField(obj, name);
				if (sf.field && sf.field->m_IsStatic)
					if (const SymbolInfo* sym = StaticMemberSymbol(sf.owner, name))
					{
						if (auto allowed = AccessCheck::CheckField(m_Ctx, sf.owner, *sf.field); !allowed.Ok())
							return m_Ctx.Fail(n, QueryResult<Value>::Fail(allowed.error, allowed.message));
						LValue lv; lv.kind = LValue::Kind::Global; lv.type = sf.field->m_Type; lv.sym = sym;
						return Value::L(m_Ctx.NewLValue(lv));
					}
			}
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(q.error, q.message)); // genuinely not a member and not a lane
		}

		if (q.value.IsField)
		{
			const ObjectType::FieldInfo* f = q.value.field;
			const ObjectType* owner = q.value.owner;
			if (auto allowed = AccessCheck::CheckField(m_Ctx, owner, *f); !allowed.Ok())
				return m_Ctx.Fail(n, QueryResult<Value>::Fail(allowed.error, allowed.message));
			LValue lv; lv.kind = LValue::Kind::Field; lv.type = f->m_Type; lv.field = f;
			lv.base = SubobjectLValue(m_Ctx, baseLoc, owner, q.value.ownerOffset); // the declaring sub-object
			const LValue* field = m_Ctx.NewLValue(lv);
			return Value::L(ReferenceBinding::Of(f->m_Type) ? ReferenceBinding::Referent(m_Ctx, field) : field);
		}
		return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotCallable, "'" + n->GetMemberNameToken().GetText() + "' is a method; call it"));
	}

	QueryResult<ExprLowerer::QualifiedObject> ExprLowerer::QualifiedSubobject(const MemberAccessNode* n, const Type* objectType, const LValue* object)
	{
		using Result = QueryResult<QualifiedObject>;
		const Core::String written = n->QualifierText();
		const auto* self = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(objectType));
		if (!self || !object)
			return Result::Fail(QueryError::NotAValue, "'" + written + "::" + n->GetMemberNameToken().GetText() + "' names a member of a class, but the object is not one");
		auto found = m_Ctx.queries().QueryType(written, m_Ctx.unitId, m_Ctx.scope);
		const auto* qualifier = found.Ok() ? dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(found.value)) : nullptr;
		if (!qualifier) return Result::Fail(QueryError::NotFound, "'" + written + "' is not a class, so it cannot qualify a member of '" + QualifiedName(self) + "'");
		const ClassHierarchy::BaseLocation at = ClassHierarchy::LocateBase(self, qualifier);
		if (!at.found)
			return Result::Fail(QueryError::NotFound, "'" + written + "' is not '" + QualifiedName(self) + "' or one of its bases");
		if (at.ambiguous)
			return Result::Fail(QueryError::Ambiguous, "'" + written + "' is more than one sub-object of '" + QualifiedName(self) + "'");
		return Result::Success(QualifiedObject{ qualifier, SubobjectLValue(m_Ctx, object, qualifier, at.offset) });
	}

	const SymbolInfo* ExprLowerer::StaticMemberSymbol(const ObjectType* owner, const Core::String& name) const
	{
		const Scope* classScope = m_Ctx.queries().Model()->TypeScope(owner);
		const SymbolInfo* sym = classScope ? m_Ctx.queries().Model()->Symbols()->FindVisible(classScope, name, m_Ctx.unitId) : nullptr;
		return (sym && sym->kind == SymbolKind::Variable) ? sym : nullptr;
	}

	// `m[k]` on an object no [operator("index")] of which takes k: the key type its reader takes, or that it has none.
	static Core::String NoReaderMessage(const ModelQueries& queries, const ObjectType& type, const Type* key)
	{
		const FunctionRecord* reader = queries.FindOperatorMethod(&type, "index", 1);
		if (!reader || !reader->type || reader->type->GetParameterCount() != 1 || !key)
			return "Cannot index into '" + type.GetName() + "': it has no [operator(\"index\")] reader - the current value cannot be read";
		return "no [operator(\"index\")] of '" + type.GetName() + "' takes a '" + QualifiedName(TypeHandle::UnwrapToValue(key)) + "': its reader takes a '" +
			QualifiedName(reader->type->GetParameters()[0].m_Type) + "'";
	}

	// `a[i]` (05): an operator-overloaded `[]` wins (QO("[]")); otherwise built-in index math. The
	// result is an LVALUE (element slot/field) - `a[i] = v` stores through it with no special path.
	QueryResult<Value> ExprLowerer::ResolveArrayIndex(const ArrayIndexNode* n)
	{
		auto idx = ResolveExpr(n->GetIndexExpression().Get());
		if (!idx.Ok()) return idx;

		// A packed view (`a.data`, `data`, `a.xy`) resolves to its `packed T[N]` lanes, indexed below.
		auto arr = ResolveExpr(n->GetArrayExpression().Get());
		if (!arr.Ok()) return arr;

		const Type* baseType = TypeHandle::UnwrapToValue(arr.value.type);
		// A `packed T[N]` value (D-36): its lane at a runtime index.
		if (const auto* vector = dynamic_cast<const SwizzleType*>(baseType))
		{
			LValue lv; lv.kind = LValue::Kind::VectorLaneDyn; lv.type = vector->GetElementType();
			lv.base = Lifecycle(m_Ctx).AddressableObject(arr.value, vector);
			lv.index = m_Ctx.LoadIfNeeded(idx.value);
			return Value::L(m_Ctx.NewLValue(lv));
		}
		// Overloaded `[]` (a method returning a reference): the call IS the lvalue.
		auto op = m_Ctx.queries().QueryOperator("[]", baseType, TypeHandle::UnwrapToValue(idx.value.type), m_Ctx.unitId, m_Ctx.enclosingType,
			AccessOf(arr.value));
		if (op.error == QueryError::ReadOnly) return m_Ctx.Fail(n, QueryResult<Value>::Fail(op.error, op.message));
		if (op.Ok() && op.value.record)
		{
			const FunctionRecord* rec = op.value.record;
			if (!rec) return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotFound, "operator[] has no body"));
			const Type* rawRet = rec->type ? rec->type->GetReturnType() : nullptr;
			// A getter returning a reference names the element itself: reads load through it, writes store.
			if (rawRet && rawRet->GetKind() == TypeKind::Reference) return EmitOperatorCall(n, op.value, arr.value, idx.value);
			// The getter returns BY VALUE: `obj[i]` is not a memory location. Reading calls the getter;
			// writing must dispatch to [operator("[]=")] (there is no address to store through). Build a
			// pre-built getter call for reads and remember the setter for the assignment path to invoke.
			auto callR = EmitOperatorCall(n, op.value, arr.value, idx.value);
			if (!callR.Ok()) return callR;
			const Type* elem = TypeHandle::UnwrapToValue(rawRet); // the element type the getter yields
			// The write form, [operator("[]=")] / [operator("index_assign")], takes the index and the value. The
			// setter is used only when the object has one: a reader alone makes `obj[i] = v` an error.
			const auto* indexed = dynamic_cast<const ObjectType*>(baseType);
			const FunctionRecord* setter = indexed ? m_Ctx.queries().FindOperatorMethod(indexed, "index_assign", 2) : nullptr;
			if (setter && (!setter->type || setter->type->GetParameterCount() != 2)) setter = nullptr;
			LValue lv; lv.kind = LValue::Kind::IndexOp; lv.type = elem;
			lv.getter = rec; lv.setter = setter;
			lv.receiver = arr.value.isLValue ? m_Ctx.NewLValue(*arr.value.lvalue) : nullptr;
			lv.idxNode = idx.value.isLValue ? m_Ctx.LoadIfNeeded(idx.value) : idx.value.rvalue;
			lv.read = callR.value.rvalue; // the getter call; EmitLoad returns it, the store path calls lv.setter
			return Value::L(m_Ctx.NewLValue(lv));
		}

		const HIRNode* baseAddr = nullptr; // a POINTER rvalue to element 0
		const Type* elemType = nullptr; size_t elemSize = 0;
		if (const auto* ar = dynamic_cast<const ArrayType*>(baseType))
		{
			elemType = ar->GetElementType(); elemSize = elemType ? elemType->GetSize() : 0;
			const Type* ptrToElem = m_Ctx.queries().TypeStore()->Pointer(elemType);
			// An lvalue array: its ADDRESS is the pointer to element 0 (not a load of the whole array).
			baseAddr = arr.value.isLValue ? m_Ctx.out->NewNode<AddressOfNode>(ptrToElem, arr.value.lvalue) : arr.value.rvalue;
		}
		else if (const auto* pt = dynamic_cast<const PointerType*>(baseType)) // p[i] == *(p + i)
		{
			const Type* elem = m_Ctx.queries().ElementOf(baseType);
			if (!elem) return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "cannot index this type"));
			elemType = elem; elemSize = elem->GetSize();
			baseAddr = m_Ctx.LoadIfNeeded(arr.value); // the pointer value itself
		}
		else if (baseType->GetKind() == TypeKind::InitList) // list[i] == *(list.data + i*sizeof(T))
		{
			const InitListType* il = static_cast<const InitListType*>(baseType);
			elemType = il->GetElementType(); elemSize = elemType ? elemType->GetSize() : 0;
			const Type* ptrToElem = m_Ctx.queries().TypeStore()->Pointer(elemType);
			LValue dataLv; dataLv.kind = LValue::Kind::Field; dataLv.type = ptrToElem;
			dataLv.base = arr.value.isLValue ? arr.value.lvalue : nullptr; // rvalue list: codegen loads it first
			dataLv.field = il->FindMember("data");
			baseAddr = m_Ctx.out->NewNode<LoadNode>(ptrToElem, m_Ctx.NewLValue(dataLv));
		}
		else if (baseType->GetKind() == TypeKind::Object)
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue,
				NoReaderMessage(m_Ctx.queries(), *static_cast<const ObjectType*>(baseType), idx.value.type)));
		else return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, "cannot index a non-array/pointer: '" + Core::String(baseType->GetName()) + "'"));

		const Type* intT = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64);
		const HIRNode* idxNode = m_Ctx.LoadIfNeeded(idx.value);
		if (const auto* array = dynamic_cast<const ArrayType*>(baseType))
			if (Core::String why = ConstantOperands::IndexProblem(*array, idxNode); !why.empty())
				return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotAValue, why));
		if (idxNode->type != intT) idxNode = m_Ctx.out->NewNode<ConvertNode>(idxNode, idxNode->type, intT);
		const HIRNode* offset = elemSize > 1
			? m_Ctx.out->NewNode<IntrinsicOpNode>(intT, IntrinsicOp::Mul, idxNode,
				m_Ctx.out->NewNode<ConstantNode>(intT, ConstantValue::Int((int64)elemSize)))
			: idxNode; // element size 1: the index IS the byte offset

		// The element address = base pointer + byte offset; `a[i]` is a Deref of that.
		const HIRNode* elemAddr = m_Ctx.out->NewNode<AddressArithmeticNode>(baseAddr->type, baseAddr, offset);
		LValue lv; lv.kind = LValue::Kind::Deref; lv.type = elemType; lv.addr = elemAddr;
		return Value::L(m_Ctx.NewLValue(lv));
	}

	// `E::X` / `Ns::C::K` (05): a qualified constant folds to a ConstantNode; a non-constant static
	// member is an lvalue (its backing storage) for read/write. The base may be a plain name, a
	// namespace-qualified name, or a type - all reduce to a dotted path we feed the model.
	// The qualifier of a static access as written, `A::B` (template arguments left out); "" for anything else.
	static Core::String QualifierPath(const AstNode* base)
	{
		if (const auto* qualType = dynamic_cast<const QualTypeNode*>(base)) base = qualType->BaseName().Get();
		if (const auto* qn = dynamic_cast<const NamespaceQualifiedNameNode*>(base))
		{
			Core::String path;
			for (uint32 i = 0; i < qn->SegmentCount(); ++i) { if (i) path += "::"; path += qn->GetSegments()[i].nameToken.GetText(); }
			return path;
		}
		if (const auto* id = dynamic_cast<const IdentifierExpressionNode*>(base)) return id->GetName();
		if (const auto* access = dynamic_cast<const StaticAccessNode*>(base))
		{
			const Core::String outer = QualifierPath(access->GetBaseType().Get());
			return outer.empty() ? outer : outer + "::" + access->GetMemberNameToken().GetText();
		}
		return "";
	}

	const Type* ExprLowerer::BoundTypeNamed(const AstNode* base) const
	{
		const auto* id = dynamic_cast<const IdentifierExpressionNode*>(base);
		const SymbolInfo* local = id ? m_Ctx.queries().FindLocal(id->GetName()) : nullptr;
		return local && local->kind == SymbolKind::Type ? local->type : nullptr;
	}

	static bool HasTemplateArguments(const NamespaceQualifiedNameNode& name)
	{
		for (const auto& seg : name.GetSegments())
			if (seg.templateArgs) return true;
		return false;
	}

	// The template instance an alias names (`alias IntBox = Box<int32>;` for `IntBox::Size()`), or null: an instance's
	// members are in its own scope, never under the alias's name, so a call through the alias takes the instance as its
	// receiver (BUG-422). An alias of a plain type keeps the lookup by name, which finds its members.
	const ObjectType* ExprLowerer::AliasedInstance(const AstNode* base) const
	{
		const Core::String path = QualifierPath(base);
		if (path.empty()) return nullptr;
		// Written without template arguments, a name that is an instance can only be an alias of one.
		if (const auto* qualType = dynamic_cast<const QualTypeNode*>(base)) base = qualType->BaseName().Get();
		if (const auto* written = dynamic_cast<const NamespaceQualifiedNameNode*>(base); written && HasTemplateArguments(*written)) return nullptr;
		auto type = m_Ctx.queries().QueryType(path, m_Ctx.unitId, m_Ctx.scope);
		if (!type.Ok() || !type.value) return nullptr;
		const auto* instance = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(type.value));
		return instance && !instance->GetTemplateArguments().Empty() ? instance : nullptr;
	}


	// The type a qualifier or a composite literal's prefix names (`E`, `Ns::C`, `Box<int32>`, `Outer<float>::Inner`,
	// a bound `T`), or null when it names a namespace or no type. Naming an instance that cannot be made is an error.
	QueryResult<const Type*> ExprLowerer::QualifierType(const AstNode* base) const
	{
		using Result = QueryResult<const Type*>;
		if (const Type* bound = BoundTypeNamed(base)) return Result(TypeHandle::UnwrapToValue(bound));
		if (const auto* access = dynamic_cast<const StaticAccessNode*>(base))
		{
			auto outer = QualifierType(access->GetBaseType().Get());
			if (!outer.Ok()) return outer;
			if (const auto* obj = dynamic_cast<const ObjectType*>(outer.value)) return Result(MemberType(obj, access->GetMemberNameToken().GetText()));
			if (outer.value) return Result(nullptr);
		}
		const NamespaceQualifiedNameNode* name = dynamic_cast<const NamespaceQualifiedNameNode*>(base);
		if (const auto* qualType = dynamic_cast<const QualTypeNode*>(base)) name = qualType->BaseName().Get();
		QueryResult<const Type*> type;
		if (name) type = m_Ctx.queries().QueryNqnType(name, m_Ctx.unitId, m_Ctx.scope);
		else if (const Core::String path = QualifierPath(base); !path.empty())
			type = m_Ctx.queries().QueryType(path, m_Ctx.unitId, m_Ctx.scope);
		if (!type.Ok() && name && HasTemplateArguments(*name)) return type;
		return Result(type.Ok() && type.value ? TypeHandle::UnwrapToValue(type.value) : nullptr);
	}

	// A type declared in `owner` (its class layer only), or null.
	const Type* ExprLowerer::MemberType(const ObjectType* owner, const Core::String& name) const
	{
		const Scope* scope = m_Ctx.model().TypeScope(owner);
		if (!scope) return nullptr;
		for (const auto& entry : scope->Entries(name))
			if (entry->kind == SymbolKind::Type && entry->type) return TypeHandle::UnwrapToValue(entry->type);
		return nullptr;
	}

	// A constant declared in `owner`'s class layer: a nested unscoped enum's member.
	const SymbolInfo* ExprLowerer::MemberConstant(const ObjectType* owner, const Core::String& name) const
	{
		const Scope* scope = m_Ctx.model().TypeScope(owner);
		if (!scope) return nullptr;
		for (const auto& entry : scope->Entries(name))
			if (entry->kind == SymbolKind::Variable && entry->isConst && !entry->constValue.IsNone()) return entry.get();
		return nullptr;
	}

	QueryResult<Value> ExprLowerer::EnumMemberValue(const EnumType* en, const Core::String& member)
	{
		const EnumType::MemberInfo* found = en->GetMemberByName(member);
		if (!found) return QueryResult<Value>::Fail(QueryError::NotFound, "no member '" + member + "' in the enum '" + QualifiedName(en) + "'");
		return Value::R(m_Ctx.out->NewNode<ConstantNode>(en, ConstantValue::Int(found->m_Value)), en);
	}

	QueryResult<Value> ExprLowerer::StaticFieldValue(const ObjectType* owner, const Core::String& member)
	{
		// `Type::member` names the static data member Type declares or inherits; a folded constant is its value.
		ClassHierarchy::FieldHit hit = ClassHierarchy::FindField(owner, member);
		const SymbolInfo* sym = hit.field && hit.field->m_IsStatic ? StaticMemberSymbol(hit.owner, member) : nullptr;
		if (!sym) return QueryResult<Value>::Fail(QueryError::NotFound, "no static data member '" + member + "' in '" + owner->GetName() + "'");
		if (auto allowed = AccessCheck::CheckField(m_Ctx, hit.owner, *hit.field); !allowed.Ok())
			return QueryResult<Value>::Fail(allowed.error, allowed.message);
		const Type* type = hit.field->m_Type;
		if (sym->isConst && !sym->constValue.IsNone()) return Value::R(m_Ctx.out->NewNode<ConstantNode>(type, sym->constValue), type);
		LValue lv; lv.kind = LValue::Kind::Global; lv.type = type; lv.sym = sym;
		return Value::L(m_Ctx.NewLValue(lv));
	}

	namespace
	{
		// The methods named `name` that `owner` declares or inherits, nearest first.
		void MethodsNamed(const ModelQueries& q, const ObjectType* owner, const Core::String& name, std::vector<const FunctionRecord*>& out)
		{
			for (const ObjectType::MethodInfo& m : owner->GetMethods())
				if (m.mName == name)
					if (const FunctionRecord* rec = q.RecordForMethod(owner, &m)) out.push_back(rec);
			if (!out.empty()) return; // a method hides the bases' of the same name
			for (const ObjectType* base : owner->GetBaseTypes()) MethodsNamed(q, base, name, out);
		}
	}

	QueryResult<Value> ExprLowerer::MethodValue(const AstNode* n, const ObjectType* owner, const Core::String& member)
	{
		std::vector<const FunctionRecord*> methods;
		MethodsNamed(m_Ctx.queries(), owner, member, methods);
		const Core::String named = owner->GetName() + "::" + member;
		if (methods.empty()) return QueryResult<Value>::Fail(QueryError::NotFound, "no method '" + member + "' in '" + owner->GetName() + "'");
		if (methods.size() > 1)
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::Ambiguous, "'" + named + "' names several overloads: a funcptr takes one, "
				"so pass a lambda that calls the one you mean"));
		return DecayedFunction(n, named, *methods.front(), methods.front()->type);
	}

	QueryResult<Value> ExprLowerer::ResolveStaticAccess(const StaticAccessNode* n)
	{
		const Core::String member = n->GetMemberNameToken().GetText();
		const AstNode* base = n->GetBaseType().Get();
		auto qualifier = QualifierType(base);
		if (!qualifier.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(qualifier.error, qualifier.message));
		if (const auto* en = dynamic_cast<const EnumType*>(qualifier.value)) return m_Ctx.Fail(n, EnumMemberValue(en, member));
		if (const auto* owner = dynamic_cast<const ObjectType*>(qualifier.value))
		{
			// A template instance's members are its own; any other type's qualifier may also name a nested enum.
			auto field = StaticFieldValue(owner, member);
			if (field.Ok() || field.error == QueryError::NotVisible) return m_Ctx.Fail(n, field);
			if (const SymbolInfo* constant = MemberConstant(owner, member))
				return Value::R(m_Ctx.out->NewNode<ConstantNode>(constant->type, constant->constValue), constant->type);
			if (auto method = MethodValue(n, owner, member); method.Ok() || method.error != QueryError::NotFound) return method;
			if (!owner->GetTemplateArguments().Empty()) return m_Ctx.Fail(n, field);
		}

		Core::String path = QualifierPath(base);
		path = path.empty() ? member : path + "::" + member;

		auto c = m_Ctx.queries().QueryConstant(path, m_Ctx.unitId, m_Ctx.scope);
		if (c.Ok() && !c.value.IsNone())
		{
			// A compile-time constant must be emitted with its REAL type: `const float CF = 8.5f` folds to a
			// Float64 value but is a FLOAT - emitting it as an Int64-typed constant (ConstantFP on i64) is
			// invalid IR. Ask for the variable's declared type and fall back to Int64 only if unavailable
			// (an enum/template-param constant that has no variable symbol).
		auto vt = m_Ctx.queries().QueryVariable(path, nullptr, m_Ctx.unitId, m_Ctx.enclosingType);
		const Type* ct;
		if (vt.Ok() && vt.value.type) ct = vt.value.type;
		else if (c.value.kind == ConstantValue::Kind::Float64) ct = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Float64);
		else if (c.value.kind == ConstantValue::Kind::Bool) ct = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Bool);
		else
		{
			// A SCOPED enum member (E::X) has no variable symbol, so vt fails and the value is a plain int.
			// Emit it with its ENUM type, not Int64: an enum-typed constant binds to an enum parameter by
			// exact match (ParamCost 0), whereas an Int64 only converts when it equals the enum's underlying
			// type - so `f(E::X)` failed with "no matching overload" while `E k = E::X; f(k);` worked. The
			// qualifier must name an actual enum type (a namespace qualifier like Math::K does not), so this
			// cannot misfire on a namespaced constant. Unscoped members are unaffected - they register as
			// const variables whose declared type is already the EnumType (handled by the vt branch above).
			ct = nullptr;
			size_t colon = path.rfind("::");
			if (colon != Core::String::npos)
			{
				auto tr = m_Ctx.queries().QueryType(path.substr(0, colon), m_Ctx.unitId, m_Ctx.scope);
				if (tr.Ok())
				{
					const Type* tt = TypeHandle::UnwrapToValue(tr.value);
					if (tt && tt->GetKind() == TypeKind::Enum) ct = tt;
				}
			}
			if (!ct) ct = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64);
		}
		return Value::R(m_Ctx.out->NewNode<ConstantNode>(ct, c.value), ct);
		}

		auto variable = m_Ctx.queries().QueryVariable(path, nullptr, m_Ctx.unitId, m_Ctx.enclosingType);
		if (variable.error == QueryError::NotCallable) // the name is no variable: `Ns::Function` decays to its address (BUG-263)
			if (auto function = FunctionValue(n, path); function.Ok() || function.error != QueryError::NotFound) return function;
		auto v = m_Ctx.Fail(n, variable);
		if (!v.Ok()) return QueryResult<Value>::Fail(v.error, v.message);
		if (v.value.symbol)
		{
			LValue lv; lv.kind = LValue::Kind::Global; lv.type = v.value.type; lv.sym = v.value.symbol;
			return Value::L(m_Ctx.NewLValue(lv));
		}
		return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotFound, "static access resolved to nothing: " + path));
	}
} // namespace Scripting
