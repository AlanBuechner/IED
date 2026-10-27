// Reflection wrapper (plan 14 redesign): friendly handles over the low-level reflect_* runtime builtins.
// Every entity class (Type, Variable, Function) is a thin void* handle into the reflection registry with
// named accessors. Member queries are HANDLE-BASED: Type.FindField(name) returns a Field handle whose
// TypeOf() resolves the field's type to ANOTHER Type handle - so you can drill from a type to a field to
// its type, exactly as the low-level reflect_field / reflect_field_type_handle builtins do. Groups are
// fully defined by [group("name")] annotations on declarations (no default groups).

namespace Reflection
{
	enum class AnnotationArg : uint32 { None = 0, Int = 1, Float = 2, Bool = 3, String = 4 }

	// What a registered entity is (the runtime's reflect_KIND_* values).
	enum class EntityKind : uint32 { Object = 14, Enum = 15, Union = 16, Variable = 17, Function = 18 }

	// A type's flags (the runtime's reflect_TYPE_* values).
	const uint32 kTypeHoldsPointers = 1u;
	const uint32 kTypeDefaultConstructible = 2u;
	const uint32 kTypeCopyable = 4u;
	const uint32 kTypeAbstract = 8u;

	// Whether the comma-separated `roles` (a method's operator roles) has `role`.
	bool HasRole(const char* roles, const char* role) {
		if (roles == nullptr || role == nullptr) return false;
		const uint64 length = strlen((void*)role);
		for (const char* at = roles; *at != (char)0; ) {
			const char* end = at;
			while (*end != (char)0 && *end != ',') end += 1;
			if ((uint64)(end - at) == length && strncmp((void*)at, (void*)role, length) == 0) return true;
			at = *end == ',' ? end + 1 : end;
		}
		return false;
	}

	// An annotation the compiler does not act on (D-42), such as `[Range(0, 1)]`: its name as written and its
	// arguments, folded at compile time. Every entity handle has AnnotationCount, AnnotationAt and
	// FindAnnotation; a missing name gives an invalid Annotation.
	class Annotation
	{
	public:
		[operator("create")] public void Init(void* h) { handle = h; }
		public bool IsValid() const { return handle != nullptr; }
		public const char* Name() const { return reflect_annotation_name(handle); }
		public int64 ArgCount() const { return (int64)reflect_annotation_arg_count(handle); }
		public AnnotationArg ArgKind(int64 i) const { return (AnnotationArg)reflect_annotation_arg_kind(handle, (uint32)i); }
		public int64 IntArg(int64 i) const { return reflect_annotation_arg_int(handle, (uint32)i); }
		public double FloatArg(int64 i) const { return reflect_annotation_arg_float(handle, (uint32)i); }
		public bool BoolArg(int64 i) const { return reflect_annotation_arg_int(handle, (uint32)i) != 0; }
		public const char* StringArg(int64 i) const { return reflect_annotation_arg_string(handle, (uint32)i); }

	private:
		private void* handle = nullptr;
	}

	template<E> Annotation FindAnnotationIn(const E& entity, const char* name)
	{
		for (int64 i = 0; i < entity.AnnotationCount(); ++i)
		{
			Annotation annotation = entity.AnnotationAt(i);
			if (strcmp(annotation.Name(), name) == 0) return annotation;
		}
		return Annotation(nullptr);
	}

	class Field
	{
	public:
		[operator("create")] public void Init(void* h) { handle = h; }
		public bool IsValid() const { return handle != nullptr; }
		public const char* Name() const { return reflect_field_handle_name(handle); }
		public uint64 Offset() const { return reflect_field_handle_offset(handle); }
		public uint64 SizeOf() const { return reflect_field_handle_size(handle); }
		// The field's type NAME as a string (e.g. "int32", "float"). Primitives are not in the registry,
		// so this is the only way to learn a scalar field's kind - used by the serializers to dispatch.
		public const char* TypeName() const { return reflect_field_handle_type_name(handle); }
		// The field's TYPE as a handle - the user-story drilling (drill from type -> field -> type).
		public Type TypeOf() const { return Type(reflect_field_type_handle(handle)); }
		public bool IsConst() const { return reflect_field_handle_is_const(handle) != 0; }
		// A second view of bytes an earlier field already names: a union member after the first view, a lane's
		// second name (`r` beside `x`) or a vector's whole-lane array (`data`). Serializers skip aliases.
		public bool IsAlias() const { return reflect_field_handle_is_alias(handle) != 0; }

