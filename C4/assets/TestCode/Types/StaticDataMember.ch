// StaticDataMember - a class with `static` DATA members (not methods).
// Regression lock: before this fix, a static data member leaked into the object LAYOUT (so
// `Tally t;` had a phantom field at offset 0) and every access form lowered to something
// different - bare read in a method hit one global, bare write another (`__static_` guard), and
// qualified `Tally.total` was a field access at offset 0 on the (nonexistent) type value -> AV.
// Now a static data member IS a module global named <Type>_<field>; all access forms (bare read,
// bare write, compound `+=`, qualified dot read, qualified dot write) converge on that ONE global.
class Tally {
    public static int32 total;
    public static int32 other;
    public void Inc() { total = total + 1; }      // bare read+write in a method
    public void Add(int32 n) { total += n; }       // bare compound in a method
    public int32 Get() { return total; }           // bare read, returned by value
};

void TestStaticDataMember();
void TestStaticDataMemberInitializer();
void TestStaticMethod();
