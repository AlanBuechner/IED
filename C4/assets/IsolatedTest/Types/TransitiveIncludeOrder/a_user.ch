// Uses Vec WITHOUT including z_def.ch: it relies on every includer pulling z_def.ch in FIRST,
// exactly like a header that leans on its includer's umbrella include order. The program model
// must preserve that include-before-use ordering across the whole program.
class User {
public:
    public Vec v;
    public int32 Sum() { return v.x + v.y; }
}
int32 MakeUser();
