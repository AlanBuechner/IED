// CharPtrAssign - cross-unit char* assignability regression.
// A struct with a char* field is declared in this shared header and included by two separate
// .cf entry units (cp1.cf, cp2.cf), both living in Types/ so each is its own compilation unit.
// Before canonical primitive interning, each unit registered its OWN `char` instance, so the
// deduped struct field's char* (over one unit's char) did not equal a char* built in the other
// unit -> "Cannot assign value of type 'char*' to 'char*'". Now every unit shares ONE canonical
// char, so the assignment is accepted and round-trips.
struct CP {
    char* p;
};

void TestCharPtrAssignUnit1();
void TestCharPtrAssignUnit2();
