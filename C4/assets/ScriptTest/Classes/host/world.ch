namespace World
{
	interface IUpdatable
	{
		int32 Update(int32 dt);
	}

	class Entity
	{
	public:
		public int32 health = 10;
		public virtual int32 Damage(int32 amount) { health -= amount; return health; }
		public virtual int32 Kind() const { return 0; }
		// The host's own call of a virtual method, which reaches a script's override.
		public int32 Hit(int32 amount) { return Damage(amount); }
	}

	class Registry
	{
	public:
		public Core::Array<Entity*> entities;
		public Core::Array<IUpdatable*> updatables;
		public void Add(Entity* entity);
		public void AddUpdatable(IUpdatable* updatable);
		public int32 TotalKind() const;
		public int32 UpdateAll(int32 dt) const;
	}

	Entity* MakeEntity();
	bool IsUpdatable(Entity* entity);
}
