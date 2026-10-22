# gen_vulkan.py - writes vk_gen.h and vk_gen.cpp from the Khronos registry (vendor/vulkan-headers/registry/vk.xml):
# one Serialize per Vulkan structure Flashbang records (the roots below, what they contain, and every structure that
# extends one of them through pNext), the pNext chain by sType, handles as ids, and the names of enum values and flag
# bits for descriptions. Only what vulkan_core.h and vulkan_win32.h define is used (no provisional or other platforms'
# extensions). Rerun after changing the roots or the headers' pin; the output is committed like forwarders.h.
#   python gen_vulkan.py
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
REGISTRY = os.path.join(REPO, "vendor", "vulkan-headers", "registry", "vk.xml")

# What the capture records and the replay reads back.
ROOTS = [
	"VkApplicationInfo", "VkDeviceCreateInfo", "VkMemoryAllocateInfo", "VkBufferCreateInfo", "VkImageCreateInfo",
	"VkImageViewCreateInfo", "VkBufferViewCreateInfo", "VkSamplerCreateInfo", "VkShaderModuleCreateInfo",
	"VkDescriptorSetLayoutCreateInfo", "VkPipelineLayoutCreateInfo", "VkDescriptorPoolCreateInfo",
	"VkRenderPassCreateInfo", "VkRenderPassCreateInfo2", "VkFramebufferCreateInfo", "VkGraphicsPipelineCreateInfo",
	"VkComputePipelineCreateInfo", "VkCommandPoolCreateInfo", "VkFenceCreateInfo", "VkSemaphoreCreateInfo",
	"VkEventCreateInfo", "VkQueryPoolCreateInfo", "VkSwapchainCreateInfoKHR", "VkWriteDescriptorSet",
	"VkCopyDescriptorSet", "VkCommandBufferBeginInfo", "VkCommandBufferInheritanceInfo", "VkRenderPassBeginInfo",
	"VkSubpassBeginInfo", "VkSubpassEndInfo", "VkRenderingInfo", "VkDependencyInfo", "VkMemoryBarrier",
	"VkBufferMemoryBarrier", "VkImageMemoryBarrier", "VkViewport", "VkRect2D", "VkClearAttachment", "VkClearRect",
	"VkImageCopy", "VkImageBlit", "VkBufferCopy", "VkBufferImageCopy", "VkImageResolve", "VkImageSubresourceRange",
	"VkDebugUtilsLabelEXT", "VkCopyBufferInfo2", "VkCopyImageInfo2", "VkCopyBufferToImageInfo2",
	"VkCopyImageToBufferInfo2", "VkBlitImageInfo2", "VkResolveImageInfo2", "VkClearDepthStencilValue",
	"VkDescriptorImageInfo", "VkDescriptorBufferInfo", "VkPushConstantRange", "VkVertexInputBindingDescription2EXT",
	"VkVertexInputAttributeDescription2EXT",
]
# Enums and flags the hand-written records name, besides those the structures do.
EXTRA_ENUMS = ["VkIndexType", "VkPipelineBindPoint", "VkSubpassContents", "VkImageLayout", "VkObjectType", "VkResult",
	"VkStencilFaceFlagBits", "VkShaderStageFlagBits", "VkQueryControlFlagBits", "VkPipelineStageFlagBits",
	"VkDependencyFlagBits", "VkPhysicalDeviceType", "VkMemoryPropertyFlagBits", "VkMemoryHeapFlagBits",
	"VkQueueFlagBits", "VkPrimitiveTopology", "VkCullModeFlagBits", "VkFrontFace", "VkCompareOp", "VkLogicOp",
	"VkStencilOp", "VkBlendFactor", "VkBlendOp", "VkColorComponentFlagBits", "VkDynamicState", "VkPolygonMode",
	"VkImageAspectFlagBits", "VkSampleCountFlagBits", "VkFormat", "VkDescriptorType", "VkImageTiling",
	"VkImageUsageFlagBits", "VkBufferUsageFlagBits", "VkImageViewType", "VkImageType", "VkAccessFlagBits",
	"VkPipelineStageFlagBits2", "VkAccessFlagBits2", "VkResolveModeFlagBits", "VkAttachmentLoadOp", "VkAttachmentStoreOp",
	"VkFilter", "VkQueryType", "VkQueryResultFlagBits", "VkVertexInputRate", "VkLineRasterizationMode"]
