# spirv.py - a small SPIR-V 1.0 assembler for the Vulkan sample's shaders (no SPIR-V compiler is a dependency of
# Flashbang): types, constants and decorations are made once and laid out in the module's required section order;
# a shader's body is written with op() and its helpers. Stdlib only.
import struct

# Opcodes (SPIR-V 1.0 specification, section 3.32).
OP = {
	"Source": 3, "Name": 5, "MemberName": 6, "ExtInstImport": 11, "ExtInst": 12, "MemoryModel": 14, "EntryPoint": 15,
	"ExecutionMode": 16, "Capability": 17, "TypeVoid": 19, "TypeBool": 20, "TypeInt": 21, "TypeFloat": 22,
	"TypeVector": 23, "TypeMatrix": 24, "TypeImage": 25, "TypeSampler": 26, "TypeSampledImage": 27, "TypeArray": 28,
	"TypeStruct": 30, "TypePointer": 32, "TypeFunction": 33, "Constant": 43, "ConstantComposite": 44, "Function": 54,
	"FunctionEnd": 56, "Variable": 59, "Load": 61, "Store": 62, "AccessChain": 65, "Decorate": 71, "MemberDecorate": 72,
	"VectorShuffle": 79, "CompositeConstruct": 80, "CompositeExtract": 81, "ImageSampleImplicitLod": 87,
	"ImageWrite": 99, "ConvertUToF": 112, "ConvertSToF": 111, "Bitcast": 124, "FNegate": 127, "IAdd": 128,
	"FAdd": 129, "FSub": 131, "FMul": 133, "FDiv": 136, "VectorTimesScalar": 142, "MatrixTimesVector": 145, "Dot": 148,
	"ShiftLeftLogical": 196, "BitwiseXor": 198, "BitwiseAnd": 199, "Label": 248, "Return": 253,
}
# GLSL.std.450 extended instructions.
GLSL = {"FAbs": 4, "Floor": 8, "Fract": 10, "FMin": 37, "FMax": 40, "FClamp": 43, "FMix": 46, "Normalize": 69}
DECORATION = {"Block": 2, "ColMajor": 5, "MatrixStride": 7, "BuiltIn": 11, "NonReadable": 25, "Location": 30, "Binding": 33, "DescriptorSet": 34, "Offset": 35}
BUILTIN = {"Position": 0, "GlobalInvocationId": 28, "VertexIndex": 42}
STORAGE = {"UniformConstant": 0, "Input": 1, "Uniform": 2, "Output": 3, "Function": 7, "PushConstant": 9}
MODEL = {"vertex": 0, "fragment": 4, "compute": 5}


