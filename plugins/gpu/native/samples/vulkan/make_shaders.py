# The Vulkan sample's shaders as SPIR-V, written with spirv.py, into a C++ header of word arrays (the same passes as
# the D3D12 sample: a compute pattern, the lit scene, the tonemap). Run by build.py before the sample compiles.
#   python make_shaders.py <out/shaders.h>
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from spirv import Module  # noqa: E402


def frame_block(m):
	return m.struct("Frame", [("viewProj", m.mat4(), 0, True), ("light", m.vec(4), 64, False)])


def object_block(m):
	return m.struct("Object", [("world", m.mat4(), 0, True), ("color", m.vec(4), 64, False), ("material", m.vec(4), 80, False)])


def pattern_cs():
	"""Writes a 256x256 pattern: red and green ramps, a blue checker."""
	m = Module("compute")
	uint, f = m.int(False), m.float()
	image = m.image2d(2, 4)                                   # a storage image, Rgba8
	target = m.variable("UniformConstant", image, "pattern", DescriptorSet=0, Binding=0)
	m.decorate(target, "NonReadable")
	gid = m.input(m.vec(3, uint), "globalId", builtin="GlobalInvocationId")
	ids = m.load(m.vec(3, uint), gid)
	x, y = m.extract(uint, ids, 0), m.extract(uint, ids, 1)
	r = m.op("FMul", f, m.op("ConvertUToF", f, x), m.const_f(1.0 / 256))
	g = m.op("FMul", f, m.op("ConvertUToF", f, y), m.const_f(1.0 / 256))
	bit = m.op("BitwiseAnd", uint, m.op("BitwiseXor", uint, x, y), m.const_u(32))
	b = m.op("FAdd", f, m.op("FMul", f, m.op("ConvertUToF", f, bit), m.const_f(0.75 / 32)), m.const_f(0.25))
	color = m.construct(m.vec(4), r, g, b, m.const_f(1.0))
	coord = m.op("Bitcast", m.vec(2, m.int(True)), m.construct(m.vec(2, uint), x, y))
	m.inst("code", "ImageWrite", m.load(image, target), coord, color)
	return m.binary(local_size=(8, 8, 1))


def scene_vs():
	m = Module("vertex")
	v2, v3, v4, mat4 = m.vec(2), m.vec(3), m.vec(4), m.mat4()
	frame = m.variable("Uniform", frame_block(m), "frame", DescriptorSet=0, Binding=0)
	obj = m.variable("PushConstant", object_block(m), "object")
	position, normal, uv = m.input(v3, "position", 0), m.input(v3, "normal", 1), m.input(v2, "uv", 2)
	out_normal, out_uv, out_tint = m.output(v3, "worldNormal", 0), m.output(v2, "texcoord", 1), m.output(v4, "tint", 2)
	clip = m.output(v4, "clipPosition", builtin="Position")
	world = m.member("PushConstant", mat4, obj, 0)
	world_position = m.op("MatrixTimesVector", v4, world, m.construct(v4, m.load(v3, position), m.const_f(1.0)))
	m.store(clip, m.op("MatrixTimesVector", v4, m.member("Uniform", mat4, frame, 0), world_position))
	world_normal = m.op("MatrixTimesVector", v4, world, m.construct(v4, m.load(v3, normal), m.const_f(0.0)))
	m.store(out_normal, m.op("VectorShuffle", v3, world_normal, world_normal, 0, 1, 2))
	m.store(out_uv, m.load(v2, uv))
	m.store(out_tint, m.member("PushConstant", v4, obj, 1))
	return m.binary()


