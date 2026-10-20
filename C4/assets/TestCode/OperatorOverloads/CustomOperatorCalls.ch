// OperatorOverloads - Custom operator call test declarations
// Verifies `obj.operator_<name>(...)` resolves to the method annotated [operator("<name>")],
// for both regular member methods and extension methods, with and without parameters.

void TestCustomOperatorMemberMethod();
void TestCustomOperatorWithParams();
void TestCustomOperatorExtensionMethod();
void TestCustomOperatorBuiltInStillWorks();
