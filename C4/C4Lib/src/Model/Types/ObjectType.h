#pragma once
#include "Model/Types/Type.h"
#include "Model/ConstantValue.h"
#include "Model/ModelWrites.h"
#include "Parser/AstNode.h" // AccessModifier (member visibility)
#include <unordered_map>
#include <vector>

namespace Scripting
{
	class SwizzleType; // forward: packed SIMD region storage

	/// User-defined object type (class, struct, interface).
	class ObjectType : public Type
	{
	public:
		static constexpr TypeKind kTypeKind = TypeKind::Object;
		enum class ObjectKind { Class, Struct, Interface };

		struct FieldInfo
		{
			Core::String mName;
			const Type* m_Type = nullptr;
			size_t m_Offset = 0;
			bool m_IsStatic = false;
			bool m_IsConst = false;
			bool m_IsVolatile = false; // every load and store of it is volatile (D-29)
			// A non-static field's default initializer (`int32 x = 7;`): every construction of the object starts
			// the field from it (docs/language/05).
			const AstNode* m_Initializer = nullptr;
			// Anonymous-union (swizzle) region membership. Members of one anonymous union share a
			// single byte offset (the region base); only the first member triggers alignment and
			// only the last advances the layout, by the whole region size. m_Offset = base + intra.
			bool m_InUnionRegion = false;
			bool m_UnionRegionFirst = false;
			bool m_UnionRegionLast = false;
			// True when this region member is a flattened struct VIEW (sequential intra offsets);
			// false for a plain anonymous-union member (all alias offset 0). Lets the RESOLVE pass
			// recompute a region's size correctly when a member is a forward object type.
			int m_UnionViewId = -1; // view within the region: -1 = plain member (offset 0), >=0 = flattened struct view
			size_t m_UnionRegionAlign = 0; // strictest member alignment (first member)
			size_t m_UnionRegionSize = 0;  // padded region size (last member)
			size_t m_UnionIntraOffset = 0; // offset of this member within the region
			// Unique id per anonymous-union region (-1 for non-region fields). Members of one
			// region share a byte range; distinct regions (even adjacent) do not alias each other.
			int m_UnionRegionId = -1;
			// LLVM struct slot this field maps to (base-first order, statics excluded). Non-swizzle
			// fields get a unique sequential slot; swizzle aliases map to the same slot as the first
			// member at their intra-region offset. Computed by CalculateLayout.
			int m_SlotIndex = -1;
			AccessModifier m_Access = AccessModifier::Public; // visibility (section-resolved)
			const AstNode* m_DeclaringNode = nullptr;         // the VariableDefinitionNode (for LSP/DAP)
			// The storage of a packed lane region (`union { packed float x, y; }`): the region's SIMD vector,
			// laid out like any field, which the lanes, runs and array view read. Not a field a program names.
			bool m_IsPackedStorage = false;
		};

		struct MethodInfo
		{
			Core::String mName;
			const Type* m_ReturnType = nullptr;
			Core::Vector<const Type*> m_ParameterTypes;
			bool m_IsStatic = false;
			bool m_IsVirtual = false;
			bool m_IsConst = false;
			bool m_IsOverride = false;
			bool m_IsFinal = false;            // no derived type may override it (D-29)
			bool m_IsExtensionMethod = false;  // true if this method is an extension method
			// Every canonical [operator(...)] name of the method ("add", "index", "create", ...).
			Core::Vector<Core::String> m_OperatorAnnotations;
			bool HasOperator(const Core::String& annotation) const
			{
				for (const Core::String& a : m_OperatorAnnotations)
					if (a == annotation) return true;
				return false;
			}
			AccessModifier m_Access = AccessModifier::Public; // visibility (section-resolved)
			const AstNode* m_DeclaringNode = nullptr;         // the FunctionDefinitionNode (for LSP/DAP)
			bool m_HasBody = false;                           // some declaration/definition of this method has a body
		};

		struct PackedViewRegion
		{
			const SwizzleType* m_Swizzle = nullptr; // SIMD <N x T> storage for this region
			Core::String m_FieldName;               // synthetic object field that stores the vector
		};

		ObjectType(TypeKind kind, const Core::String& name);
		Core::String GetName() const override;
		Core::String ToString() const override { return GetName(); }

		ObjectKind GetObjectKind() const { return m_ObjectKind; }
		const Core::String& GetFullName() const { return m_FullName; }

		void SetObjectKind(ObjectKind kind) { m_ObjectKind = kind; }
		void SetFullName(const Core::String& name) { m_FullName = name; }

