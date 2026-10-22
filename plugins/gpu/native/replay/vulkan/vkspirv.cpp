// SPIR-V reflection and disassembly from the SPIR-V specification's opcode and enumerant numbers (the subset shaders
// for graphics and compute use; anything else is printed by number).
#include "vkspirv.h"

#include <cstring>
#include <set>

namespace fb::replay::vulkan
{
	namespace
	{
		struct Enumerant
		{
			u32 value;
			const char* name;
		};

		const Enumerant kStorage[] = {{0, "UniformConstant"}, {1, "Input"}, {2, "Uniform"}, {3, "Output"}, {4, "Workgroup"}, {5, "CrossWorkgroup"}, {6, "Private"},
			{7, "Function"}, {8, "Generic"}, {9, "PushConstant"}, {10, "AtomicCounter"}, {11, "Image"}, {12, "StorageBuffer"}, {5349, "PhysicalStorageBuffer"}};
		const Enumerant kDecoration[] = {{0, "RelaxedPrecision"}, {1, "SpecId"}, {2, "Block"}, {3, "BufferBlock"}, {4, "RowMajor"}, {5, "ColMajor"}, {6, "ArrayStride"},
			{7, "MatrixStride"}, {8, "GLSLShared"}, {9, "GLSLPacked"}, {10, "CPacked"}, {11, "BuiltIn"}, {13, "NoPerspective"}, {14, "Flat"}, {15, "Patch"}, {16, "Centroid"},
			{17, "Sample"}, {18, "Invariant"}, {19, "Restrict"}, {20, "Aliased"}, {21, "Volatile"}, {22, "Constant"}, {23, "Coherent"}, {24, "NonWritable"},
			{25, "NonReadable"}, {26, "Uniform"}, {28, "SaturatedConversion"}, {29, "Stream"}, {30, "Location"}, {31, "Component"}, {32, "Index"}, {33, "Binding"},
			{34, "DescriptorSet"}, {35, "Offset"}, {36, "XfbBuffer"}, {37, "XfbStride"}, {38, "FuncParamAttr"}, {39, "FPRoundingMode"}, {40, "FPFastMathMode"},
			{41, "LinkageAttributes"}, {42, "NoContraction"}, {43, "InputAttachmentIndex"}, {44, "Alignment"}, {5300, "NonUniform"}};
		const Enumerant kBuiltIn[] = {{0, "Position"}, {1, "PointSize"}, {3, "ClipDistance"}, {4, "CullDistance"}, {5, "VertexId"}, {6, "InstanceId"}, {7, "PrimitiveId"},
			{8, "InvocationId"}, {9, "Layer"}, {10, "ViewportIndex"}, {11, "TessLevelOuter"}, {12, "TessLevelInner"}, {13, "TessCoord"}, {14, "PatchVertices"},
			{15, "FragCoord"}, {16, "PointCoord"}, {17, "FrontFacing"}, {18, "SampleId"}, {19, "SamplePosition"}, {20, "SampleMask"}, {22, "FragDepth"},
			{23, "HelperInvocation"}, {24, "NumWorkgroups"}, {25, "WorkgroupSize"}, {26, "WorkgroupId"}, {27, "LocalInvocationId"}, {28, "GlobalInvocationId"},
			{29, "LocalInvocationIndex"}, {42, "VertexIndex"}, {43, "InstanceIndex"}, {4424, "BaseVertex"}, {4425, "BaseInstance"}, {4426, "DrawIndex"},
			{4440, "ViewIndex"}};
		const Enumerant kDim[] = {{0, "1D"}, {1, "2D"}, {2, "3D"}, {3, "Cube"}, {4, "Rect"}, {5, "Buffer"}, {6, "SubpassData"}};
		const Enumerant kImageFormat[] = {{0, "Unknown"}, {1, "Rgba32f"}, {2, "Rgba16f"}, {3, "R32f"}, {4, "Rgba8"}, {5, "Rgba8Snorm"}, {6, "Rg32f"}, {7, "Rg16f"},
			{8, "R11fG11fB10f"}, {9, "R16f"}, {10, "Rgba16"}, {11, "Rgb10A2"}, {12, "Rg16"}, {13, "Rg8"}, {14, "R16"}, {15, "R8"}, {21, "Rgba32i"}, {22, "Rgba16i"},
			{23, "Rgba8i"}, {24, "R32i"}, {30, "Rgba32ui"}, {31, "Rgba16ui"}, {32, "Rgba8ui"}, {33, "R32ui"}};
		const Enumerant kModel[] = {{0, "Vertex"}, {1, "TessellationControl"}, {2, "TessellationEvaluation"}, {3, "Geometry"}, {4, "Fragment"}, {5, "GLCompute"},
			{6, "Kernel"}, {5267, "TaskNV"}, {5268, "MeshNV"}, {5313, "RayGenerationKHR"}, {5364, "TaskEXT"}, {5365, "MeshEXT"}};
		const Enumerant kMode[] = {{0, "Invocations"}, {1, "SpacingEqual"}, {4, "VertexOrderCw"}, {5, "VertexOrderCcw"}, {6, "PixelCenterInteger"},
			{7, "OriginUpperLeft"}, {8, "OriginLowerLeft"}, {9, "EarlyFragmentTests"}, {10, "PointMode"}, {12, "DepthReplacing"}, {14, "DepthGreater"},
			{15, "DepthLess"}, {16, "DepthUnchanged"}, {17, "LocalSize"}, {18, "LocalSizeHint"}, {19, "InputPoints"}, {22, "Triangles"}, {26, "OutputVertices"},
			{27, "OutputPoints"}, {28, "OutputLineStrip"}, {29, "OutputTriangleStrip"}};
		const Enumerant kCapability[] = {{0, "Matrix"}, {1, "Shader"}, {2, "Geometry"}, {3, "Tessellation"}, {4, "Addresses"}, {5, "Linkage"}, {6, "Kernel"},
			{9, "Float16"}, {10, "Float64"}, {11, "Int64"}, {22, "Int16"}, {32, "ClipDistance"}, {33, "CullDistance"}, {39, "Int8"}, {40, "InputAttachment"},
			{44, "SampledBuffer"}, {49, "ImageQuery"}, {50, "DerivativeControl"}, {55, "StorageImageReadWithoutFormat"}, {56, "StorageImageWriteWithoutFormat"},
			{57, "MultiViewport"}, {4427, "DrawParameters"}, {4439, "MultiView"}, {5301, "RuntimeDescriptorArray"}};
		const Enumerant kAddressing[] = {{0, "Logical"}, {1, "Physical32"}, {2, "Physical64"}, {5348, "PhysicalStorageBuffer64"}};
		const Enumerant kMemory[] = {{0, "Simple"}, {1, "GLSL450"}, {2, "OpenCL"}, {3, "Vulkan"}};
		const Enumerant kLanguage[] = {{0, "Unknown"}, {1, "ESSL"}, {2, "GLSL"}, {3, "OpenCL_C"}, {4, "OpenCL_CPP"}, {5, "HLSL"}};
		const char* kGlsl[] = {nullptr, "Round", "RoundEven", "Trunc", "FAbs", "SAbs", "FSign", "SSign", "Floor", "Ceil", "Fract", "Radians", "Degrees", "Sin", "Cos",
			"Tan", "Asin", "Acos", "Atan", "Sinh", "Cosh", "Tanh", "Asinh", "Acosh", "Atanh", "Atan2", "Pow", "Exp", "Log", "Exp2", "Log2", "Sqrt", "InverseSqrt",
			"Determinant", "MatrixInverse", "Modf", "ModfStruct", "FMin", "UMin", "SMin", "FMax", "UMax", "SMax", "FClamp", "UClamp", "SClamp", "FMix", "IMix",
			"Step", "SmoothStep", "Fma", "Frexp", "FrexpStruct", "Ldexp", "PackSnorm4x8", "PackUnorm4x8", "PackSnorm2x16", "PackUnorm2x16", "PackHalf2x16",
			"PackDouble2x32", "UnpackSnorm2x16", "UnpackUnorm2x16", "UnpackHalf2x16", "UnpackSnorm4x8", "UnpackUnorm4x8", "UnpackDouble2x32", "Length",
			"Distance", "Cross", "Normalize", "FaceForward", "Reflect", "Refract", "FindILsb", "FindSMsb", "FindUMsb", "InterpolateAtCentroid",
			"InterpolateAtSample", "InterpolateAtOffset", "NMin", "NMax", "NClamp"};