def scene_fs():
	"""The object's tint times its texture (the pattern or the checker, by material), lit by the frame's light."""
	m = Module("fragment")
	f, v2, v3, v4 = m.float(), m.vec(2), m.vec(3), m.vec(4)
	frame = m.variable("Uniform", frame_block(m), "frame", DescriptorSet=0, Binding=0)
	obj = m.variable("PushConstant", object_block(m), "object")
	texture = m.sampled_image(m.image2d(1))
	pattern = m.variable("UniformConstant", texture, "patternTexture", DescriptorSet=0, Binding=1)
	checker = m.variable("UniformConstant", texture, "checkerTexture", DescriptorSet=0, Binding=2)
	in_normal, in_uv, in_tint = m.input(v3, "worldNormal", 0), m.input(v2, "texcoord", 1), m.input(v4, "tint", 2)
	out = m.output(v4, "color", 0)
	normal = m.glsl_op("Normalize", v3, m.load(v3, in_normal))
	light = m.member("Uniform", v4, frame, 1)
	ndl = m.glsl_op("FMax", f, m.op("Dot", f, normal, m.op("VectorShuffle", v3, light, light, 0, 1, 2)), m.const_f(0.0))
	lit = m.op("FMul", f, m.op("FAdd", f, m.op("FMul", f, ndl, m.const_f(0.8)), m.const_f(0.2)), m.extract(f, light, 3))
	uv = m.load(v2, in_uv)
	a = m.op("ImageSampleImplicitLod", v4, m.load(texture, pattern), uv)
	b = m.op("ImageSampleImplicitLod", v4, m.load(texture, checker), uv)
	t = m.extract(f, m.member("PushConstant", v4, obj, 2), 0)
	tex = m.glsl_op("FMix", v4, a, b, m.construct(v4, t, t, t, t))
	shaded = m.op("VectorTimesScalar", v4, m.op("FMul", v4, tex, m.load(v4, in_tint)), lit)
	m.store(out, m.op("VectorShuffle", v4, shaded, m.const_vec(1.0, 1.0, 1.0, 1.0), 0, 1, 2, 7))
	return m.binary()


def tonemap_vs():
	"""A triangle covering the target from the vertex index alone."""
	m = Module("vertex")
	i, f, v2, v4 = m.int(True), m.float(), m.vec(2), m.vec(4)
	index = m.input(i, "vertexIndex", builtin="VertexIndex")
	out_uv = m.output(v2, "texcoord", 0)
	clip = m.output(v4, "clipPosition", builtin="Position")
	vi = m.load(i, index)
	x = m.op("ConvertSToF", f, m.op("BitwiseAnd", i, m.op("ShiftLeftLogical", i, vi, m.const_i(1)), m.const_i(2)))
	y = m.op("ConvertSToF", f, m.op("BitwiseAnd", i, vi, m.const_i(2)))
	m.store(out_uv, m.construct(v2, x, y))
	two, one = m.const_f(2.0), m.const_f(1.0)
	cx = m.op("FSub", f, m.op("FMul", f, x, two), one)
	cy = m.op("FSub", f, m.op("FMul", f, y, two), one)
	m.store(clip, m.construct(v4, cx, cy, m.const_f(0.0), one))
	return m.binary()


def tonemap_fs():
	"""Reinhard: c / (1 + c), alpha 1."""
	m = Module("fragment")
	v2, v4 = m.vec(2), m.vec(4)
	hdr = m.variable("UniformConstant", m.sampled_image(m.image2d(1)), "hdr", DescriptorSet=0, Binding=0)
	in_uv = m.input(v2, "texcoord", 0)
	out = m.output(v4, "color", 0)
	c = m.op("ImageSampleImplicitLod", v4, m.load(m.sampled_image(m.image2d(1)), hdr), m.load(v2, in_uv))
	ones = m.const_vec(1.0, 1.0, 1.0, 1.0)
	mapped = m.op("FDiv", v4, c, m.op("FAdd", v4, c, ones))
	m.store(out, m.op("VectorShuffle", v4, mapped, ones, 0, 1, 2, 7))
	return m.binary()


SHADERS = [("kPatternCs", pattern_cs), ("kSceneVs", scene_vs), ("kSceneFs", scene_fs), ("kTonemapVs", tonemap_vs), ("kTonemapFs", tonemap_fs)]


def header():
	lines = ["// Generated by make_shaders.py: the Vulkan sample's SPIR-V. Do not edit.", "#pragma once", "#include <stdint.h>", ""]
	for name, make in SHADERS:
		words = make()
		lines.append("static const uint32_t %s[] = {" % name)
		for i in range(0, len(words), 8):
			lines.append("\t" + ", ".join("0x%08x" % w for w in words[i:i + 8]) + ",")
		lines.append("};")
		lines.append("")
	return "\n".join(lines)


def main():
	out = sys.argv[1]
	text = header()
	os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
	try:
		with open(out, encoding="ascii") as f:
			if f.read() == text:
				return
	except OSError:
		pass
	with open(out + ".tmp", "w", encoding="ascii", newline="\n") as f:
		f.write(text)
	os.replace(out + ".tmp", out)


if __name__ == "__main__":
	main()
