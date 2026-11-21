struct Counter
{
	public int32 count;
	public void Add(int32 n)
	{
		if (n > 0)
		{
			count = count + n;
	}
	public int32 Get() { return count; }
}

struct Size
{
	public int32 w;
	public int32 h;
}