		template <size_t N> const char* NameIn(const Enumerant (&table)[N], u32 value) {
			for (const Enumerant& e : table)
				if (e.value == value) return e.name;
			return nullptr;
		}

		// Operand patterns: T result type, R result, i id, * the rest ids, l literal, + the rest literals, s string,
		// e<kind> an enumerant (s storage, d decoration, m model, x mode, c capability, a addressing, y memory, g language,
		// i dim, f image format), X an extended instruction and its ids.
		struct OpInfo
		{
			u16 op;
			const char* name;
			const char* pattern;
		};

		const OpInfo kOps[] = {
			{3, "Source", "eg l"}, {4, "SourceExtension", "s"}, {5, "Name", "i s"}, {6, "MemberName", "i l s"}, {7, "String", "R s"}, {8, "Line", "i l l"},
			{10, "Extension", "s"}, {11, "ExtInstImport", "R s"}, {12, "ExtInst", "T R i X"}, {14, "MemoryModel", "ea ey"}, {15, "EntryPoint", "em i s *"},
			{16, "ExecutionMode", "i ex +"}, {17, "Capability", "ec"}, {19, "TypeVoid", "R"}, {20, "TypeBool", "R"}, {21, "TypeInt", "R l l"},
			{22, "TypeFloat", "R l"}, {23, "TypeVector", "R i l"}, {24, "TypeMatrix", "R i l"}, {25, "TypeImage", "R i ei l l l l ef"}, {26, "TypeSampler", "R"},
			{27, "TypeSampledImage", "R i"}, {28, "TypeArray", "R i i"}, {29, "TypeRuntimeArray", "R i"}, {30, "TypeStruct", "R *"},
			{32, "TypePointer", "R es i"}, {33, "TypeFunction", "R i *"}, {41, "ConstantTrue", "T R"}, {42, "ConstantFalse", "T R"}, {43, "Constant", "T R +"},
			{44, "ConstantComposite", "T R *"}, {46, "ConstantNull", "T R"}, {48, "SpecConstantTrue", "T R"}, {49, "SpecConstantFalse", "T R"},
			{50, "SpecConstant", "T R +"}, {51, "SpecConstantComposite", "T R *"}, {54, "Function", "T R l i"}, {55, "FunctionParameter", "T R"},
			{56, "FunctionEnd", ""}, {57, "FunctionCall", "T R i *"}, {59, "Variable", "T R es *"}, {61, "Load", "T R i +"}, {62, "Store", "i i +"},
			{63, "CopyMemory", "i i +"}, {65, "AccessChain", "T R i *"}, {66, "InBoundsAccessChain", "T R i *"}, {68, "ArrayLength", "T R i l"},
			{71, "Decorate", "i ed +"}, {72, "MemberDecorate", "i l ed +"}, {77, "VectorExtractDynamic", "T R i i"}, {78, "VectorInsertDynamic", "T R i i i"},
			{79, "VectorShuffle", "T R i i +"}, {80, "CompositeConstruct", "T R *"}, {81, "CompositeExtract", "T R i +"}, {82, "CompositeInsert", "T R i i +"},
			{83, "CopyObject", "T R i"}, {84, "Transpose", "T R i"}, {86, "SampledImage", "T R i i"}, {87, "ImageSampleImplicitLod", "T R i i +"},
			{88, "ImageSampleExplicitLod", "T R i i +"}, {89, "ImageSampleDrefImplicitLod", "T R i i i +"}, {90, "ImageSampleDrefExplicitLod", "T R i i i +"},
			{95, "ImageFetch", "T R i i +"}, {96, "ImageGather", "T R i i i +"}, {97, "ImageDrefGather", "T R i i i +"}, {98, "ImageRead", "T R i i +"},
			{99, "ImageWrite", "i i i +"}, {100, "Image", "T R i"}, {103, "ImageQuerySizeLod", "T R i i"}, {104, "ImageQuerySize", "T R i"},
			{105, "ImageQueryLod", "T R i i"}, {106, "ImageQueryLevels", "T R i"}, {107, "ImageQuerySamples", "T R i"}, {109, "ConvertFToU", "T R i"},
			{110, "ConvertFToS", "T R i"}, {111, "ConvertSToF", "T R i"}, {112, "ConvertUToF", "T R i"}, {113, "UConvert", "T R i"}, {114, "SConvert", "T R i"},
			{115, "FConvert", "T R i"}, {116, "QuantizeToF16", "T R i"}, {124, "Bitcast", "T R i"}, {126, "SNegate", "T R i"}, {127, "FNegate", "T R i"},
			{128, "IAdd", "T R i i"}, {129, "FAdd", "T R i i"}, {130, "ISub", "T R i i"}, {131, "FSub", "T R i i"}, {132, "IMul", "T R i i"},
			{133, "FMul", "T R i i"}, {134, "UDiv", "T R i i"}, {135, "SDiv", "T R i i"}, {136, "FDiv", "T R i i"}, {137, "UMod", "T R i i"},
			{138, "SRem", "T R i i"}, {139, "SMod", "T R i i"}, {140, "FRem", "T R i i"}, {141, "FMod", "T R i i"}, {142, "VectorTimesScalar", "T R i i"},
			{143, "MatrixTimesScalar", "T R i i"}, {144, "VectorTimesMatrix", "T R i i"}, {145, "MatrixTimesVector", "T R i i"},
			{146, "MatrixTimesMatrix", "T R i i"}, {147, "OuterProduct", "T R i i"}, {148, "Dot", "T R i i"}, {154, "Any", "T R i"}, {155, "All", "T R i"},
			{156, "IsNan", "T R i"}, {157, "IsInf", "T R i"}, {164, "LogicalEqual", "T R i i"}, {165, "LogicalNotEqual", "T R i i"}, {166, "LogicalOr", "T R i i"},
			{167, "LogicalAnd", "T R i i"}, {168, "LogicalNot", "T R i"}, {169, "Select", "T R i i i"}, {170, "IEqual", "T R i i"}, {171, "INotEqual", "T R i i"},
			{172, "UGreaterThan", "T R i i"}, {173, "SGreaterThan", "T R i i"}, {174, "UGreaterThanEqual", "T R i i"}, {175, "SGreaterThanEqual", "T R i i"},
			{176, "ULessThan", "T R i i"}, {177, "SLessThan", "T R i i"}, {178, "ULessThanEqual", "T R i i"}, {179, "SLessThanEqual", "T R i i"},
			{180, "FOrdEqual", "T R i i"}, {181, "FUnordEqual", "T R i i"}, {182, "FOrdNotEqual", "T R i i"}, {183, "FUnordNotEqual", "T R i i"},
			{184, "FOrdLessThan", "T R i i"}, {185, "FUnordLessThan", "T R i i"}, {186, "FOrdGreaterThan", "T R i i"}, {187, "FUnordGreaterThan", "T R i i"},
			{188, "FOrdLessThanEqual", "T R i i"}, {189, "FUnordLessThanEqual", "T R i i"}, {190, "FOrdGreaterThanEqual", "T R i i"},
			{191, "FUnordGreaterThanEqual", "T R i i"}, {194, "ShiftRightLogical", "T R i i"}, {195, "ShiftRightArithmetic", "T R i i"},
			{196, "ShiftLeftLogical", "T R i i"}, {197, "BitwiseOr", "T R i i"}, {198, "BitwiseXor", "T R i i"}, {199, "BitwiseAnd", "T R i i"},
			{200, "Not", "T R i"}, {201, "BitFieldInsert", "T R i i i i"}, {202, "BitFieldSExtract", "T R i i i"}, {203, "BitFieldUExtract", "T R i i i"},
			{204, "BitReverse", "T R i"}, {205, "BitCount", "T R i"}, {207, "DPdx", "T R i"}, {208, "DPdy", "T R i"}, {209, "Fwidth", "T R i"},
			{210, "DPdxFine", "T R i"}, {211, "DPdyFine", "T R i"}, {212, "FwidthFine", "T R i"}, {213, "DPdxCoarse", "T R i"}, {214, "DPdyCoarse", "T R i"},
			{215, "FwidthCoarse", "T R i"}, {218, "EmitVertex", ""}, {219, "EndPrimitive", ""}, {224, "ControlBarrier", "i i i"}, {225, "MemoryBarrier", "i i"},
			{227, "AtomicLoad", "T R i i i"}, {228, "AtomicStore", "i i i i"}, {229, "AtomicExchange", "T R i i i i"},
			{230, "AtomicCompareExchange", "T R i i i i i i"}, {232, "AtomicIIncrement", "T R i i i"}, {233, "AtomicIDecrement", "T R i i i"},
			{234, "AtomicIAdd", "T R i i i i"}, {235, "AtomicISub", "T R i i i i"}, {236, "AtomicSMin", "T R i i i i"}, {237, "AtomicUMin", "T R i i i i"},
			{238, "AtomicSMax", "T R i i i i"}, {239, "AtomicUMax", "T R i i i i"}, {240, "AtomicAnd", "T R i i i i"}, {241, "AtomicOr", "T R i i i i"},
			{242, "AtomicXor", "T R i i i i"}, {245, "Phi", "T R *"}, {246, "LoopMerge", "i i +"}, {247, "SelectionMerge", "i +"}, {248, "Label", "R"},
			{249, "Branch", "i"}, {250, "BranchConditional", "i i i +"}, {251, "Switch", "i i +"}, {252, "Kill", ""}, {253, "Return", ""},
			{254, "ReturnValue", "i"}, {255, "Unreachable", ""}, {4416, "TerminateInvocation", ""}, {5379, "DemoteToHelperInvocation", ""},
		};