		// The field inside `object`, an instance of the type the field belongs to.
		public void* AddressIn(void* object) const { return (void*)((char*)object + Offset()); }
		public int64 AnnotationCount() const { return (int64)reflect_field_annotation_count(handle); }
		public Annotation AnnotationAt(int64 i) const { return Annotation(reflect_field_annotation_at(handle, (uint32)i)); }
		public Annotation FindAnnotation(const char* name) const { return FindAnnotationIn(*this, name); }
		// Get copies the field of `object` into `out` and Set assigns `value` to it, both through T's own copy;
		// false when T's size is not the field's (Set: or the field is const).
		template<T> public bool Get(void* object, T& out) const
		{
			if (!IsValid() || object == nullptr || (uint64)sizeof(T) != SizeOf()) return false;
			out = *(T*)AddressIn(object);
			return true;
		}
		template<T> public bool Set(void* object, const T& value) const
		{
			if (!IsValid() || object == nullptr || IsConst() || (uint64)sizeof(T) != SizeOf()) return false;
			*(T*)AddressIn(object) = value;
			return true;
		}

	private:
		private void* handle = nullptr;
	}

	// Iterator over a Type's fields for range-for (`for (Field f : t)`). Carries the owner handle and an
	// index; each step yields a fresh Field handle by value (Deref returns an rvalue, not a reference -
	// there is no stable buffer to point into, so a copy of the cheap void* wrapper is the right unit).
	struct FieldIter {
		public void* owner;
		public int64 index;
		[operator("notequal")] public bool Noteq(const FieldIter& o) const { return this.index != o.index; }
		[operator("dereference")] public Field Deref() { return Field(reflect_field_at(owner, (uint32)index)); }
		[operator("increment")] public FieldIter Inc() {
			FieldIter r; r.owner = this.owner; r.index = this.index + 1; return r;
		}
	}

	// Iterator over a Type's methods (`for (Method m : t.Methods())`). Same shape as FieldIter, but each
	// step yields a fresh Method handle by value.
	struct MethodIter {
		public void* owner;
		public int64 index;
		[operator("notequal")] public bool Noteq(const MethodIter& o) const { return this.index != o.index; }
		[operator("dereference")] public Method Deref() { return Method(reflect_method_at(owner, (uint32)index)); }
		[operator("increment")] public MethodIter Inc() {
			MethodIter r; r.owner = this.owner; r.index = this.index + 1; return r;
		}
	}

	// Iterator over a Type's enum values (`for (EnumValue e : t.Enums())`). Same shape, yields EnumValue.
	struct EnumIter {
		public void* owner;
		public int64 index;
		[operator("notequal")] public bool Noteq(const EnumIter& o) const { return this.index != o.index; }
		[operator("dereference")] public EnumValue Deref() { return EnumValue(reflect_enum_at(owner, (uint32)index)); }
		[operator("increment")] public EnumIter Inc() {
			EnumIter r; r.owner = this.owner; r.index = this.index + 1; return r;
		}
	}

	// A view over a Type's methods: the thing you range-for. begin/end bound to the owner handle, so the
	// loop reads straight off it without re-acquiring the type by name each step.
	struct MethodsView {
		public void* owner;
		[operator("begin")] public MethodIter Begin() const { MethodIter it; it.owner = this.owner; it.index = 0; return it; }
		[operator("end")]   public MethodIter End() const { MethodIter it; it.owner = this.owner; it.index = reflect_method_count(this.owner); return it; }
	}

	// A view over a Type's enum values: the thing you range-for.
	struct EnumsView {
		public void* owner;
		[operator("begin")] public EnumIter Begin() const { EnumIter it; it.owner = this.owner; it.index = 0; return it; }
		[operator("end")]   public EnumIter End() const { EnumIter it; it.owner = this.owner; it.index = reflect_enum_count(this.owner); return it; }
	}
	class Param
	{
	public:
		[operator("create")] public void Init(void* h) { handle = h; }
		public bool IsValid() const { return handle != nullptr; }
		public const char* Name() const { return reflect_param_handle_name(handle); }
		public const char* TypeName() const { return reflect_param_handle_type_name(handle); }
		public Type TypeOf() const { return Type(reflect_param_type_handle(handle)); }
		public int64 AnnotationCount() const { return (int64)reflect_param_annotation_count(handle); }
		public Annotation AnnotationAt(int64 i) const { return Annotation(reflect_param_annotation_at(handle, (uint32)i)); }
		public Annotation FindAnnotation(const char* name) const { return FindAnnotationIn(*this, name); }

	private:
		private void* handle = nullptr;
	}

