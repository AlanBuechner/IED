#include "ext.h"

int c4_exported_answer(void);

int CallExported(void) { return c4_exported_answer() + 1; }