		const OpInfo* OpOf(u32 op) {
			for (const OpInfo& info : kOps)
				if (info.op == op) return &info;
			return nullptr;
		}

		std::string StringAt(const u32* words, size_t count, size_t& at) {
			std::string text;
			while (at < count) {
				const u32 w = words[at++];
				for (int b = 0; b < 4; ++b) {
					const char c = (char)((w >> (b * 8)) & 0xFF);
					if (!c) return text;
					text += c;
				}
			}
			return text;
		}

		// What the parser keeps of a module: names, decorations, types, constants, variables.
		struct Info
		{
			std::map<u32, std::string> names;
			std::map<u32, std::map<u32, std::string>> memberNames;
			std::map<u32, std::map<u32, std::vector<u32>>> decorations;                    // id -> decoration -> values
			std::map<u32, std::map<u32, std::map<u32, std::vector<u32>>>> memberDecorations; // struct -> member -> decoration -> values
			std::map<u32, std::vector<u32>> types;     // id -> {op, operands after the result}
			std::map<u32, u32> constants;              // id -> its first literal
			std::map<u32, std::pair<u32, u32>> variables;   // id -> {pointer type, storage class}
			std::vector<std::pair<SpirvEntry, std::vector<u32>>> entries;
			std::map<u32, std::string> extInstSets;
		};

