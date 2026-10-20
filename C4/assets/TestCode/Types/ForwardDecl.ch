// ForwardDecl - class/struct forward declarations (`class X;`), the C++ idiom used to break
// header cycles and shrink includes. Regression lock: before this fix the parser rejected a
// declaration-only type with "Expected an opening brace for the type body, got ';'". Now a `;`
// after a NAMED type (no inheritance list) is a forward declaration - it introduces the name as an
// INCOMPLETE type so pointers/references to it and a later definition both work.

// Case 1: incomplete type, pointer-only use (NO definition exists anywhere).
class Window;

struct WindowRef {
    Window* handle;   // pointer to an incomplete type is valid
    int32 id;
};

// Case 2: fwd-decl BEFORE its definition in the same unit. A pointer field to it is fine; the later
// full definition (with methods) resolves to the SAME type, so method calls work through the name.
class Engine;

struct EngineConfig {
    Engine* engine;
    int32 count;
};

class Engine {
    public static int32 liveCount;
    public int32 id;
    public void SetId(int32 v) { id = v; }
    public int32 GetId() { return id; }
};

// Case 3: redundant fwd-decl AFTER a full definition is NOT a "Duplicate type" error.
class Done {
    public int32 x;
};
class Done;   // legal no-op re-declaration

void TestForwardDeclPointerOnly();
void TestForwardDeclDefinedLater();
void TestForwardDeclRedundantAfterDef();
