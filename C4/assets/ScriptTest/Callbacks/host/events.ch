namespace Events
{
	class Bus
	{
	public:
		public Core::Array<func<int32(int32)>> handlers;
		public void Subscribe(func<int32(int32)> handler);
		public int32 Fire(int32 value) const;
	}

	int32 Apply(funcptr<int32(int32)> fn, int32 value);
	func<int32(int32)> MakeAdder(int32 base);
}