# Unions, and structures whose arrays depend on another member, written by hand in vk_ser.cpp.
HAND_UNIONS = {"VkClearColorValue", "VkClearValue"}
HAND_STRUCTS = {"VkWriteDescriptorSet"}
DISPATCHABLE = "VK_DEFINE_HANDLE"
SCALARS = {"uint8_t", "int8_t", "uint16_t", "int16_t", "uint32_t", "int32_t", "uint64_t", "int64_t", "float", "double",
	"size_t", "VkBool32", "VkDeviceSize", "VkDeviceAddress", "VkSampleMask"}


def text_of(member):
	"""A member's C declaration without its comment."""
	parts = [member.text or ""]
	for child in member:
		if child.tag != "comment":
			parts.append(child.text or "")
		parts.append(child.tail or "")
	return "".join(parts)


def apis(element):
	value = element.get("api")
	return value.split(",") if value else ["vulkan"]


class Registry:
	def __init__(self, path):
		root = ET.parse(path).getroot()
		self.types = {}
		self.aliases = {}
		for t in root.find("types"):
			if t.tag != "type" or "vulkan" not in apis(t):
				continue
			name = t.get("name") or (t.find("name").text if t.find("name") is not None else None)
			if not name:
				continue
			if t.get("alias"):
				self.aliases[name] = t.get("alias")
				continue
			self.types[name] = t
		self.enums = {}
		self.wide = set()       # 64-bit flag bits: typedefs of VkFlags64, not enums
		for e in root.findall("enums"):
			if e.get("type") in ("enum", "bitmask"):
				self.enums[e.get("name")] = [(v.get("name"), self.value_of(v, None)) for v in e.findall("enum") if not v.get("alias") and "vulkan" in apis(v)]
				if e.get("bitwidth") == "64":
					self.wide.add(e.get("name"))
		self.available_types = set()
		for feature in root.findall("feature"):
			if "vulkan" in apis(feature):
				self.take(feature, None)
		for extension in root.find("extensions"):
			supported = (extension.get("supported") or "").split(",")
			if "vulkan" not in supported or extension.get("provisional") == "true" or extension.get("platform") not in (None, "win32"):
				continue
			self.take(extension, int(extension.get("number")))

	@staticmethod
	def value_of(e, extnumber):
		if e.get("bitpos") is not None:
			return 1 << int(e.get("bitpos"))
		if e.get("offset") is not None:
			number = int(e.get("extnumber") or extnumber)
			value = 1000000000 + (number - 1) * 1000 + int(e.get("offset"))
			return -value if e.get("dir") == "-" else value
		value = e.get("value")
		if value is None:
			return None
		value = value.rstrip("ULlf")
		try:
			return int(value, 0)
		except ValueError:
			return None

	def take(self, element, extnumber):
		for require in element.findall("require"):
			if "vulkan" not in apis(require):
				continue
			for t in require.findall("type"):
				self.available_types.add(t.get("name"))
			for e in require.findall("enum"):
				if e.get("extends") and not e.get("alias") and "vulkan" in apis(e):
					self.enums.setdefault(e.get("extends"), []).append((e.get("name"), self.value_of(e, extnumber)))

	def resolve(self, name):
		while name in self.aliases:
			name = self.aliases[name]
		return name

	def category(self, name):
		t = self.types.get(self.resolve(name))
		return t.get("category") if t is not None else None

	def available(self, name):
		return name in self.available_types or self.resolve(name) in self.available_types

	def enum_values(self, name):
		"""The enum's values the headers define (its own, and those the versions and extensions they define add), the
		first name of each value."""
		seen, out = set(), []
		for value_name, value in self.enums.get(name, []):
			if value is None or value in seen:
				continue
			seen.add(value)
			out.append((value_name, value))
		return out


class Member:
	def __init__(self, element):
		self.element = element
		self.name = element.find("name").text
		self.type = element.find("type").text
		decl = text_of(element)
		after = decl.split(self.name, 1)[1] if self.name in decl else ""
		self.pointers = decl.split(self.name)[0].count("*")
		self.dims = re.findall(r"\[([A-Za-z0-9_]+)\]", after)
		self.bitfield = ":" in after
		self.len = (element.get("len") or "").split(",")
		self.altlen = element.get("altlen")
		self.optional = (element.get("optional") or "").startswith("true")