	// One enumerator of a reflected enum. Addressed by index within its type, but every property read
	// goes through this HANDLE (like Field/Method) - no owner+index re-lookup.
	class EnumValue
	{
	public:
		[operator("create")] public void Init(void* h) { handle = h; }
		public bool IsValid() const { return handle != nullptr; }
		public const char* Name() const { return reflect_enum_handle_name(handle); }
		public int64 Value() const { return reflect_enum_handle_value(handle); }

	private:
		private void* handle = nullptr;
	}

	class Method
	{
	public:
		[operator("create")] public void Init(void* h) { handle = h; }
		public bool IsValid() const { return handle != nullptr; }
		public const char* Name() const { return reflect_method_handle_name(handle); }
		public int64 ParamCount() const { return (int64)reflect_method_handle_param_count(handle); }
		public bool IsVirtual() const { return reflect_method_handle_is_virtual(handle) != 0; }
		public bool IsStatic() const { return reflect_method_handle_is_static(handle) != 0; }
		public bool IsConst() const { return reflect_method_handle_is_const(handle) != 0; }
		public Param ParamAt(int64 i) const { return Param(reflect_method_param(handle, (uint32)i)); }
		public const char* ReturnTypeName() const { return reflect_method_handle_return_type_name(handle); }
		public Type ReturnType() const { return Type(reflect_method_return_handle(handle)); }
		// The operators it is, as [operator] names them canonically: "create", "delete", "copy", "assign,copy" for one
		// that is two; "" for a method that is no operator. HasOperatorRole asks for one of them.
		public const char* OperatorRole() const { return reflect_method_handle_operator_role(handle); }
		public bool HasOperatorRole(const char* role) const { return HasRole(OperatorRole(), role); }
		public int64 AnnotationCount() const { return (int64)reflect_method_annotation_count(handle); }
		public Annotation AnnotationAt(int64 i) const { return Annotation(reflect_method_annotation_at(handle, (uint32)i)); }
		public Annotation FindAnnotation(const char* name) const { return FindAnnotationIn(*this, name); }

		// Invoke calls the method on `object` (unused for a static method). `arguments[i]` is the address of
		// argument i, read as parameter i's type; the result is built in `result` (a reference result stores
		// the referent's address there). False when the method cannot be called.
		public bool IsInvokable() const { return reflect_method_is_invokable(handle) != 0; }
		public bool Invoke(void* object, void* result, void** arguments) const
		{
			return reflect_method_invoke(handle, object, result, arguments) != 0;
		}
		// Each argument must have its parameter's exact type; false when the count differs.
		template<...Ts> public bool Call(void* object, void* result, const Ts&... args) const
		{
			if (sizeof...(Ts) != (int64)ParamCount()) return false;
			void*[sizeof...(Ts) + 1] addresses = { (void*)&args..., nullptr };
			return Invoke(object, result, &addresses[0]);
		}

	private:
		private void* handle = nullptr;
	}

	class Type
	{
	public:
		[operator("create")] public void Init(void* h) { handle = h; }

		// type information
		public bool IsValid() const { return handle != nullptr; }
		public const char* Name() const { return reflect_name(handle); }
		// The source tag it registered under: "" for the program's own, a script's name for a script's (docs/runtime/04).
		public const char* Tag() const { return reflect_type_tag(handle); }
		public uint64 SizeOf() const { return reflect_size(handle); }
		public uint64 AlignOf() const { return reflect_align(handle); }
		public bool IsVirtualType() const { return reflect_is_virtual_type(handle) != 0; }
		public EntityKind Kind() const { return (EntityKind)reflect_kind_of(handle); }
		public const char* BaseName() const { return reflect_base_name(handle); }

		// What its lifecycle needs, for storage of a type known only at run time (a pool's chunk): whether a value
		// may hold managed pointers (its storage must be scanned), can start from nothing or be copied, can exist on
		// its own, and whether destroying or copying one runs code.
		public bool HoldsPointers() const { return (reflect_type_flags(handle) & kTypeHoldsPointers) != 0u; }
		public bool IsDefaultConstructible() const { return (reflect_type_flags(handle) & kTypeDefaultConstructible) != 0u; }
		public bool IsCopyable() const { return (reflect_type_flags(handle) & kTypeCopyable) != 0u; }
		public bool IsAbstract() const { return (reflect_type_flags(handle) & kTypeAbstract) != 0u; }
		public bool IsTriviallyDestructible() const { return IsValid() && reflect_type_has_destroy(handle) == 0u; }
		public bool IsTriviallyCopyable() const { return IsCopyable() && reflect_type_has_copy(handle) == 0u; }
		// `new (object) T()`: the storage zeroed, then the type's start; false (nothing written) when it is not
		// default constructible. Destroy runs `destroy(object)`; the storage is then raw memory. CopyTo makes `to`
		// a copy of `from` (`new (to) T(*from)`, `to` zeroed first); false when it cannot be copied. The type's
		// creates, deletes and copies may throw.
		public bool Construct(void* object) const { return reflect_type_construct(handle, object) != 0u; }
		public void Destroy(void* object) const { reflect_type_destroy(handle, object); }
		public bool CopyTo(void* to, const void* from) const { return reflect_type_copy(handle, to, from) != 0u; }