		bool Has(const Info& info, u32 id, u32 decoration) {
			auto found = info.decorations.find(id);
			return found != info.decorations.end() && found->second.count(decoration);
		}

		u32 Decoration(const Info& info, u32 id, u32 decoration, u32 fallback = 0) {
			auto found = info.decorations.find(id);
			if (found == info.decorations.end()) return fallback;
			auto value = found->second.find(decoration);
			return value == found->second.end() || value->second.empty() ? fallback : value->second[0];
		}

		std::string TypeText(const Info& info, u32 id, int depth = 0) {
			auto found = info.types.find(id);
			if (found == info.types.end() || depth > 8) return "?";
			const std::vector<u32>& t = found->second;
			switch (t[0]) {
				case 19: return "void";
				case 20: return "bool";
				case 21: return std::string(t[2] ? "int" : "uint") + (t[1] != 32 ? std::to_string(t[1]) : "");
				case 22: return t[1] == 32 ? "float" : t[1] == 64 ? "double" : "half";
				case 23: {
					const std::string component = TypeText(info, t[1], depth + 1);
					const char* prefix = component == "float" ? "vec" : component == "int" ? "ivec" : component == "uint" ? "uvec" : component == "bool" ? "bvec" : nullptr;
					return prefix ? prefix + std::to_string(t[2]) : component + std::to_string(t[2]);
				}
				case 24: {
					auto column = info.types.find(t[1]);
					const u32 rows = column != info.types.end() && column->second.size() > 2 ? column->second[2] : 0;
					return rows == t[2] ? "mat" + std::to_string(t[2]) : "mat" + std::to_string(t[2]) + "x" + std::to_string(rows);
				}
				case 25: {
					const char* dim = NameIn(kDim, t[2]);
					return std::string(t[6] == 2 ? "image" : "texture") + (dim ? dim : "?") + (t[4] ? "Array" : "") + (t[5] ? "MS" : "");
				}
				case 26: return "sampler";
				case 27: {
					auto image = info.types.find(t[1]);
					const char* dim = image != info.types.end() && image->second.size() > 2 ? NameIn(kDim, image->second[2]) : nullptr;
					return std::string("sampler") + (dim ? dim : "?") + (image != info.types.end() && image->second.size() > 4 && image->second[4] ? "Array" : "");
				}
				case 28: {
					auto length = info.constants.find(t[2]);
					return TypeText(info, t[1], depth + 1) + "[" + (length != info.constants.end() ? std::to_string(length->second) : "?") + "]";
				}
				case 29: return TypeText(info, t[1], depth + 1) + "[]";
				case 30: {
					auto name = info.names.find(id);
					return name != info.names.end() && !name->second.empty() ? name->second : "struct";
				}
				case 32: return TypeText(info, t[2], depth + 1);
				default: return "?";
			}
		}

