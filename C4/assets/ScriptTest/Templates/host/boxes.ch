namespace Store
{
	template<T> class Box
	{
	public:
		public T value;
		public static int32 s_made = 0;
		[operator("create")] public void Init() { s_made++; }
		public T Get() const { return value; }
		public void Set(T v) { value = v; }
	}

	template<T> T Largest(T a, T b) { return a > b ? a : b; }

	void MakeHostBox();
	int32 HostBoxesMade();
	int32 HostLargest(int32 a, int32 b);
}