		// fields (handle-based: returns a Field you can query further)
		public int64 FieldCount() const { return (int64)reflect_field_count(handle); }
		public Field FindField(const char* name) const { return Field(reflect_field(handle, name)); }
		public Field FieldAt(int64 i) const { return Field(reflect_field_at(handle, (uint32)i)); }

		// methods (handle-based)
		public int64 MethodCount() const { return (int64)reflect_method_count(handle); }
		public Method FindMethod(const char* name) const { return Method(reflect_method(handle, name)); }
		public Method MethodAt(int64 i) const { return Method(reflect_method_at(handle, (uint32)i)); }

		// enums (handle-based: EnumAt(i) returns an EnumValue you can query further)
		public int64 EnumCount() const { return (int64)reflect_enum_count(handle); }
		public EnumValue EnumAt(int64 i) const { return EnumValue(reflect_enum_at(handle, (uint32)i)); }

		// static data members, registered as variables owned by this type
		public int64 StaticVariableCount() const { return (int64)reflect_type_static_variable_count(handle); }
		public Variable StaticVariableAt(int64 i) const { return Variable(reflect_type_static_variable_at(handle, (uint32)i)); }
		public int64 AnnotationCount() const { return (int64)reflect_type_annotation_count(handle); }
		public Annotation AnnotationAt(int64 i) const { return Annotation(reflect_type_annotation_at(handle, (uint32)i)); }
		public Annotation FindAnnotation(const char* name) const { return FindAnnotationIn(*this, name); }

		// Registry lookup by name (re-enabled: namespaced pointer RETURN types are fixed).
		public Type FindByName(const char* name) const { return Type(reflect_type(name)); }

		// range-for over the type's fields: for (Field f : t) { ... }
		[operator("begin")] public FieldIter Begin() const { FieldIter it; it.owner = handle; it.index = 0; return it; }
		[operator("end")]   public FieldIter End() const { FieldIter it; it.owner = handle; it.index = reflect_field_count(handle); return it; }

		// Views for the other member categories (begin/end cannot be overloaded to yield a different
		// element type from one receiver, so these are separate range-for-able views).
		public MethodsView Methods() const { MethodsView v; v.owner = handle; return v; }
		public EnumsView Enums() const { EnumsView v; v.owner = handle; return v; }

	private:
		private void* handle = nullptr;
	}

	class Variable
	{
	public:
		[operator("create")] public void Init(void* h) { handle = h; }
		public bool IsValid() const { return handle != nullptr; }
		public const char* Name() const { return reflect_variable_handle_name(handle); }
		public const char* Tag() const { return reflect_variable_handle_tag(handle); }
		public const char* TypeName() const { return reflect_variable_handle_type_name(handle); }
		public bool IsConst() const { return reflect_variable_handle_is_const(handle) != 0; }
		// The type a static data member belongs to ("" for a global).
		public const char* OwnerName() const { return reflect_variable_handle_owner(handle); }
		public Type TypeOf() const { return Type(reflect_variable_type_handle(handle)); }
		public void* Address() { return reflect_variable_address(handle); }
		public int64 AnnotationCount() const { return (int64)reflect_variable_annotation_count(handle); }
		public Annotation AnnotationAt(int64 i) const { return Annotation(reflect_variable_annotation_at(handle, (uint32)i)); }
		public Annotation FindAnnotation(const char* name) const { return FindAnnotationIn(*this, name); }

	private:
		private void* handle = nullptr;
	}