		/// What names the type at run time (RTTI, exception matching): the namespaces and classes it is declared
		/// in, outermost first, and for a template instance its arguments (a null type is a value argument).
		struct TemplateArgument { const Type* type = nullptr; ConstantValue value; }; // a type, or a value when type is null
		const Core::Vector<Core::String>& GetEnclosingNames() const { return m_EnclosingNames; }
		void SetEnclosingNames(const Core::Vector<Core::String>& names) { m_EnclosingNames = names; }
		const Core::Vector<TemplateArgument>& GetTemplateArguments() const { return m_TemplateArguments; }
		void SetTemplateArguments(const Core::Vector<TemplateArgument>& args) { m_TemplateArguments = args; }

		const Core::Vector<FieldInfo>& GetFields() const { return m_Fields; }
		Core::Vector<FieldInfo>& GetFieldsMutable() { return m_Fields; }

		void AddPackedView(const SwizzleType* swizzle, const Core::String& fieldName)
		{
			PackedViewRegion r; r.m_Swizzle = swizzle; r.m_FieldName = fieldName; m_PackedViews.Push(r);
		}
		const Core::Vector<PackedViewRegion>& GetPackedViews() const { return m_PackedViews; }
		// The SIMD region a lane name belongs to (nullptr if the name is not a packed lane).
		const PackedViewRegion* FindPackedLane(const Core::String& laneName) const;
		// The SIMD region whose whole-region array alias matches (nullptr if none).
		const PackedViewRegion* FindPackedArrayAlias(const Core::String& name) const;
		const Core::Vector<MethodInfo>& GetMethods() const { return m_Methods; }

		void AddField(const FieldInfo& field) { Asking("a field"); m_Fields.Push(field); m_LaidOut = false; }
		// A packed lane region (`union { packed float x, y; }` at `declared`) and the field that stores it, placed
		// after the fields written before it (fields resolve in any order, so it goes in by source position).
		void AddPackedRegion(const SwizzleType* region, const AstNode* declared);
		// The storage field of a packed region (null for a region without one).
		const FieldInfo* PackedStorage(const PackedViewRegion& region) const;
		void AddMethod(const MethodInfo& method)
		{
			Asking("a method");
			m_MethodsNamed[method.mName].Push(m_Methods.Count());
			for (const Core::String& annotation : method.m_OperatorAnnotations) m_MethodsWithOperator[annotation].Push(m_Methods.Count());
			m_DeclaresVirtual = m_DeclaresVirtual || (!method.m_IsStatic && (method.m_IsVirtual || method.m_IsOverride));
			m_Methods.Push(method);
		}
		/// The indices into GetMethods of the methods named `name`, in order; null when none is: a call's lookup reads
		/// these instead of every method of every class it passes.
		const Core::Vector<uint32>* MethodsNamed(const Core::String& name) const
		{
			const auto named = m_MethodsNamed.find(name);
			return named == m_MethodsNamed.end() ? nullptr : &named->second;
		}
		/// The same for the methods an [operator("...")] annotation names (`+`, `==`, `copy`): null when none does.
		const Core::Vector<uint32>* MethodsWithOperator(const Core::String& annotation) const
		{
			const auto found = m_MethodsWithOperator.find(annotation);
			return found == m_MethodsWithOperator.end() ? nullptr : &found->second;
		}

		/// Size/alignment, lazily computed. Once the type is fully defined and every non-static field
		/// has a resolved type, GetSize computes the layout on demand (memoized in m_LaidOut). This makes
		/// sizeof(T) correct at ANY point after T's own fields are known - not only after the end-of-pass-2
		/// FinalizeLayouts sweep - so a struct field whose array size is `sizeof(OtherType)` resolves even
		/// when OtherType is defined later in the same pass. A type still being laid out (m_LayingOut, a
		/// by-value cycle) or not fully resolvable yet returns its current (possibly 0) size.
		size_t GetSize() const override;
		size_t GetAlignment() const override { const_cast<ObjectType*>(this)->EnsureLayout(); return m_Alignment; }

		/// Get a field by name. Returns nullptr if not found.
		const FieldInfo* GetFieldByName(const Core::String& name) const;

		/// Find a method by name, most-derived first: own methods, then inherited (bases in order),
		/// then extension methods. Mirrors GetFieldByName's hierarchy walk so member lookup has ONE
		/// rule for both fields and methods. Returns the FIRST (most-derived) method with that name -
		/// overload selection is a separate step (ResolveOverload). nullptr when no such method exists.
		const MethodInfo* FindMethodByName(const Core::String& name) const;

		/// The absolute byte offset of a field (own or inherited) within this type, or -1 when no such field.
		int64 GetFieldOffset(const Core::String& name) const;

