// Types - Object Field Zero-Initialization Regression tests declarations
// A bare object declaration must leave every field initialized (ints 0, pointers null) on ALL
// construction paths. Before the fix, codegen stored a scalar `i32 0` into an aggregate slot,
// zeroing only the leading bytes and leaving pointer fields as garbage stack contents (UB).
struct ZS {
    int32 a;
    char* p;
};

void TestObjectZeroInitLocal();
void TestObjectZeroInitReturn();
void TestObjectZeroInitCopyAndGlobal();
void TestObjectExplicitInitializerWins();