		u32 MemberDecoration(const Info& info, u32 structId, u32 member, u32 decoration, u32 fallback = 0) {
			auto members = info.memberDecorations.find(structId);
			if (members == info.memberDecorations.end()) return fallback;
			auto decorations = members->second.find(member);
			if (decorations == members->second.end()) return fallback;
			auto value = decorations->second.find(decoration);
			return value == decorations->second.end() || value->second.empty() ? fallback : value->second[0];
		}

		u32 TypeSize(const Info& info, u32 id, int depth = 0);

		// A struct member's bytes: a matrix by its MatrixStride, anything else by its type.
		u32 MemberSize(const Info& info, u32 structId, u32 member, int depth) {
			auto type = info.types.find(structId);
			if (type == info.types.end() || member + 1 >= type->second.size()) return 0;
			const u32 memberType = type->second[member + 1];
			auto columns = info.types.find(memberType);
			const u32 stride = MemberDecoration(info, structId, member, 7);
			if (stride && columns != info.types.end() && columns->second[0] == 24) return stride * columns->second[2];
			return TypeSize(info, memberType, depth + 1);
		}

		u32 TypeSize(const Info& info, u32 id, int depth) {
			auto found = info.types.find(id);
			if (found == info.types.end() || depth > 8) return 0;
			const std::vector<u32>& t = found->second;
			switch (t[0]) {
				case 20: return 4;
				case 21:
				case 22: return t[1] / 8;
				case 23: return TypeSize(info, t[1], depth + 1) * t[2];
				case 24: return TypeSize(info, t[1], depth + 1) * t[2];
				case 28: {
					auto length = info.constants.find(t[2]);
					const u32 stride = Decoration(info, id, 6, TypeSize(info, t[1], depth + 1));
					return length != info.constants.end() ? stride * length->second : 0;
				}
				case 30: {
					u32 size = 0;
					for (u32 m = 0; m + 1 < t.size(); ++m) size = std::max(size, MemberDecoration(info, id, m, 35) + MemberSize(info, id, m, depth));
					return size;
				}
				default: return 0;
			}
		}

