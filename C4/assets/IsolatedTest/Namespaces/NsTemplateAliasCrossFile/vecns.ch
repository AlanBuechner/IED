// Regression lock for the RESOLVED "namespaced template alias / static fn collapse" bug (WORK.md t193/t195).
// A namespaced template class with (a) a method returning its template param by value and (b) a method
// taking a T param, plus an in-namespace alias and an in-namespace free function, must all resolve from a
// separate consuming file. Before the fix the consumer saw "Unknown type 'Math::V3F'" / "Undefined
// identifier 'Math'". Array fields use C4's type-first syntax (T[3] data).

namespace Math {
	template<T> class TVector3 {
		public T[3] data;
		public T Get(int32 i) { return this.data[i]; }        // returns T by value
		public bool Has(int32 i, T v) { return this.data[i] == v; }  // takes a T param
	};
	alias V3F = TVector3<float>;
	float DotV3(V3F a, V3F b);
}
