#pragma once
#include "Model/Types.h"
#include "Core/Core.h"
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Scripting
{
	/// Owns every Type* in the program (doc 03 ownership invariant): builtins, user types, and
	/// wrapper types. A phase never constructs a type - it asks the model, which asks this store.
	/// Pointers are stable for the whole compilation because this store owns them; callers may
	/// hold them anywhere (HIR nodes, LSP caches) with no lifetime reasoning at call sites.
	class TypeStore
	{
	public:
		TypeStore();

		// --- Builtins (interned singletons per kind) -----------------------------------
		/// The canonical primitive for a kind: the same instance every time (pointer identity is
		/// "exact match" for overload ranking and type comparison).
		const Type* Builtin(TypeKind kind);
		/// Resolve a builtin name ("int32", "float", ...) to its type, or nullptr.
		const Type* FindBuiltin(const Core::String& name) const;
		/// A name the language gives a built-in type (`int32`, `float`, `int`, `va_list`), known without a store.
		static bool IsBuiltinName(const Core::String& name);

		// --- User types (owned, returned by pointer) ------------------------------------
		/// Take ownership of a user-defined type and return the stable pointer to it.
		template <typename T>
		const T* Own(T* type)
		{
			m_Owned.Push(std::unique_ptr<Type>(type));
			return type;
		}

		// --- Wrapper interning (one canonical instance per logical type) -----------------
		/// `const T*`, `T&`, `T[N]`, and funcptr types are interned: a logical type is ONE
		/// canonical instance within this program, so pointer identity compares them correctly.
		const Type* Pointer(const Type* pointee, bool constReferent = false);
		const Type* Reference(const Type* referent, bool constReferent = false);
		const Type* Array(const Type* element, uint64 count);
		/// A packed SIMD sub-vector `<laneCount x element>` (multi-lane swizzle result: v.xz). One
		/// canonical instance per (element, width) so two same-width sub-vectors compare equal by
		/// pointer identity and no spurious ConvertNode is inserted between them.
		const Type* Swizzle(const Type* element, uint32 laneCount);
		const Type* FunPtr(const Type* returnType, const Core::Vector<const Type*>& paramTypes);
		/// `func<R(A)>`, one canonical instance per signature like FunPtr.
		const Type* Func(const Type* returnType, const Core::Vector<const Type*>& paramTypes);
		/// `__init_list<T>`: a built-in alias template (doc 05 - it is just a type). One canonical
		/// instance per element type; the layout is fixed for all T.
		const Type* InitList(const Type* elementType);
		/// A braced argument of `entries` entries, for overload resolution (D-57). One per count.
		const Type* BracedList(uint32 entries);

		/// Intern a FREE-FUNCTION signature: identical resolved signatures (same return type and
		/// parameter types by pointer identity) yield ONE canonical instance. This is what makes a
		/// forward declaration and its definition the same symbol - the builder interns both, sees
		/// one pointer, and attaches the body instead of adding a duplicate overload. Method and
		/// template-instantiation signatures are NOT interned: two instantiations may share a
		/// spelling but must stay distinct records (their bodies differ).
		const FunctionType* Function(const Type* returnType, const Core::Vector<const Type*>& paramTypes, bool variadic = false);

		// --- Layout (order-independent fixed point, doc 02 pass 2) ------------------------
		/// Compute size/alignment for every object type to a fixpoint: forward references resolve
		/// because every shell exists before this runs. Safe to call repeatedly (memoized).
		void FinalizeLayouts();

		uint32 Count() const { return static_cast<uint32>(m_Owned.Count()); }
		const Core::Vector<std::unique_ptr<Type>>& All() const { return m_Owned; }

	private:
		Core::Vector<PrimitiveType*> m_Builtins; // one per TypeKind index, nullptr until made
		std::unordered_map<Core::String, const Type*> m_BuiltinByName;
		Core::Vector<std::unique_ptr<Type>> m_Owned;
		// What a signature is interned by: what it returns (a funcptr's and a func's by name), its parameters and
		// whether it is variadic.
		template <typename Returned>
		struct SignatureKey
		{
			Returned returned;
			std::vector<const Type*> params;
			bool variadic = false;
			bool operator==(const SignatureKey& other) const
			{
				return returned == other.returned && params == other.params && variadic == other.variadic;
			}
		};

		struct KeyHash
		{
			static size_t Combine(size_t seed, size_t value) { return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2)); }

			template <typename A, typename B>
			size_t operator()(const std::pair<A, B>& key) const { return Combine(std::hash<A>{}(key.first), std::hash<B>{}(key.second)); }

			template <typename Returned>
			size_t operator()(const SignatureKey<Returned>& key) const
			{
				size_t seed = Combine(std::hash<Returned>{}(key.returned), key.variadic ? 1 : 0);
				for (const Type* param : key.params) seed = Combine(seed, std::hash<const Type*>{}(param));
				return seed;
			}
		};

		template <typename Returned>
		static SignatureKey<Returned> KeyOf(Returned returned, const Core::Vector<const Type*>& params, bool variadic)
		{
			return { std::move(returned), std::vector<const Type*>(params.begin(), params.end()), variadic };
		}

		std::unordered_map<std::pair<const Type*, bool>, const Type*, KeyHash> m_PointerCache;
		std::unordered_map<std::pair<const Type*, bool>, const Type*, KeyHash> m_ReferenceCache;
		std::unordered_map<std::pair<const Type*, uint64>, const Type*, KeyHash> m_ArrayCache;
		std::map<std::pair<const Type*, uint32>, const Type*> m_SwizzleCache; // (element, laneCount) -> <N x elem>
		std::map<const Type*, const Type*> m_InitListCache; // element type -> __init_list<T>
		std::map<uint32, const Type*> m_BracedListCache;    // entry count -> {N entries}
		std::unordered_map<SignatureKey<Core::String>, const Type*, KeyHash> m_FunPtrCache;
		std::unordered_map<SignatureKey<Core::String>, const Type*, KeyHash> m_FuncCache;
		std::unordered_map<SignatureKey<const Type*>, const FunctionType*, KeyHash> m_FunctionCache; // every function's signature
	};

} // namespace Scripting