	class Function
	{
	public:
		[operator("create")] public void Init(void* h) { handle = h; }
		public bool IsValid() const { return handle != nullptr; }
		public const char* Name() const { return reflect_function_handle_name(handle); }
		public const char* Tag() const { return reflect_function_handle_tag(handle); }
		// The emitted symbol: unique, so each overload is its own Function.
		public const char* Symbol() const { return reflect_function_handle_symbol(handle); }
		// The function's code (nullptr when reflection cannot call it).
		public const void* Address() const { return reflect_function_handle_address(handle); }
		public int64 ParamCount() const { return (int64)reflect_function_handle_param_count(handle); }
		public Param ParamAt(int64 i) const { return Param(reflect_function_param(handle, (uint32)i)); }
		public bool IsStatic() const { return reflect_function_handle_is_static(handle) != 0; }
		// The type a method belongs to ("" for a free function).
		public const char* OwnerName() const { return reflect_function_handle_owner(handle); }
		public const char* ReturnTypeName() const { return reflect_function_handle_return_type_name(handle); }
		public Type ReturnType() const { return Type(reflect_function_return_handle(handle)); }
		public int64 AnnotationCount() const { return (int64)reflect_function_annotation_count(handle); }
		public Annotation AnnotationAt(int64 i) const { return Annotation(reflect_function_annotation_at(handle, (uint32)i)); }
		public Annotation FindAnnotation(const char* name) const { return FindAnnotationIn(*this, name); }

		// Invoke calls a free or static function and InvokeOn a method on `object`, as Method.Invoke does.
		public bool IsInvokable() const { return reflect_function_is_invokable(handle) != 0; }
		public bool Invoke(void* result, void** arguments) const { return InvokeOn(nullptr, result, arguments); }
		public bool InvokeOn(void* object, void* result, void** arguments) const
		{
			return reflect_function_invoke(handle, object, result, arguments) != 0;
		}
		// Each argument must have its parameter's exact type; false when the count differs.
		template<...Ts> public bool Call(void* result, const Ts&... args) const { return CallOn(nullptr, result, args...); }
		template<...Ts> public bool CallOn(void* object, void* result, const Ts&... args) const
		{
			if (sizeof...(Ts) != (int64)ParamCount()) return false;
			void*[sizeof...(Ts) + 1] addresses = { (void*)&args..., nullptr };
			return InvokeOn(object, result, &addresses[0]);
		}

	private:
		private void* handle = nullptr;
	}

	// A group is ACQUIRED ONCE by name (reflect_group); the returned handle is stable, so Count/Item
	// read off it directly without re-acquiring the group by name on every query. Like every other
	// wrapper, this holds an opaque void* handle and reports IsValid()==false for a missing group.
	class Group
	{
	public:
		[operator("create")] public void Init(const char* name) { handle = reflect_group(name); }
		public bool IsValid() const { return handle != nullptr; }
		public int64 Count() const { return (int64)reflect_group_handle_count(handle); }
		public void* Item(int64 i) const { return reflect_group_handle_item(handle, (uint32)i); }
		public EntityKind KindOf(void* item) const { return (EntityKind)reflect_kind_of(item); }

	private:
		private void* handle = nullptr;
	}

	// Registry-level helpers (the registry is global runtime state). Free functions - no `static`/access
	// modifiers at namespace scope; a null handle yields an IsValid()==false wrapper.
	Type FindType(const char* name) { return Type(reflect_type(name)); }
	Variable FindVariable(const char* name) { return Variable(reflect_variable(name)); }
	Function FindFunction(const char* name) { return Function(reflect_function(name)); }
	Type TypeAt(int64 i) { return Type(reflect_type_by_index((int32)i)); }
	Variable VariableAt(int64 i) { return Variable(reflect_variable_by_index((int32)i)); }
	Function FunctionAt(int64 i) { return Function(reflect_function_by_index((int32)i)); }
	// The function whose code holds `address`, such as a return address of GC::AllocSample's frames; invalid when it is
	// no reflected function's.
	Function FunctionContaining(const void* address) { return Function(reflect_function_containing(address)); }
	int64 TypeCount() { return (int64)reflect_count(); }
	int64 VariableCount() { return (int64)reflect_variable_count(); }
	int64 FunctionCount() { return (int64)reflect_function_count(); }
	int64 GroupNameCount() { return (int64)reflect_group_name_count(); }
	const char* GroupNameAt(int64 i) { return reflect_group_name_at((uint32)i); }
	// Removes every entity registered under `tag` (its listeners run first); how many.
	int64 UnloadSource(const char* tag) { return (int64)c4_reflect_unload_source(tag); }
	// `listener(tag, user)` runs when `tag` unloads, while its entities are still registered; "" hears every
	// tag. The returned id removes it.
	uint32 OnUnload(const char* tag, funcptr<void(const char*, void*)> listener, void* user)
	{
		return c4_reflect_on_unload(tag, (void*)listener, user);
	}
	void RemoveUnloadListener(uint32 id) { c4_reflect_remove_listener(id); }
}
