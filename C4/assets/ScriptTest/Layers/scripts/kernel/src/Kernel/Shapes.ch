// A module as a package holds one: its header names what scripts over it use. Its macros reach them too.
include "world.ch"

#define KERNEL_TWICE(x)((x) * 2)

namespace Kernel
{
	class Oops
	{
	public:
		public int32 code;
		[operator("create")] public void Init(int32 c) { code = c; }
	}

	class Square : public World::Entity
	{
	public:
		public int32 side = 1;
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int32 s) { side = s; }
		public override int32 Kind() const { return 1; }
		public virtual int32 Area() const;
		// The layer's own call of a virtual method, which reaches an override over it.
		public int32 AreaPlus(int32 extra) const;
		public int32 Doubled() const { return side * 2; }
	}

	template<typename T> class Box
	{
	public:
		public Core::Array<T> items;
		public void Put(const T& item) { items.Push(item); }
		public int64 Count() const { return items.Count(); }
	}

	// A helper of this file, which a template of it names: a script over it instantiates the template and calls this.
	static int32 Helper(int32 x) { return x + 1; }

	template<typename T> int32 UsesHelper(const T& value) { return Helper((int32)value); }

	int32 g_Counter = 4;
	thread_local int32 t_Slot = 9;

	int32 BumpCounter();
	int32 Version();
	void ThrowOops(int32 code);
}