		/// True when an object of this type carries a vptr: it declares or inherits a virtual method, or it is
		/// an interface (interfaces are always polymorphic).
		bool IsPolymorphic() const;
		bool IsInterface() const { return m_ObjectKind == ObjectKind::Interface; }

		/// MSVC layout facts (valid once the layout is computed): the byte offset of each direct base in
		/// declaration order, the primary base (the first polymorphic base, which shares this type's vptr at
		/// offset 0), and whether this type introduces its own vptr at offset 0 (polymorphic, no polymorphic base).
		size_t GetBaseOffset(uint32 baseIndex) const
		{
			const_cast<ObjectType*>(this)->EnsureLayout();
			return baseIndex < m_BaseOffsets.Count() ? m_BaseOffsets[baseIndex] : 0;
		}
		const ObjectType* GetPrimaryBase() const { const_cast<ObjectType*>(this)->EnsureLayout(); return m_PrimaryBase; }
		bool HasOwnVptr() const { const_cast<ObjectType*>(this)->EnsureLayout(); return m_HasOwnVptr; }

		/// Get the number of methods.
		uint32 GetMethodCount() const { return m_Methods.Count(); }

		const Core::Vector<const ObjectType*>& GetBaseTypes() const { return m_BaseTypes; }
		void AddBaseType(const ObjectType* base) { Asking("a base"); m_BaseTypes.Push(base); }

		/// Extension methods registered for this type (from `extends` functions).
		const Core::Vector<MethodInfo>& GetExtensionMethods() const { return m_ExtensionMethods; }
		void AddExtensionMethod(const MethodInfo& method) { Asking("an extension method"); m_ExtensionMethods.Push(method); }

		// True when this object's ONLY storage is a single packed SIMD region (no base, no vptr,
		// every non-static field belongs to that one region). Such an object IS a vector: it lowers
		// as the bare <N x T> and passes in XMM registers, exactly like the standalone `swizzle`
		// form - so the two representations collapse into one concept (the class only adds methods).
		bool IsPurePackedVector() const;
		// The single packed region when IsPurePackedVector() is true, else nullptr.
		const PackedViewRegion* GetPackedVectorRegion() const { return IsPurePackedVector() ? &m_PackedViews[0] : nullptr; }
		// The SwizzleType behind this object: the region's SIMD <N x T> when it IS a pure packed
		// vector, else nullptr. Single place to answer "does this object lower as a swizzle vector?".
		const SwizzleType* GetPackedVectorSwizzle() const { auto r = GetPackedVectorRegion(); return r ? r->m_Swizzle : nullptr; }

		uint32 GetTemplateParameterCount() const { return m_TemplateParamCount; }
		void SetTemplateParameterCount(uint32 count) { m_TemplateParamCount = count; }

		bool IsAbstract() const { return m_IsAbstract; }
		void SetAbstract(bool v) { m_IsAbstract = v; }
		bool IsFinal() const { return m_IsFinal; }
		void SetFinal(bool v) { m_IsFinal = v; }
		/// The anonymous struct of a return list `[float, bool]` (D-34, Model/Types/ReturnLists).
		bool IsReturnList() const { return m_IsReturnList; }
		void SetReturnList(bool v) { m_IsReturnList = v; }

		bool IsComplete() const { return m_IsComplete; }
		void SetComplete(bool v) { m_IsComplete = v; }

		// True once the type's definition body has been fully processed (all fields/bases recorded).
		// A shell registered by the COLLECT pre-pass is not defined until its body is walked, so
		// constructs that need a complete size mid-walk (sizeof, union/swizzle regions) can tell a
		// forward reference apart from a usable type instead of silently sizing it as zero.
		bool IsDefined() const { return m_IsDefined; }
		void SetDefined(bool v) { m_IsDefined = v; }

		// Fields, bases and packed regions the resolver has queued and not attached yet: until they are, the type's
		// size and contents are not known, so a constant that measures it waits (BUG-393).
		bool HasPendingParts() const { return m_PendingParts != 0; }
		void AwaitPart() { ++m_PendingParts; }
		void AttachPendingPart() { --m_PendingParts; }

		// Layout finalization state for the order-independent RESOLVE pass. m_LaidOut means the
		// size/offsets are computed (memo); m_LayingOut marks a type currently being laid out so a
		// by-value dependency cycle (infinite size) is detected instead of recursing forever.
		bool IsLaidOut() const { return m_LaidOut; }
		void MarkLaidOut() { m_LaidOut = true; }
		bool IsLayingOut() const { return m_LayingOut; }
		void SetLayingOut(bool v) { m_LayingOut = v; }

		/// Where the type is declared, or nowhere for one the compiler makes.
		SourceLocation GetDeclLocation() const;