		bool MemberHas(const Info& info, u32 structId, u32 member, u32 decoration) {
			auto members = info.memberDecorations.find(structId);
			if (members == info.memberDecorations.end()) return false;
			auto decorations = members->second.find(member);
			return decorations != members->second.end() && decorations->second.count(decoration);
		}

		// A scalar type's layout base ("float", "half", "uint16", "bool") and bytes; empty for anything else.
		std::string ScalarBase(const Info& info, u32 id, u32& bytes) {
			auto t = info.types.find(id);
			if (t == info.types.end()) return "";
			switch (t->second[0]) {
				case 20: bytes = 4; return "bool";
				case 21: bytes = t->second[1] / 8; return std::string(t->second[2] ? "int" : "uint") + (t->second[1] != 32 ? std::to_string(t->second[1]) : "");
				case 22: bytes = t->second[1] / 8; return t->second[1] == 16 ? "half" : t->second[1] == 64 ? "double" : "float";
				default: return "";
			}
		}

		// A struct member placed for the buffer view: its vectors (a matrix's columns, or rows when row-major) and array
		// elements with their strides; no vector type for a struct or an array of arrays.
		BlockMember Placed(const Info& info, u32 structId, u32 member, const std::string& name) {
			BlockMember out;
			out.name = name;
			out.offset = MemberDecoration(info, structId, member, 35);
			out.bytes = MemberSize(info, structId, member, 0);
			u32 type = info.types.at(structId)[member + 1];
			auto t = info.types.find(type);
			if (t != info.types.end() && (t->second[0] == 28 || t->second[0] == 29)) {
				auto length = info.constants.find(t->second[2]);
				if (t->second[0] == 28 && length == info.constants.end()) return out;
				out.elements = t->second[0] == 28 ? length->second : 0;
				out.elementStride = Decoration(info, type, 6);
				type = t->second[1];
				t = info.types.find(type);
			}
			if (t == info.types.end()) return out;
			u32 scalar = type, components = 1;
			if (t->second[0] == 23) {
				scalar = t->second[1];
				components = t->second[2];
			}
			else if (t->second[0] == 24) {
				auto column = info.types.find(t->second[1]);
				if (column == info.types.end() || column->second[0] != 23) return out;
				scalar = column->second[1];
				const bool rowMajor = MemberHas(info, structId, member, 4);
				out.vectors = rowMajor ? column->second[2] : t->second[2];
				components = rowMajor ? t->second[2] : column->second[2];
				out.vectorStride = MemberDecoration(info, structId, member, 7);
			}
			u32 bytes = 0;
			const std::string base = ScalarBase(info, scalar, bytes);
			if (base.empty()) return out;
			out.vectorType = VectorType(base, components);
			out.vectorBytes = bytes * components;
			if (!out.vectorStride) out.vectorStride = out.vectorBytes;
			if (!out.elementStride) out.elementStride = (out.vectors - 1) * out.vectorStride + out.vectorBytes;
			return out;
		}

		bool Gather(const u32* words, size_t count, Info& info, SpirvModule& out) {
			if (count < 5 || words[0] != 0x07230203) return false;
			out.version = words[1];
			out.generator = words[2];
			out.bound = words[3];
			size_t at = 5;
			while (at < count) {
				const u32 length = words[at] >> 16, op = words[at] & 0xFFFF;
				if (!length || at + length > count) return false;
				const u32* w = words + at;
				++out.instructions;
				switch (op) {
					case 5: {
						size_t s = at + 2;
						info.names[w[1]] = StringAt(words, at + length, s);
						break;
					}
					case 6: {
						size_t s = at + 3;
						info.memberNames[w[1]][w[2]] = StringAt(words, at + length, s);
						break;
					}
					case 11: {
						size_t s = at + 2;
						info.extInstSets[w[1]] = StringAt(words, at + length, s);
						break;
					}
					case 15: {
						size_t s = at + 3;
						SpirvEntry entry;
						entry.model = w[1];
						entry.name = StringAt(words, at + length, s);
						info.entries.push_back({entry, std::vector<u32>(words + s, words + at + length)});
						break;
					}
					case 71: info.decorations[w[1]][w[2]] = std::vector<u32>(w + 3, w + length); break;
					case 72: info.memberDecorations[w[1]][w[2]][w[3]] = std::vector<u32>(w + 4, w + length); break;
					case 43:
						if (length > 3) info.constants[w[2]] = w[3];
						break;
					case 59: info.variables[w[2]] = {w[1], w[3]}; break;
					default:
						if (op >= 19 && op <= 39 && length >= 2) {
							std::vector<u32> type = {op};
							type.insert(type.end(), w + 2, w + length);
							info.types[w[1]] = std::move(type);
						}
						break;
				}
				at += length;
			}
			return true;
		}
	}

