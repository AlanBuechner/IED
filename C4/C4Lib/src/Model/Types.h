#pragma once
// Umbrella header for the C4 type system (NOTE resolution): each concrete type now lives in its own
// Model/Types/<Type>.h/.cpp. This file includes them all so existing `#include "Model/Types.h"` sites
// keep working unchanged. New code may include a single per-type header directly when it only needs one.

#include "Model/Types/TypeKind.h"
#include "Model/Types/Type.h"
#include "Model/Types/PrimitiveType.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/UnionType.h"
#include "Model/Types/SwizzleType.h"
#include "Model/Types/AliasType.h"
#include "Model/Types/PointerType.h"
#include "Model/Types/ReferenceType.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/SignatureType.h"
#include "Model/Types/FunPtrType.h"
#include "Model/Types/FuncType.h"
#include "Model/Types/FunctionType.h"
#include "Model/Types/InitListType.h"
#include "Model/Types/BracedListType.h"
#include "Model/Types/TemplateNameType.h"
#include "Model/Types/InvalidType.h"
