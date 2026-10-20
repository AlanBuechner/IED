namespace Canvas
{
	class Shape
	{
	public:
		public virtual int32 Area() const { return 0; }
	}

	int32 TotalArea(Shape** shapes, int32 count);
}