def words_of(text):
	data = text.encode("utf-8") + b"\0"
	data += b"\0" * (-len(data) % 4)
	return list(struct.unpack("<%dI" % (len(data) // 4), data))


class Module:
	def __init__(self, model, name="main"):
		self.model = model
		self.bound = 1
		self.sections = {k: [] for k in ("capability", "import", "memory", "entry", "mode", "debug", "decoration", "global", "code")}
		self.cache = {}
		self.interface = []
		self.entry_name = name
		self.inst("capability", "Capability", 1)        # Shader
		self.glsl = self.new_id()
		self.inst("import", "ExtInstImport", self.glsl, *words_of("GLSL.std.450"))
		self.inst("memory", "MemoryModel", 0, 1)         # Logical, GLSL450
		self.main = self.new_id()

	def new_id(self):
		self.bound += 1
		return self.bound - 1

	def inst(self, section, op, *operands):
		self.sections[section].append([OP[op]] + list(operands))

	def once(self, key, make):
		if key not in self.cache:
			self.cache[key] = make()
		return self.cache[key]

	def named(self, target, name):
		self.inst("debug", "Name", target, *words_of(name))
		return target

	def decorate(self, target, decoration, *values):
		self.inst("decoration", "Decorate", target, DECORATION[decoration], *values)

	def member_decorate(self, struct_id, member, decoration, *values):
		self.inst("decoration", "MemberDecorate", struct_id, member, DECORATION[decoration], *values)

	# --- Types -----------------------------------------------------------------------------------------------------

	def type(self, key, op, *operands):
		def make():
			result = self.new_id()
			self.inst("global", op, result, *operands)
			return result
		return self.once(("type",) + key, make)

	def void(self):
		return self.type(("void",), "TypeVoid")

	def float(self):
		return self.type(("float",), "TypeFloat", 32)

	def int(self, signed=True):
		return self.type(("int", signed), "TypeInt", 32, 1 if signed else 0)

	def vec(self, n, component=None):
		component = component or self.float()
		return self.type(("vec", n, component), "TypeVector", component, n)

	def mat4(self):
		return self.type(("mat4",), "TypeMatrix", self.vec(4), 4)

	def pointer(self, storage, target):
		return self.type(("ptr", storage, target), "TypePointer", STORAGE[storage], target)

	def function_type(self, result):
		return self.type(("fn", result), "TypeFunction", result)

	def image2d(self, sampled, format_code=0):
		# Sampled: 1 = with a sampler, 2 = a storage image.
		return self.type(("image2d", sampled, format_code), "TypeImage", self.float(), 1, 0, 0, 0, sampled, format_code)

	def sampled_image(self, image):
		return self.type(("sampledimage", image), "TypeSampledImage", image)

	def struct(self, name, members):
		"""A Block struct: members is [(name, type id, offset, is_matrix)]."""
		result = self.new_id()
		self.inst("global", "TypeStruct", result, *[m[1] for m in members])
		self.named(result, name)
		self.decorate(result, "Block")
		for i, (member, _, offset, is_matrix) in enumerate(members):
			self.inst("debug", "MemberName", result, i, *words_of(member))
			self.member_decorate(result, i, "Offset", offset)
			if is_matrix:
				self.member_decorate(result, i, "ColMajor")
				self.member_decorate(result, i, "MatrixStride", 16)
		return result

	# --- Constants -------------------------------------------------------------------------------------------------

	def constant(self, key, op, result_type, *values):
		# Unlike a type, a constant has a result type before its result id.
		def make():
			result = self.new_id()
			self.inst("global", op, result_type, result, *values)
			return result
		return self.once(("const",) + key, make)

	def const_f(self, value):
		bits = struct.unpack("<I", struct.pack("<f", value))[0]
		return self.constant(("f", bits), "Constant", self.float(), bits)

	def const_i(self, value):
		return self.constant(("i", value), "Constant", self.int(True), value & 0xFFFFFFFF)

	def const_u(self, value):
		return self.constant(("u", value), "Constant", self.int(False), value & 0xFFFFFFFF)

	def const_vec(self, *values):
		components = [self.const_f(v) for v in values]
		return self.constant(("v",) + tuple(components), "ConstantComposite", self.vec(len(values)), *components)

	# --- Variables -------------------------------------------------------------------------------------------------

	def variable(self, storage, value_type, name, **decorations):
		pointer = self.pointer(storage, value_type)
		result = self.new_id()
		self.inst("global", "Variable", pointer, result, STORAGE[storage])
		self.named(result, name)
		for decoration, value in decorations.items():
			self.decorate(result, decoration, *(value if isinstance(value, tuple) else (value,)))
		if storage in ("Input", "Output"):
			self.interface.append(result)
		return result

	def input(self, value_type, name, location=None, builtin=None):
		decorations = {"Location": location} if location is not None else {"BuiltIn": BUILTIN[builtin]}
		return self.variable("Input", value_type, name, **decorations)

	def output(self, value_type, name, location=None, builtin=None):
		decorations = {"Location": location} if location is not None else {"BuiltIn": BUILTIN[builtin]}
		return self.variable("Output", value_type, name, **decorations)

	# --- The entry point's body ------------------------------------------------------------------------------------

	def op(self, op, result_type, *operands):
		result = self.new_id()
		self.inst("code", op, result_type, result, *operands)
		return result

	def load(self, value_type, pointer):
		return self.op("Load", value_type, pointer)

	def store(self, pointer, value):
		self.inst("code", "Store", pointer, value)

	def member(self, storage, value_type, base, index):
		"""Loads member `index` of a Block variable."""
		chain = self.op("AccessChain", self.pointer(storage, value_type), base, self.const_i(index))
		return self.load(value_type, chain)

	def glsl_op(self, name, result_type, *operands):
		return self.op("ExtInst", result_type, self.glsl, GLSL[name], *operands)

	def extract(self, result_type, composite, *indices):
		return self.op("CompositeExtract", result_type, composite, *indices)

	def construct(self, result_type, *parts):
		return self.op("CompositeConstruct", result_type, *parts)

	def binary(self, local_size=None):
		void = self.void()
		fn_type = self.function_type(void)
		label = self.new_id()
		self.named(self.main, self.entry_name)
		self.inst("entry", "EntryPoint", MODEL[self.model], self.main, *words_of(self.entry_name), *self.interface)
		if self.model == "fragment":
			self.inst("mode", "ExecutionMode", self.main, 7)                 # OriginUpperLeft
		if self.model == "compute":
			self.inst("mode", "ExecutionMode", self.main, 17, *local_size)   # LocalSize
		body = [[OP["Function"], void, self.main, 0, fn_type], [OP["Label"], label]] + self.sections["code"] + [[OP["Return"]], [OP["FunctionEnd"]]]
		words = [0x07230203, 0x00010000, 0, self.bound, 0]
		for section in ("capability", "import", "memory", "entry", "mode", "debug", "decoration", "global"):
			for instruction in self.sections[section]:
				words += [(len(instruction) << 16) | instruction[0]] + instruction[1:]
		for instruction in body:
			words += [(len(instruction) << 16) | instruction[0]] + instruction[1:]
		return words