class Generator:
	def __init__(self, registry):
		self.r = registry
		self.structs = {}          # name -> [Member] (supported, in emission order)
		self.unsupported = set()
		self.enums = set(EXTRA_ENUMS)
		self.flags = set()
		self.handles = set()

	def members(self, name):
		return [Member(m) for m in self.r.types[name].findall("member") if "vulkan" in apis(m)]

	def supported_member(self, owner, m):
		if m.bitfield or len(m.dims) > 1:
			return False
		category = self.r.category(m.type)
		if category == "funcpointer":
			return False
		if m.pointers >= 2:
			return m.type == "char" and "null-terminated" in m.len
		if m.dims and (m.type == "char" or category in ("struct", "union")):
			return category == "struct" and self.include(m.type)
		if category == "union":
			return m.type in HAND_UNIONS
		if category == "struct":
			return self.include(m.type)
		if m.pointers == 1 and m.type == "void" and m.name != "pNext" and not m.len[0]:
			return True                  # an opaque pointer (pUserData): not recorded
		if m.type in SCALARS or category in ("enum", "bitmask", "handle") or m.type in ("void", "char"):
			return True
		return False

	def include(self, name):
		name = self.r.resolve(name)
		if name in self.structs:
			return True
		if name in self.unsupported or self.r.category(name) != "struct" or not self.r.available(name):
			return False
		self.structs[name] = None       # in progress (a structure may contain itself through pNext)
		members = self.members(name)
		for m in members:
			if not self.supported_member(name, m):
				del self.structs[name]
				self.unsupported.add(name)
				return False
		self.structs[name] = members
		for m in members:
			category = self.r.category(m.type)
			if category == "enum":
				self.enums.add(self.r.resolve(m.type))
			elif category == "bitmask":
				self.flags.add(self.r.resolve(m.type))
			elif category == "handle":
				self.handles.add(self.r.resolve(m.type))
		return True

	def close(self):
		for root in ROOTS:
			if not self.include(root):
				raise SystemExit("root %s is not supported" % root)
		# Every available structure that extends an included one (repeat: extenders extend extenders).
		while True:
			added = False
			for name, t in self.r.types.items():
				if name in self.structs or name in self.unsupported or t.get("category") != "struct":
					continue
				extends = [self.r.resolve(x) for x in (t.get("structextends") or "").split(",") if x]
				if any(x in self.structs for x in extends) and not t.get("returnedonly") == "true" and self.include(name):
					added = True
			if not added:
				break

	# --- Emission --------------------------------------------------------------------------------------------------

	def stype_of(self, name):
		for m in self.r.types[name].findall("member"):
			if m.find("name").text == "sType" and m.get("values"):
				return m.get("values")
		return None

	def count_expr(self, m, members):
		names = {x.name for x in members}
		if m.altlen:
			return re.sub(r"\b([A-Za-z_][A-Za-z0-9_]*)\b", lambda g: "v." + g.group(1) if g.group(1) in names else g.group(1), m.altlen)
		first = m.len[0]
		if first in names:
			return "v." + first
		if first.isdigit():
			return first
		return None

	def emit_member(self, name, m, members):
		category = self.r.category(m.type)
		field = "v." + m.name
		if m.name == "pNext":
			if m.element.text and "const" in m.element.text:
				return ['\tfb::vk::SerializeNext(s, "pNext", v.pNext);']
			return ['\t{', '\t\tconst void* next = v.pNext;', '\t\tfb::vk::SerializeNext(s, "pNext", next);', '\t\tv.pNext = (void*)next;', '\t}']
		if m.dims:
			return ['\ts.Fixed("%s", %s);' % (m.name, field)]
		if m.pointers >= 2:
			count = self.count_expr(m, members)
			return ['\ts.StrArray("%s", %s, %s);' % (m.name, field, count)]
		if m.pointers == 1:
			if m.type == "char":
				return ['\ts.Str("%s", %s);' % (m.name, field)]
			if name == "VkShaderModuleCreateInfo" and m.name == "pCode":
				return ['\t{', '\t\tconst void* code = v.pCode;', '\t\ts.Blob("pCode", code, v.codeSize);', '\t\tv.pCode = (const uint32_t*)code;', '\t}']
			if m.type == "void" and not m.len[0]:
				return ['\tif (s.Reading()) %s = nullptr;' % field]
			count = self.count_expr(m, members)
			if m.type == "void":
				return ['\t{', '\t\tconst void* data = %s;' % field, '\t\tfb::u64 size = (fb::u64)(%s);' % count, '\t\ts.Blob("%s", data, size);' % m.name, '\t\t%s = (decltype(%s))data;' % (field, field), '\t}']
			if count is None:
				return ['\ts.Optional("%s", %s);' % (m.name, field)]
			return ['\tfb::vk::SerArray(s, "%s", %s, %s);' % (m.name, field, count)]
		if category == "bitmask":
			bits = self.bits_of(m.type)
			if bits:
				return ['\ts.Flags("%s", %s, &fb::vk::Flags_%s);' % (m.name, field, bits)]
		return ['\ts.Field("%s", %s);' % (m.name, field)]

	def bits_of(self, flags):
		t = self.r.types.get(self.r.resolve(flags))
		if t is None:
			return None
		bits = t.get("requires") or t.get("bitvalues")
		if bits and self.r.available(bits):
			self.enums.add(bits)
			return bits
		return None

	def flags_function(self, bits):
		values = [(n, v) for n, v in self.r.enum_values(bits) if v]
		lines = ["\tstd::string Flags_%s(u64 value) {" % bits, "\t\tstatic const FlagName names[] = {"]
		prefix = common_prefix([n for n, _ in values])
		for n, _ in values:
			lines.append('\t\t\t{(u64)%s, "%s"},' % (n, short(n, prefix)))
		lines += ["\t\t\t{0, nullptr},", "\t\t};", "\t\treturn JoinFlags(value, names);", "\t}"]
		return lines

	def enum_function(self, name):
		values = self.r.enum_values(name)
		prefix = common_prefix([n for n, _ in values])
		lines = ["const char* EnumName(%s v) {" % name, "\tswitch (v) {"]
		for n, _ in values:
			lines.append('\t\tcase %s: return "%s";' % (n, short(n, prefix)))
		lines += ["\t\tdefault: return nullptr;", "\t}", "}"]
		return lines

	def write(self):
		self.close()
		order = sorted(self.structs)
		bitmask_bits = sorted({b for b in (self.bits_of(f) for f in self.flags) if b})
		enum_names = sorted(e for e in self.enums if self.r.category(e) == "enum" and self.r.available(e) and self.r.enum_values(e) and e not in self.r.wide)
		wide_bits = sorted(e for e in self.enums if e in self.r.wide and self.r.available(e))
		flag_bits = sorted(set(bitmask_bits) | {e for e in enum_names if "FlagBits" in e} | set(wide_bits))
		plain_enums = [e for e in enum_names if "FlagBits" not in e]
		header = [
			"// Generated by gen_vulkan.py from vk.xml: Vulkan structures as Flashbang serializes them. Do not edit.",
			"#pragma once", "", '#include "ser.h"', "", "#include <vulkan/vulkan.h>", "", "#include <string>", "",
			"// Names of enum values for descriptions (without their common prefix).",
		]
		header += ["const char* EnumName(%s v);" % e for e in enum_names]
		header += ["", "namespace fb::vk", "{", "\t// Names of a flag type's set bits, joined with '|'."]
		header += ["\tstd::string Flags_%s(u64 value);" % b for b in flag_bits]
		header += [
			"",
			"\t// A pNext chain: each structure Flashbang knows, by sType; one it does not is left out, and its sType is",
			"\t// noted for TakeDroppedNext (the capture warns that a replay may differ).",
			"\tvoid SerializeNext(Ser& s, const char* name, const void*& next);",
			"\tbool KnownNext(VkStructureType type);",
			"\tstd::vector<u32> TakeDroppedNext();",
			"",
			"\t// An array that may be absent: a presence byte, then the items.",
			"\ttemplate <class T, class C> void SerArray(Ser& s, const char* name, T*& items, C count) {",
			"\t\tif (!s.Presence(items != nullptr && count > 0)) {",
			"\t\t\tif (s.Reading()) items = nullptr;",
			"\t\t\tif (s.Describing()) s.Json()->Null(name);",
			"\t\t\treturn;",
			"\t\t}",
			"\t\ts.Items(name, items, count);",
			"\t}",
			"}", "",
			"// Handles travel as ids (non-dispatchable) or object ids (dispatchable).",
		]
		for h in sorted(self.handles | {"VkQueue", "VkCommandBuffer", "VkDevice", "VkPhysicalDevice", "VkInstance", "VkBuffer", "VkImage", "VkImageView",
				"VkSampler", "VkDescriptorSet", "VkPipeline", "VkPipelineLayout", "VkRenderPass", "VkFramebuffer", "VkDeviceMemory", "VkQueryPool",
				"VkSwapchainKHR", "VkFence", "VkSemaphore", "VkEvent", "VkShaderModule", "VkDescriptorSetLayout", "VkDescriptorPool", "VkCommandPool",
				"VkBufferView", "VkPipelineCache"}):
			t = self.r.types.get(h)
			if t is None or not self.r.available(h):
				continue
			kind = t.find("type").text if t.find("type") is not None else ""
			call = "Object" if kind == DISPATCHABLE else "Handle"
			header.append("inline void SerField(fb::Ser& s, const char* name, %s& v) { s.%s(name, v); }" % (h, call))
		header += ["", "// Unions, by hand (vk_ser.cpp).", "void SerField(fb::Ser& s, const char* name, VkClearColorValue& v);",
			"void SerField(fb::Ser& s, const char* name, VkClearValue& v);", "", "// Structures."]
		header += ["void Serialize(fb::Ser& s, %s& v);" % name for name in order]
		header.append("")

		source = ['// Generated by gen_vulkan.py from vk.xml. Do not edit.', '#include "vk_gen.h"', "",
			"#include <vector>", ""]
		for e in plain_enums:
			source += self.enum_function(e) + [""]
		for e in sorted(e for e in enum_names if "FlagBits" in e):
			source += self.enum_function(e) + [""]
		source += ["namespace fb::vk", "{"]
		for b in flag_bits:
			source += self.flags_function(b) + [""]
		known = [(name, self.stype_of(name)) for name in order if self.stype_of(name) and any(m.name == "pNext" for m in self.structs[name])]
		source += ["\tstatic thread_local std::vector<u32> dropped;", "",
			"\tstd::vector<u32> TakeDroppedNext() {", "\t\tstd::vector<u32> out;", "\t\tout.swap(dropped);", "\t\treturn out;", "\t}", "",
			"\tbool KnownNext(VkStructureType type) {", "\t\tswitch (type) {"]
		source += ["\t\t\tcase %s:" % st for _, st in known]
		source += ["\t\t\t\treturn true;", "\t\t\tdefault: return false;", "\t\t}", "\t}", "",
			"\ttemplate <class T> static const void* NextOf(Ser& s, const char* name, const void* at) {",
			"\t\tT* item = s.Writing() ? (T*)at : s.Memory()->Array<T>(1);",
			"\t\tif (s.Reading()) item->sType = (VkStructureType)0;",
			"\t\ts.Field(name, *item);",
			"\t\treturn item;", "\t}", "",
			"\tvoid SerializeNext(Ser& s, const char* name, const void*& next) {",
			"\t\tif (s.Writing()) {",
			"\t\t\twhile (next && !KnownNext(((const VkBaseInStructure*)next)->sType)) {",
			"\t\t\t\tdropped.push_back((u32)((const VkBaseInStructure*)next)->sType);",
			"\t\t\t\tnext = ((const VkBaseInStructure*)next)->pNext;",
			"\t\t\t}",
			"\t\t}",
			"\t\tu32 type = s.Writing() && next ? (u32)((const VkBaseInStructure*)next)->sType : 0;",
			"\t\ts.RawU32(type);",
			"\t\tif (!type) {",
			"\t\t\tif (s.Reading()) next = nullptr;",
			"\t\t\tif (s.Describing()) s.Json()->Null(name);",
			"\t\t\treturn;",
			"\t\t}",
			"\t\tswitch ((VkStructureType)type) {"]
		for struct_name, st in known:
			source.append("\t\t\tcase %s: next = NextOf<%s>(s, name, next); break;" % (st, struct_name))
		source += ["\t\t\tdefault:", "\t\t\t\tif (s.Reading()) next = nullptr;", "\t\t\t\tbreak;", "\t\t}", "\t}", "}", ""]
		for name in order:
			if name in HAND_STRUCTS:
				continue
			members = self.structs[name]
			source.append("void Serialize(fb::Ser& s, %s& v) {" % name)
			for m in members:
				source += self.emit_member(name, m, members)
			source += ["}", ""]
		return "\n".join(header), "\n".join(source)


