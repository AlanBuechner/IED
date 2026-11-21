// Uses Vec of z_def.ch, which sorts after this file: the program model registers every declaration before it
// resolves any, so the order the files are met in never matters. (A header that left the include out and leaned on
// its includers' include order is NegativeTest HeaderLeansOnItsIncluder: a declaration sees what its file includes.)
include "z_def.ch"

class User {
public:
    public Vec v;
    public int32 Sum() { return v.x + v.y; }
}
int32 MakeUser();