	const char* ExecutionModelName(u32 model) {
		const char* name = NameIn(kModel, model);
		return name ? name : "?";
	}

	bool ParseSpirv(const u32* words, size_t count, SpirvModule& out) {
		Info info;
		if (!words || !Gather(words, count, info, out)) return false;
		for (const auto& entry : info.entries) out.entries.push_back(entry.first);
		for (const auto& variable : info.variables) {
			const u32 id = variable.first, storage = variable.second.second;
			auto pointer = info.types.find(variable.second.first);
			if (pointer == info.types.end() || pointer->second[0] != 32) continue;
			const u32 pointee = pointer->second[2];
			auto name = info.names.find(id);
			const std::string text = name != info.names.end() ? name->second : "";
			if (storage == 1 || storage == 3) {
				SpirvInterface item;
				item.type = TypeText(info, pointee);
				if (Has(info, id, 30)) item.location = (i32)Decoration(info, id, 30);
				if (Has(info, id, 11)) {
					const char* builtin = NameIn(kBuiltIn, Decoration(info, id, 11));
					item.builtin = builtin ? builtin : "?";
				}
				item.name = !text.empty() ? text : !item.builtin.empty() ? item.builtin : "_" + std::to_string(id);
				(storage == 1 ? out.inputs : out.outputs).push_back(item);
				continue;
			}
			if (storage != 0 && storage != 2 && storage != 9 && storage != 12) continue;
			SpirvBinding binding;
			binding.name = text;
			binding.set = Decoration(info, id, 34);
			binding.binding = Decoration(info, id, 33);
			u32 type = pointee;
			auto t = info.types.find(type);
			if (t != info.types.end() && (t->second[0] == 28 || t->second[0] == 29)) {
				if (t->second[0] == 28) {
					auto length = info.constants.find(t->second[2]);
					binding.count = length != info.constants.end() ? length->second : 1;
				}
				else binding.count = 0;
				type = t->second[1];
				t = info.types.find(type);
			}
			if (t == info.types.end()) continue;
			switch (t->second[0]) {
				case 30: {
					binding.kind = storage == 9 ? "push constants" : (storage == 12 || Has(info, type, 3)) ? "storage buffer" : "uniform buffer";
					if (binding.name.empty()) {
						auto typeName = info.names.find(type);
						binding.name = typeName != info.names.end() ? typeName->second : "block";
					}
					binding.size = TypeSize(info, type);
					auto members = info.memberNames.find(type);
					for (u32 m = 0; m + 1 < t->second.size(); ++m) {
						SpirvMember member;
						member.name = members != info.memberNames.end() && members->second.count(m) ? members->second.at(m) : "_" + std::to_string(m);
						member.type = TypeText(info, t->second[m + 1]);
						member.offset = MemberDecoration(info, type, m, 35);
						member.size = MemberSize(info, type, m, 0);
						binding.members.push_back(member);
						binding.block.push_back(Placed(info, type, m, member.name));
					}
					break;
				}
				case 25:
					binding.kind = t->second[2] == 5 ? (t->second[6] == 2 ? "storage texel buffer" : "uniform texel buffer") : t->second[2] == 6 ? "input attachment"
						: t->second[6] == 2 ? "storage image" : "sampled image";
					break;
				case 26: binding.kind = "sampler"; break;
				case 27: binding.kind = "combined image sampler"; break;
				default: binding.kind = TypeText(info, type); break;
			}
			out.bindings.push_back(binding);
		}
		return true;
	}

	bool SpirvResult(u32 op, bool& typed) {
		const OpInfo* info = OpOf(op);
		typed = info && info->pattern[0] == 'T' && info->pattern[1] == ' ' && info->pattern[2] == 'R';
		return info && (typed || info->pattern[0] == 'R');
	}

