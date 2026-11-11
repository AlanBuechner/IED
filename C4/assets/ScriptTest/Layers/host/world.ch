namespace World
{
	class Entity
	{
	public:
		public virtual int32 Kind() const { return 0; }
	}

	int32 HostValue();
}