		/// The AST node that defines this type. Lets the main walk tell "reuse my own COLLECT shell"
		/// apart from a genuine same-name duplicate definition (which must still error).
		const AstNode* GetDeclNode() const { return m_DeclNode; }
		void SetDeclNode(const AstNode* node) { m_DeclNode = node; }

		/// [group("name")] annotations on the declaration (reflection, plan 14). Empty = no group.
		const Core::Vector<Core::String>& GetGroups() const { return m_Groups; }
		void SetGroups(const Core::Vector<Core::String>& g) { m_Groups = g; }

		void CalculateLayout();

		/// The model's writes, asked before this type changes from when the model owns it (TypeStore::Own): its
		/// members, bases and layout are read by lowering on several threads while the model is frozen.
		void WatchWrites(const ModelWrites* writes) { m_Writes = writes; }

		// Force this object's layout to be computed now (post-order over bases) so GetSize/GetAlignment
		// return real values. No-op when the type is not yet fully defined or is already laid out. Used by
		// sizeof() evaluation so a `sizeof(T)` in an array size folds to T's true size even before the
		// end-of-pass-2 FinalizeLayouts sweep has run.
		void EnsureLayout();

		/// COM GUID parts (from a [uuid("...")] attribute). Layout matches the Win32 _GUID:
		/// Data1 (4 bytes), Data2/Data3 (2 bytes each), Data4 (8 bytes).
		struct GuidParts
		{
			uint32_t m_Data1 = 0;
			uint16_t m_Data2 = 0;
			uint16_t m_Data3 = 0;
			uint8_t m_Data4[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
		};

		bool HasGuid() const { return m_HasGuid; }
		const GuidParts& GetGuid() const { return m_Guid; }
		void SetGuid(const GuidParts& guid) { m_Guid = guid; m_HasGuid = true; }

	private:
		void LayOutUnionRegions();
		void Asking(const char* what) const
		{
			if (m_Writes) m_Writes->Ask(what);
		}

		ObjectKind m_ObjectKind = ObjectKind::Class;
		Core::String m_FullName;
		Core::Vector<Core::String> m_EnclosingNames;
		Core::Vector<TemplateArgument> m_TemplateArguments;
		Core::Vector<FieldInfo> m_Fields;
		// Packed SIMD lane views (`packed T x, y;` inside a class/struct): each region is one <N x T>
		// vector stored in a synthetic field; lane names (including aliases across views) map to it.
		Core::Vector<PackedViewRegion> m_PackedViews;
		// Anonymous-union regions (swizzles). Each region covers a contiguous byte range at one
		// offset; its member fields are flattened into m_Fields sharing that offset.
		struct UnionRegion { size_t Offset = 0; size_t Size = 0; size_t Alignment = 1; };
		Core::Vector<UnionRegion> m_UnionRegions;
		Core::Vector<MethodInfo> m_Methods;
		std::unordered_map<Core::String, Core::Vector<uint32>> m_MethodsNamed; // MethodsNamed's, kept by AddMethod
		std::unordered_map<Core::String, Core::Vector<uint32>> m_MethodsWithOperator; // MethodsWithOperator's, kept by AddMethod
		bool m_DeclaresVirtual = false; // a method of its own is virtual or overrides (IsPolymorphic), kept by AddMethod
		Core::Vector<const ObjectType*> m_BaseTypes;
		Core::Vector<size_t> m_BaseOffsets;        // parallel to m_BaseTypes, set by CalculateLayout
		const ObjectType* m_PrimaryBase = nullptr;
		bool m_HasOwnVptr = false;
		Core::Vector<MethodInfo> m_ExtensionMethods;  // extension methods targeting this type
		uint32 m_TemplateParamCount = 0;
		bool m_IsAbstract = false;
		bool m_IsFinal = false;
		bool m_IsReturnList = false;
		bool m_IsComplete = false;
		bool m_IsDefined = false;
		uint32 m_PendingParts = 0;
		// Layout memo + in-progress guard. Mutable because GetSize/GetAlignment are const and lazily
		// compute the layout on demand (post-order over bases) so sizeof(T) is correct as soon as T's
		// own fields are resolved - not only after the end-of-pass-2 FinalizeLayouts sweep.
		mutable bool m_LaidOut = false;
		mutable bool m_LayingOut = false;
		const ModelWrites* m_Writes = nullptr;
		const AstNode* m_DeclNode = nullptr;
		Core::Vector<Core::String> m_Groups; // [group("name")] annotations (reflection, plan 14)
		GuidParts m_Guid;
		bool m_HasGuid = false;
	};
} // namespace Scripting