	std::string DisassembleSpirv(const u32* words, size_t count, std::map<size_t, u32>* lines) {
		Info info;
		SpirvModule header;
		if (!words || !Gather(words, count, info, header)) return "; not SPIR-V";
		// Friendly names: the module's names, else types and scalar constants named by what they are.
		std::map<u32, std::string> names;
		std::set<std::string> taken;
		auto give = [&](u32 id, std::string name) {
			for (char& c : name)
				if (!isalnum((unsigned char)c) && c != '_') c = '_';
			std::string unique = name;
			for (int n = 1; taken.count(unique); ++n) unique = name + "_" + std::to_string(n);
			taken.insert(unique);
			names[id] = unique;
		};
		for (const auto& name : info.names)
			if (!name.second.empty()) give(name.first, name.second);
		size_t at = 5;
		while (at < count) {
			const u32 length = words[at] >> 16, op = words[at] & 0xFFFF;
			if (!length || at + length > count) break;
			const u32* w = words + at;
			if (op >= 19 && op <= 33 && length >= 2 && !names.count(w[1])) {
				std::string name;
				switch (op) {
					case 32: {
						const char* storage = NameIn(kStorage, w[2]);
						name = std::string("_ptr_") + (storage ? storage : "S") + "_" + (names.count(w[3]) ? names[w[3]] : std::to_string(w[3]));
						break;
					}
					case 33: name = "fn_" + std::to_string(w[1]); break;
					case 30: name = "struct_" + std::to_string(w[1]); break;
					default: name = TypeText(info, w[1]); break;
				}
				give(w[1], name);
			}
			if (op == 43 && length == 4 && names.count(w[1]) && !names.count(w[2])) {
				auto type = info.types.find(w[1]);
				std::string value = std::to_string(w[3]);
				if (type != info.types.end() && type->second[0] == 22) {
					float f;
					memcpy(&f, &w[3], 4);
					value = Format("%g", f);
				}
				else if (type != info.types.end() && type->second[0] == 21 && type->second[2]) value = std::to_string((i32)w[3]);
				for (char& c : value)
					if (c == '-') c = 'n';
				give(w[2], names[w[1]] + "_" + value);
			}
			at += length;
		}
		auto id = [&](u32 v) { return "%" + (names.count(v) ? names[v] : std::to_string(v)); };

		std::string text = Format("; SPIR-V %u.%u, generator 0x%08x, bound %u\n", (header.version >> 16) & 0xFF, (header.version >> 8) & 0xFF, header.generator, header.bound);
		u32 lineCount = 1;
		bool inFunction = false;
		at = 5;
		while (at < count) {
			const u32 length = words[at] >> 16, op = words[at] & 0xFFFF;
			if (!length || at + length > count) {
				text += "; (damaged)\n";
				break;
			}
			const OpInfo* opInfo = OpOf(op);
			std::string result, line = opInfo ? std::string("Op") + opInfo->name : "Op" + std::to_string(op);
			size_t o = at + 1;
			const size_t end = at + length;
			u32 decoration = UINT32_MAX;
			if (opInfo) {
				for (const char* p = opInfo->pattern; *p && o < end; ++p) {
					switch (*p) {
						case ' ': continue;
						case 'T': line += " " + id(words[o++]); break;
						case 'R': result = id(words[o++]) + " = "; break;
						case 'i': line += " " + id(words[o++]); break;
						case '*':
							while (o < end) line += " " + id(words[o++]);
							break;
						case 'l': line += " " + std::to_string(words[o++]); break;
						case '+':
							while (o < end) {
								if (decoration == 11) {
									const char* builtin = NameIn(kBuiltIn, words[o]);
									line += " " + (builtin ? std::string(builtin) : std::to_string(words[o]));
									++o;
								}
								else line += " " + std::to_string(words[o++]);
							}
							break;
						case 's': line += " \"" + StringAt(words, end, o) + "\""; break;
						case 'X': {
							const u32 set = words[o - 1];
							const u32 instruction = words[o++];
							const bool glsl = info.extInstSets.count(set) && info.extInstSets[set] == "GLSL.std.450";
							line += " " + (glsl && instruction < sizeof(kGlsl) / sizeof(kGlsl[0]) && kGlsl[instruction] ? std::string(kGlsl[instruction]) : std::to_string(instruction));
							while (o < end) line += " " + id(words[o++]);
							break;
						}
						case 'e': {
							const char kind = *++p;
							const u32 value = words[o++];
							const char* name = kind == 's' ? NameIn(kStorage, value) : kind == 'd' ? NameIn(kDecoration, value) : kind == 'm' ? NameIn(kModel, value)
								: kind == 'x' ? NameIn(kMode, value) : kind == 'c' ? NameIn(kCapability, value) : kind == 'a' ? NameIn(kAddressing, value)
								: kind == 'y' ? NameIn(kMemory, value) : kind == 'g' ? NameIn(kLanguage, value) : kind == 'i' ? NameIn(kDim, value)
								: kind == 'f' ? NameIn(kImageFormat, value) : nullptr;
							if (kind == 'd') decoration = value;
							line += " " + (name ? std::string(name) : std::to_string(value));
							break;
						}
					}
				}
			}
			while (o < end) line += " " + std::to_string(words[o++]);
			// A function's body is indented under its OpFunction and labels.
			if (op == 56) inFunction = false;
			const bool indent = inFunction && op != 248;
			if (lines) (*lines)[at] = lineCount;
			++lineCount;
			text +=(indent ? "        " : "") + result + line + "\n";
			if (op == 54) inFunction = true;
			at += length;
		}
		return text;
	}
}