def common_prefix(names):
	if not names:
		return ""
	if len(names) == 1:
		parts = names[0].split("_")
		return "_".join(parts[:2]) + "_" if len(parts) > 2 else ""
	prefix = os.path.commonprefix(names)
	return prefix[:prefix.rfind("_") + 1] if "_" in prefix else ""


def short(name, prefix):
	out = name[len(prefix):] if prefix and name.startswith(prefix) and len(name) > len(prefix) else name
	if out.endswith("_BIT"):
		out = out[:-4]
	elif "_BIT_" in out:
		out = out.replace("_BIT_", "_")
	return out or name


def write(path, text):
	text = text if text.endswith("\n") else text + "\n"
	try:
		with open(path, encoding="ascii") as f:
			if f.read() == text:
				return
	except OSError:
		pass
	with open(path + ".tmp", "w", encoding="ascii", newline="\n") as f:
		f.write(text)
	os.replace(path + ".tmp", path)


def main():
	generator = Generator(Registry(REGISTRY))
	header, source = generator.write()
	write(os.path.join(HERE, "vk_gen.h"), header)
	write(os.path.join(HERE, "vk_gen.cpp"), source)
	print("structures %d, enums %d" % (len(generator.structs), len(generator.enums)))


if __name__ == "__main__":
	main()
