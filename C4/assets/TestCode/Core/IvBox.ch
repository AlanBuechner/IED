// A template whose instances name IvTag: this header includes what its body names, so a user includes only it.
include "IvTag.ch"

template<T> struct IvBox {
	public T held;
	public IvTag tag;
	public int32 Scaled();
	public int32 Tagged() { return (int32)tag.value + 1; }
}

// Declared here, defined in IvBox.cf.
template<T> T IvTwice(T v);
