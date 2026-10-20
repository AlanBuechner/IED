// CharPtr - the language has no built-in string type: a "string" is a C-style char* (null-terminated).
// Regression lock for task #1 (built-in `string` removed): a string literal is a char*, so it can be
// assigned to char*, returned as char*, passed to a char* parameter, and handed to the C library
// (strlen/strcmp/printf) with no conversion. The default-library Str/StringBuilder wrap this for
// richer use; see Bedrock str.ch / stringbuilder.ch.

void TestCharPtrLiteral();
void TestCharPtrReturnParam();
