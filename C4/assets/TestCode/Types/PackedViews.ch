// Types - packed views by bare name, with a variable scalar, on a returned vector and in a struct with other
// fields (BUG-181..BUG-184)
struct PvVec2 {
	public union { packed float x, y; packed float r, g; packed float[2] data; };
	public float SumBare() const;
	public void SetBoth(float v);
	public PvVec2 Swapped() const;
	public PvVec2 Times(float s) const;
}

struct PvTagged {
	public union { packed float x, y; };
	public int32 tag;
}

struct PvHeaded {
	public int32 head;
	public union { packed float x, y, z, w; packed float[4] data; };
	public int8 tail;
}

template<T> struct PvBox {
	public T before;
	public union { packed T x, y, z; packed T[3] lanes; };
	public T Total() const { return lanes[0] + lanes[1] + lanes[2] + before; }
}

void TestPackedViews();
