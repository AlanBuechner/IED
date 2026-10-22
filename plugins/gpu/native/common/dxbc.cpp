// dxbc.cpp - DXBC containers, their checksum, and the shader debugger's instrumentation (dxbc.h). The instrumented
// program picks the target invocation once at its top (by pixel, by vertex and instance, or by thread), takes a region
// of the trace buffer with an atomic add, and after each instruction that writes a register or stores appends a record
// inside an `if`. DXBC is a register machine with structured control flow, so the added blocks leave the shader's own
// flow, derivatives and samples as they were. Output registers are write-only: an instruction writing one writes a
// spare temp instead, which is logged and then moved to the output.
#include "dxbc.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace fb
{
	namespace
	{
		constexpr u32 kMd5Sines[64] = {
			0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
			0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
			0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
			0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
			0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
			0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
			0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
			0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
		constexpr u32 kMd5Shifts[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};

		u32 Rotate(u32 value, u32 shift) { return value << shift | value >> (32 - shift); }

		void Md5Block(u32 state[4], const u32 x[16]) {
			u32 a = state[0], b = state[1], c = state[2], d = state[3];
			for (u32 i = 0; i < 64; ++i) {
				u32 f, g;
				switch (i / 16) {
					case 0: f = (b & c) | (~b & d), g = i; break;
					case 1: f = (d & b) | (~d & c), g = (5 * i + 1) % 16; break;
					case 2: f = b ^ c ^ d, g = (3 * i + 5) % 16; break;
					default: f = c ^ (b | ~d), g = 7 * i % 16; break;
				}
				const u32 next = d;
				d = c;
				c = b;
				b += Rotate(a + f + kMd5Sines[i] + x[g], kMd5Shifts[i / 16 * 4 + i % 4]);
				a = next;
			}
			state[0] += a;
			state[1] += b;
			state[2] += c;
			state[3] += d;
		}

		// Opcodes of the tokenized program format (D3D10_SB_OPCODE_TYPE, with D3D11's additions).
		enum : u32
		{
			kOpAnd = 1, kOpBreakc = 3, kOpCall = 4, kOpCallc = 5, kOpCase = 6, kOpContinuec = 8, kOpDiscard = 13, kOpEndif = 21,
			kOpFtou = 28, kOpIadd = 30, kOpIf = 31, kOpIeq = 32, kOpImad = 35, kOpImul = 38, kOpLabel = 44, kOpCustomData = 53,
			kOpMov = 54, kOpRetc = 63, kOpSwitch = 76, kOpSincos = 77, kOpUdiv = 78, kOpUlt = 79, kOpUmul = 81, kOpDclInput = 95,
			kOpDclInputSgv = 96, kOpDclInputPsSiv = 100, kOpDclTemps = 104, kOpHsDecls = 113, kOpInterfaceCall = 120,
			kOpUaddc = 132, kOpUsubb = 133, kOpSwapc = 142, kOpDclUavTyped = 156, kOpDclUavRaw = 157, kOpDclUavStructured = 158,
			kOpStoreUavTyped = 164, kOpStoreRaw = 166, kOpStoreStructured = 168, kOpImmAtomicIadd = 180,
		};
		// Operand types.
		enum : u32
		{
			kTemp = 0, kInput = 1, kOutput = 2, kIndexableTemp = 3, kImmediate32 = 4, kImmediate64 = 5, kOutputDepth = 12,
			kOutputCoverage = 15, kUav = 30, kThreadShared = 31, kThreadId = 32, kThreadGroupId = 33, kThreadInGroup = 34,
			kOutputDepthAtLeast = 38, kOutputDepthAtMost = 39,
			kOutputStencilRef = 41,
		};
		// Signature system values (D3D_NAME), program kinds, and how a site's values read.
		constexpr u32 kPosition = 1, kVertexId = 6, kInstanceId = 8;
		constexpr u32 kPixel = 0, kVertex = 1, kCompute = 5;
		constexpr u32 kFloat = 0, kInt = 1, kUint = 2, kBool = 3, kRaw = 4;
		constexpr u32 kXyzw = 0xE4;            // the identity swizzle
		constexpr u32 kNonZero = 1u << 18;     // if_nz rather than if_z
		constexpr u32 kScalar = 0x4001, kVector = 0x4002;    // 32-bit immediates, one or four

		// Operand tokens: four components written by mask, read by swizzle, or one read.
		u32 MaskOp(u32 type, u32 mask, u32 dimension) { return 2 | mask << 4 | type << 12 | dimension << 20; }
		u32 SwizzleOp(u32 type, u32 swizzle, u32 dimension) { return 2 | 1 << 2 | swizzle << 4 | type << 12 | dimension << 20; }
		u32 OneOp(u32 type, u32 component, u32 dimension) { return 2 | 2 << 2 | component << 4 | type << 12 | dimension << 20; }

		bool IsDeclaration(u32 op) { return (op >= 88 && op <= 106) || op == kOpHsDecls || (op >= 143 && op <= 162) || op == 206; }
		// The declarations the compiler writes first: global flags, constant buffers, samplers, resources, UAVs.
		bool IsResourceDeclaration(u32 op) { return op == 106 || op == 88 || op == 89 || op == 90 || op == 161 || op == 162 || (op >= 156 && op <= 158) || op == kOpCustomData; }
		bool IsInputDeclaration(u32 op) { return op >= 95 && op <= 100; }
		// Instructions whose first operand is read, not written.
		bool IsFlow(u32 op) {
			return op == kOpBreakc || op == kOpCallc || op == kOpCase || op == kOpContinuec || op == kOpDiscard || op == kOpIf || op == kOpRetc || op == kOpSwitch;
		}
		bool HasTwoResults(u32 op) {
			return op == kOpImul || op == kOpSincos || op == kOpUdiv || op == kOpUmul || op == kOpUaddc || op == kOpUsubb || op == kOpSwapc;
		}
		bool IsOutput(u32 type) {
			return type == kOutput || type == kOutputDepth || type == kOutputCoverage || type == kOutputDepthAtLeast || type == kOutputDepthAtMost || type == kOutputStencilRef;
		}

		u32 ResultType(u32 op) {
			switch (op) {
				case 24: case 29: case 32: case 33: case 34: case 39: case 49: case 57: case 79: case 80: case 195: case 196: case 197: case 198: case 234:
					return kBool;
				case 27: case 30: case 35: case 36: case 37: case 38: case 40: case 41: case 42: case 137: case 139: case 214:
					return kInt;
				case 28: case 78: case 81: case 82: case 83: case 84: case 85: case 121: case 130: case 132: case 133: case 134: case 135:
				case 136: case 138: case 140: case 141: case 213: case 215:
					return kUint;
				case 1: case 45: case 46: case 54: case 55: case 59: case 60: case 61: case 87: case 109: case 111: case 126: case 127:
				case 128: case 142: case 163: case 165: case 167: case 202:
					return kRaw;
				default:
					if (op >= 178 && op <= 189) return kUint;    // the value an immediate atomic found
					if ((op >= 191 && op <= 200) || (op >= 210 && op <= 217) || (op >= 219 && op <= 227)) return kRaw;    // doubles, feedback
					return kFloat;
			}
		}

		std::string MaskText(u32 mask) {
			std::string text = ".";
			for (u32 k = 0; k < 4; ++k)
				if (mask >> k & 1) text += "xyzw"[k];
			return text;
		}

		struct Instruction
		{
			u32 at = 0;          // word index in the program; the byte offset is 4 times it
			u32 length = 0;
			u32 op = 0;
		};

		struct Operand
		{
			u32 begin = 0, end = 0;
			u32 type = 0;
			u32 components = 0;      // the token's field: 0, 1, 2 (four) or 3 (N)
			u32 mode = 0;            // four components: 0 by mask, 1 by swizzle, 2 one selected
			u32 mask = 0;
			u32 dimension = 0;
			bool immediate = true;   // every index a 32-bit immediate
			u32 index[3] = {};
		};

		bool ReadOperand(const std::vector<u32>& w, u32 end, u32& p, Operand& o) {
			if (p >= end) return false;
			o.begin = p;
			const u32 token = w[p++];
			for (bool more = token >> 31; more;) {
				if (p >= end) return false;
				more = w[p++] >> 31;
			}
			o.components = token & 3;
			o.mode = token >> 2 & 3;
			o.mask = token >> 4 & 0xF;
			o.type = token >> 12 & 0xFF;
			o.dimension = token >> 20 & 3;
			o.immediate = true;
			if (o.type == kImmediate32) p += o.components == 1 ? 1 : o.components == 2 ? 4 : 0;
			else if (o.type == kImmediate64) p += o.components == 1 ? 2 : o.components == 2 ? 8 : 0;
			for (u32 d = 0; d < o.dimension; ++d) {
				const u32 representation = token >> (22 + 3 * d) & 7;
				if (representation == 0 || representation == 3) {
					if (p < end) o.index[d] = w[p];
					p += 1;
				}
				else if (representation == 1 || representation == 4) p += 2;
				if (representation != 0) o.immediate = false;
				if (representation >= 2) {
					Operand relative;
					if (!ReadOperand(w, end, p, relative)) return false;
				}
			}
			o.end = p;
			return p <= end;
		}

		struct Element
		{
			std::string name;
			u32 stream = 0, index = 0, system = 0, component = 0, reg = 0, mask = 0, used = 0, precision = 0;
		};

		// An input signature: ISGN, or ISG1 with streams and minimum precisions.
		bool ReadSignature(const std::vector<u8>& data, bool wide, std::vector<Element>& out) {
			const size_t stride = wide ? 32 : 24;
			u32 head[2] = {};
			if (data.size() < 8) return false;
			memcpy(head, data.data(), 8);
			if (head[1] + (u64)head[0] * stride > data.size()) return false;
			for (u32 i = 0; i < head[0]; ++i) {
				u32 fields[8] = {};
				memcpy(fields, data.data() + head[1] + i * stride, stride);
				const u32* f = wide ? fields + 1 : fields;
				Element e;
				e.stream = wide ? fields[0] : 0;
				for (size_t k = f[0]; k < data.size() && data[k]; ++k) e.name += (char)data[k];
				e.index = f[1];
				e.system = f[2];
				e.component = f[3];
				e.reg = f[4];
				e.mask = f[5] & 0xFF;
				e.used = f[5] >> 8 & 0xFF;
				e.precision = wide ? f[6] : 0;
				out.push_back(e);
			}
			return true;
		}

		std::vector<u8> WriteSignature(const std::vector<Element>& elements, bool wide) {
			const u32 stride = wide ? 32 : 24;
			std::vector<u8> strings;
			std::map<std::string, u32> names;
			const u32 first = 8 + (u32)elements.size() * stride;
			for (const Element& e : elements)
				if (!names.count(e.name)) {
					names[e.name] = first + (u32)strings.size();
					strings.insert(strings.end(), e.name.begin(), e.name.end());
					strings.push_back(0);
				}
			std::vector<u8> out(first, 0);
			const u32 head[2] = {(u32)elements.size(), 8};
			memcpy(out.data(), head, 8);
			for (size_t i = 0; i < elements.size(); ++i) {
				const Element& e = elements[i];
				const u32 narrow[6] = {names[e.name], e.index, e.system, e.component, e.reg, e.mask | e.used << 8};
				const u32 full[8] = {e.stream, names[e.name], e.index, e.system, e.component, e.reg, e.mask | e.used << 8, e.precision};
				memcpy(out.data() + 8 + i * stride, wide ? full : narrow, stride);
			}
			out.insert(out.end(), strings.begin(), strings.end());
			while (out.size() % 4) out.push_back(0xAB);
			return out;
		}

		DxbcChunk* Find(std::vector<DxbcChunk>& chunks, const char* fourcc) {
			for (DxbcChunk& chunk : chunks)
				if (chunk.fourcc == FourCC(fourcc)) return &chunk;
			return nullptr;
		}

		// A system value the selection reads: its input register and component.
		struct Input
		{
			u32 reg = 0;
			u32 component = 0;
		};

		class Rewriter
		{
		public:
			Rewriter(const DxbcTarget& target, DxbcInstrumented& out) : target_(target), out_(out) {}

			bool Run(std::vector<DxbcChunk>& chunks) {
				DxbcChunk* program = Find(chunks, "SHEX");
				if (!program) return Problem(Find(chunks, "SHDR") ? "the shader is Shader Model 4, which has no UAVs for the trace" : "the container has no shader program");
				if (!Parse(program->data)) return false;
				DxbcChunk* inputs = Find(chunks, "ISGN");
				const bool wide = !inputs && (inputs = Find(chunks, "ISG1")) != nullptr;
				std::vector<Element> elements;
				if (inputs && !ReadSignature(inputs->data, wide, elements)) return Problem("the input signature is damaged");
				if (!Choose(elements)) return false;
				if (!Rewrite()) return false;
				program->data.resize(code_.size() * 4);
				memcpy(program->data.data(), code_.data(), code_.size() * 4);
				if (inputs) inputs->data = WriteSignature(elements, wide);
				else if (!elements.empty()) chunks.push_back({FourCC("ISGN"), WriteSignature(elements, false)});
				if (kind_ == kVertex) {
					DxbcChunk* features = Find(chunks, "SFI0");
					if (!features) {
						chunks.push_back({FourCC("SFI0"), std::vector<u8>(8, 0)});
						features = &chunks.back();
					}
					features->data[0] |= 4;     // D3D_SHADER_REQUIRES_UAVS_AT_EVERY_STAGE
				}
				// The replay binds its own root signature, the shader's plus the trace.
				chunks.erase(std::remove_if(chunks.begin(), chunks.end(), [](const DxbcChunk& c) { return c.fourcc == FourCC("RTS0"); }), chunks.end());
				return true;
			}

		private:
			bool Problem(const std::string& text) {
				out_.problem = text;
				return false;
			}

			bool Parse(const std::vector<u8>& data) {
				if (data.size() < 8 || data.size() % 4) return Problem("the shader program is damaged");
				words_.resize(data.size() / 4);
				memcpy(words_.data(), data.data(), data.size());
				const u32 version = words_[0];
				kind_ = version >> 16;
				if ((version >> 4 & 0xF) != 5 || (version & 0xF) != 1)
					return Problem(Format("the shader is Shader Model %u.%u; the debugger instruments 5.1 so far", version >> 4 & 0xF, version & 0xF));
				if (kind_ != kPixel && kind_ != kVertex && kind_ != kCompute) return Problem("the debugger steps pixel, vertex and compute shaders");
				const u32 count = std::min<u32>(words_[1], (u32)words_.size());
				for (u32 at = 2; at < count;) {
					const u32 op = words_[at] & 0x7FF;
					const u32 length = op == kOpCustomData ? (at + 1 < count ? words_[at + 1] : 0) : words_[at] >> 24 & 0x7F;
					if (!length || at + length > count) return Problem("the shader program is damaged");
					if (op == kOpCall || op == kOpCallc || op == kOpLabel || op == kOpInterfaceCall) return Problem("the shader calls subroutines, which the debugger does not instrument yet");
					list_.push_back({at, length, op});
					at += length;
				}
				codeStart_ = list_.size();
				for (size_t i = 0; i < list_.size(); ++i) {
					const Instruction& in = list_[i];
					if (!IsDeclaration(in.op) && in.op != kOpCustomData) {
						codeStart_ = i;
						break;
					}
					const u32* w = &words_[in.at];
					if (in.op == kOpDclTemps && in.length >= 2) {
						temps_ = w[1];
						tempsAt_ = i;
					}
					if ((in.op == kOpDclUavTyped || in.op == kOpDclUavRaw || in.op == kOpDclUavStructured) && in.length >= 3) uav_ = std::max(uav_, w[2] + 1);
					if (in.op == kOpDclInputPsSiv && in.length >= 4 && w[3] == kPosition) declared_[kPosition] = i;
					if (in.op == kOpDclInputSgv && in.length >= 4) declared_[w[3]] = i;
					if (in.op == kOpDclInput && in.length >= 2 && (w[1] >> 12 & 0xFF) == kThreadId) declared_[kThreadId] = i;
					if (IsInputDeclaration(in.op) && in.length >= 2) {
						const u32 type = w[1] >> 12 & 0xFF, dimension = w[1] >> 20 & 3;
						if ((type == kInput && dimension == 1 && in.length >= 3) || ((type == kThreadId || type == kThreadGroupId || type == kThreadInGroup) && dimension == 0))
							declaredInputs_.push_back(i);
					}
				}
				if (codeStart_ == list_.size()) return Problem("the shader program has no instructions");
				return true;
			}

			// The inputs the selection reads, declared and in the signature.
			bool Choose(std::vector<Element>& elements) {
				select_ = temps_;
				log_ = temps_ + 1;
				spare_[0] = temps_ + 2;
				spare_[1] = temps_ + 3;
				if (kind_ == kPixel) {
					Element* position = nullptr;
					for (Element& e : elements)
						if (e.system == kPosition) position = &e;
					if (!position) return Problem("the pixel shader does not take SV_Position, which picks the pixel");
					position->used |= 0x3;
					position_ = position->reg;
					auto declared = declared_.find(kPosition);
					if (declared != declared_.end()) widen_[declared->second] = 0x3;
					else inputs_.push_back({kOpDclInputPsSiv | 4u << 11 | 4u << 24, MaskOp(kInput, 0x3, 1), position_, kPosition});
				}
				else if (kind_ == kVertex) {
					vertex_ = SystemInput(elements, kVertexId, "SV_VertexID");
					instance_ = SystemInput(elements, kInstanceId, "SV_InstanceID");
				}
				else {
					auto declared = declared_.find(kThreadId);
					if (declared != declared_.end()) widen_[declared->second] = 0x7;
					else inputs_.push_back({kOpDclInput | 2u << 24, MaskOp(kThreadId, 0x7, 0)});
				}
				elements_ = elements;
				return true;
			}

			Input SystemInput(std::vector<Element>& elements, u32 system, const char* name) {
				Input input;
				bool found = false;
				u32 next = 0;
				for (const Element& e : elements) {
					next = std::max(next, e.reg + 1);
					if (e.system != system) continue;
					input.reg = e.reg;
					for (input.component = 0; input.component < 3 && !(e.mask >> input.component & 1);) ++input.component;
					found = true;
				}
				if (!found) {
					input.reg = next;
					Element added;
					added.name = name;
					added.system = system;
					added.component = 1;    // uint
					added.reg = next;
					added.mask = added.used = 1;
					elements.push_back(added);
				}
				if (!declared_.count(system)) inputs_.push_back({kOpDclInputSgv | 4u << 24, MaskOp(kInput, 1u << input.component, 1), input.reg, system});
				return input;
			}

			bool Rewrite() {
				// The trace's declaration follows the resources, new inputs follow the inputs.
				size_t uavAt = 0;
				while (uavAt < codeStart_ && IsResourceDeclaration(list_[uavAt].op)) ++uavAt;
				size_t inputAt = uavAt;
				while (inputAt < codeStart_ && IsInputDeclaration(list_[inputAt].op)) ++inputAt;
				code_.assign(words_.begin(), words_.begin() + 2);
				for (size_t i = 0; i < list_.size(); ++i) {
					if (i == uavAt) Emit(kOpDclUavRaw, {SwizzleOp(kUav, kXyzw, 3), uav_, 0, 0, target_.space});
					if (i == inputAt)
						for (const std::vector<u32>& declaration : inputs_) code_.insert(code_.end(), declaration.begin(), declaration.end());
					if (i == codeStart_) {
						if (tempsAt_ == SIZE_MAX) Emit(kOpDclTemps, {4});
						SelectInvocation();
						LogInputs();
					}
					const Instruction& in = list_[i];
					if (i < codeStart_ || in.op == kOpCustomData) Declare(i);
					else if (!Instrument(in)) return false;
				}
				code_[1] = (u32)code_.size();
				return true;
			}

			void Declare(size_t i) {
				const Instruction& in = list_[i];
				const size_t at = code_.size();
				code_.insert(code_.end(), words_.begin() + in.at, words_.begin() + in.at + in.length);
				if (i == tempsAt_) code_[at + 1] = temps_ + 4;
				auto widen = widen_.find(i);
				if (widen != widen_.end()) code_[at + 1] |= widen->second << 4;
			}

			void Emit(u32 op, std::initializer_list<u32> operands, u32 controls = 0) {
				code_.push_back(op | controls | (u32)(operands.size() + 1) << 24);
				code_.insert(code_.end(), operands);
			}

			void EmitWords(u32 op, const std::vector<u32>& operands) {
				code_.push_back(op | (u32)(operands.size() + 1) << 24);
				code_.insert(code_.end(), operands.begin(), operands.end());
			}

			// select.x: chosen; .y: the region's first byte; .z: scratch; .w: records written.
			void SelectInvocation() {
				const u32 s = select_;
				if (kind_ == kPixel) {
					Emit(kOpFtou, {MaskOp(kTemp, 0x3, 1), s, SwizzleOp(kInput, 0x04, 1), position_});
					Emit(kOpIeq, {MaskOp(kTemp, 0x3, 1), s, SwizzleOp(kTemp, 0x04, 1), s, kVector, target_.x, target_.y, 0, 0});
					Emit(kOpAnd, {MaskOp(kTemp, 0x1, 1), s, OneOp(kTemp, 0, 1), s, OneOp(kTemp, 1, 1), s});
				}
				else if (kind_ == kVertex) {
					Emit(kOpIeq, {MaskOp(kTemp, 0x1, 1), s, OneOp(kInput, vertex_.component, 1), vertex_.reg, kScalar, target_.vertex});
					Emit(kOpIeq, {MaskOp(kTemp, 0x2, 1), s, OneOp(kInput, instance_.component, 1), instance_.reg, kScalar, target_.instance});
					Emit(kOpAnd, {MaskOp(kTemp, 0x1, 1), s, OneOp(kTemp, 0, 1), s, OneOp(kTemp, 1, 1), s});
				}
				else {
					Emit(kOpIeq, {MaskOp(kTemp, 0x7, 1), s, SwizzleOp(kThreadId, 0x24, 0), kVector, target_.thread[0], target_.thread[1], target_.thread[2], 0});
					Emit(kOpAnd, {MaskOp(kTemp, 0x1, 1), s, OneOp(kTemp, 0, 1), s, OneOp(kTemp, 1, 1), s});
					Emit(kOpAnd, {MaskOp(kTemp, 0x1, 1), s, OneOp(kTemp, 0, 1), s, OneOp(kTemp, 2, 1), s});
				}
				Emit(kOpMov, {MaskOp(kTemp, 0xA, 1), s, kVector, 0, 0, 0, 0});
				Emit(kOpIf, {OneOp(kTemp, 0, 1), s}, kNonZero);
				Emit(kOpImmAtomicIadd, {MaskOp(kTemp, 0x2, 1), s, kUav << 12 | 2 << 20, uav_, 0, kScalar, 0, kScalar, kDxbcRecords * kDxbcRecordBytes});
				Emit(kOpUlt, {MaskOp(kTemp, 0x4, 1), s, OneOp(kTemp, 1, 1), s, kScalar, kDxbcTraceBytes});
				Emit(kOpAnd, {MaskOp(kTemp, 0x1, 1), s, OneOp(kTemp, 0, 1), s, OneOp(kTemp, 2, 1), s});
				Emit(kOpEndif, {});
			}

			// Appends a record of site `site` with `value` (a source operand's tokens) stored by `mask`, when the
			// invocation was chosen and its region has room.
			void Log(u32 site, const std::vector<u32>& value, u32 mask) {
				const u32 s = select_, l = log_;
				Emit(kOpUlt, {MaskOp(kTemp, 0x1, 1), l, OneOp(kTemp, 3, 1), s, kScalar, kDxbcRecords});
				Emit(kOpAnd, {MaskOp(kTemp, 0x1, 1), l, OneOp(kTemp, 0, 1), l, OneOp(kTemp, 0, 1), s});
				Emit(kOpIf, {OneOp(kTemp, 0, 1), l}, kNonZero);
				Emit(kOpImad, {MaskOp(kTemp, 0x2, 1), l, OneOp(kTemp, 3, 1), s, kScalar, kDxbcRecordBytes, OneOp(kTemp, 1, 1), s});
				Emit(kOpStoreRaw, {MaskOp(kUav, 0x1, 2), uav_, 0, OneOp(kTemp, 1, 1), l, kScalar, site + 1});
				Emit(kOpIadd, {MaskOp(kTemp, 0x2, 1), l, OneOp(kTemp, 1, 1), l, kScalar, 4});
				std::vector<u32> store = {MaskOp(kUav, mask, 2), uav_, 0, OneOp(kTemp, 1, 1), l};
				store.insert(store.end(), value.begin(), value.end());
				EmitWords(kOpStoreRaw, store);
				Emit(kOpIadd, {MaskOp(kTemp, 0x8, 1), s, OneOp(kTemp, 3, 1), s, kScalar, 1});
				Emit(kOpEndif, {});
			}

			std::string NameOf(const Operand& o) const {
				switch (o.type) {
					case kTemp: return Format("r%u", o.index[0]) + MaskText(o.mask);
					case kIndexableTemp: return (o.immediate ? Format("x%u[%u]", o.index[0], o.index[1]) : Format("x%u[i]", o.index[0])) + MaskText(o.mask);
					case kOutput: return Format("o%u", o.index[0]) + MaskText(o.components == 2 ? o.mask : 1);
					case kOutputDepth: return "oDepth";
					case kOutputDepthAtLeast: return "oDepthGE";
					case kOutputDepthAtMost: return "oDepthLE";
					case kOutputCoverage: return "oMask";
					case kOutputStencilRef: return "oStencilRef";
					case kUav: return Format("U%u", o.index[0]);
					case kThreadShared: return Format("g%u", o.index[0]);
					default: return "?";
				}
			}

			u32 Add(const DxbcSite& site) {
				out_.sites.push_back(site);
				return (u32)out_.sites.size() - 1;
			}

			u32 AddSite(const Instruction& in, const Operand& o, u32 mask, u32 type, bool store) {
				DxbcSite site;
				site.offset = in.at * 4;
				site.name = NameOf(o);
				site.mask = mask;
				site.type = type;
				site.store = store;
				return Add(site);
			}

			u32 InputType(u32 reg, u32 mask) const {
				for (const Element& e : elements_)
					if (e.reg == reg && (e.mask & mask)) return e.component == 1 ? kUint : e.component == 2 ? kInt : kFloat;
				return kFloat;
			}

			// The inputs the shader declares, logged first, at their declarations: the invocation's values as it starts.
			// Components a declaration leaves out read as one it names.
			void LogInputs() {
				for (size_t i : declaredInputs_) {
					const u32* w = &words_[list_[i].at];
					const u32 type = w[1] >> 12 & 0xFF;
					auto widen = widen_.find(i);
					const u32 mask = (w[1] >> 4 & 0xF) | (widen != widen_.end() ? widen->second : 0);
					if (!mask) continue;
					u32 first = 0;
					while (!(mask >> first & 1)) ++first;
					u32 swizzle = 0;
					for (u32 k = 0; k < 4; ++k) swizzle |= (mask >> k & 1 ? k : first) << (2 * k);
					DxbcSite site;
					site.offset = list_[i].at * 4;
					site.mask = mask;
					if (type == kInput) {
						site.name = Format("v%u", w[2]) + MaskText(mask);
						site.type = InputType(w[2], mask);
						Log(Add(site), {SwizzleOp(kInput, swizzle, 1), w[2]}, 0xF);
						continue;
					}
					site.name = std::string(type == kThreadId ? "vThreadID" : type == kThreadGroupId ? "vThreadGroupID" : "vThreadIDInGroup") + MaskText(mask);
					site.type = kUint;
					Log(Add(site), {SwizzleOp(type, swizzle, 0)}, 0xF);
				}
			}

			bool Instrument(const Instruction& in) {
				const u32 end = in.at + in.length;
				u32 p = in.at + 1;
				for (bool more = words_[in.at] >> 31; more && p < end;) more = words_[p++] >> 31;
				const u32 first = p;
				std::vector<Operand> operands;
				while (p < end) {
					Operand o;
					if (!ReadOperand(words_, end, p, o)) return Problem(Format("the instruction at offset %u could not be read", in.at * 4));
					operands.push_back(o);
				}

				// What the instruction writes: registers it logs (k: the operand), outputs it writes through a spare temp.
				struct Logged
				{
					u32 operand;
					bool output;
				};
				std::vector<Logged> logged;
				const bool store = in.op == kOpStoreUavTyped || in.op == kOpStoreRaw || in.op == kOpStoreStructured;
				if (!store && !IsFlow(in.op)) {
					const u32 results = std::min<u32>(HasTwoResults(in.op) ? 2 : 1, (u32)operands.size());
					for (u32 k = 0; k < results; ++k) {
						const Operand& o = operands[k];
						const bool byMask = o.components == 2 && o.mode == 0;
						if (o.type == kTemp && byMask && o.dimension == 1 && o.immediate) logged.push_back({k, false});
						else if (o.type == kIndexableTemp && byMask) logged.push_back({k, false});
						else if (IsOutput(o.type) && o.immediate && o.dimension <= 1 && (byMask || o.components == 1)) logged.push_back({k, true});
					}
				}
				auto output = [&](u32 k) {
					for (const Logged& l : logged)
						if (l.operand == k && l.output) return true;
					return false;
				};

				// The instruction, with outputs written to spare temps.
				const size_t start = code_.size();
				code_.insert(code_.end(), words_.begin() + in.at, words_.begin() + first);
				for (u32 k = 0; k < operands.size(); ++k) {
					const Operand& o = operands[k];
					if (output(k)) {
						code_.push_back(MaskOp(kTemp, o.components == 2 ? o.mask : 1, 1));
						code_.push_back(spare_[k]);
					}
					else code_.insert(code_.end(), words_.begin() + o.begin, words_.begin() + o.end);
				}
				code_[start] = (code_[start] & ~(0x7Fu << 24)) | (u32)(code_.size() - start) << 24;

				if (store && operands.size() >= 3) {
					const Operand& target = operands[0];
					const Operand& value = operands.back();
					const u32 mask = in.op != kOpStoreUavTyped && target.components == 2 && target.mode == 0 ? target.mask : 0xF;
					Log(AddSite(in, target, mask, kRaw, true), std::vector<u32>(words_.begin() + value.begin, words_.begin() + value.end), mask);
				}
				for (const Logged& l : logged) {
					const Operand& o = operands[l.operand];
					const u32 mask = o.components == 2 ? o.mask : 1;
					std::vector<u32> value;
					if (l.output || o.type == kTemp) value = {SwizzleOp(kTemp, kXyzw, 1), l.output ? spare_[l.operand] : o.index[0]};
					else {
						value.assign(words_.begin() + o.begin, words_.begin() + o.end);
						value[0] = (value[0] & ~0xFFCu) | 1u << 2 | kXyzw << 4;
					}
					Log(AddSite(in, o, mask, ResultType(in.op), false), value, 0xF);
				}
				for (const Logged& l : logged) {
					if (!l.output) continue;
					const Operand& o = operands[l.operand];
					std::vector<u32> move(words_.begin() + o.begin, words_.begin() + o.end);
					move.push_back(o.components == 2 ? SwizzleOp(kTemp, kXyzw, 1) : OneOp(kTemp, 0, 1));
					move.push_back(spare_[l.operand]);
					EmitWords(kOpMov, move);
				}
				return true;
			}

			const DxbcTarget& target_;
			DxbcInstrumented& out_;
			std::vector<u32> words_;
			std::vector<Instruction> list_;
			std::vector<u32> code_;
			u32 kind_ = 0;
			size_t codeStart_ = 0;
			u32 temps_ = 0;
			size_t tempsAt_ = SIZE_MAX;
			u32 uav_ = 0;                                // the trace's UAV id: after the shader's own
			std::map<u32, size_t> declared_;             // system value (or thread id) -> its input declaration
			std::map<size_t, u32> widen_;                // declaration -> components it must add
			std::vector<size_t> declaredInputs_;         // the shader's input declarations
			std::vector<Element> elements_;              // the input signature, as instrumented
			std::vector<std::vector<u32>> inputs_;       // input declarations to add
			u32 position_ = 0;
			Input vertex_, instance_;
			u32 select_ = 0, log_ = 0, spare_[2] = {};
		};
	}

	u32 FourCC(const char* text) { return (u32)(u8)text[0] | (u32)(u8)text[1] << 8 | (u32)(u8)text[2] << 16 | (u32)(u8)text[3] << 24; }

	void DxbcChecksum(const u8* bytes, size_t size, u32 checksum[4]) {
		const u8* data = bytes + 20;
		const u32 length = (u32)(size - 20), bits = length * 8;
		u32 state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
		u32 x[16];
		const u32 full = length / 64, left = length % 64;
		for (u32 i = 0; i < full; ++i) {
			memcpy(x, data + (size_t)i * 64, 64);
			Md5Block(state, x);
		}
		// DXBC's padding: the length in bits leads the last block, not trails it, and its last word is bits / 4 | 1.
		const u8* rest = data + (size_t)full * 64;
		memset(x, 0, sizeof(x));
		if (left >= 56) {
			memcpy(x, rest, left);
			((u8*)x)[left] = 0x80;
			Md5Block(state, x);
			memset(x, 0, sizeof(x));
			x[0] = bits;
		}
		else {
			x[0] = bits;
			memcpy((u8*)x + 4, rest, left);
			((u8*)x)[4 + left] = 0x80;
		}
		x[15] = bits >> 2 | 1;
		Md5Block(state, x);
		memcpy(checksum, state, 16);
	}

	bool ReadDxbc(const u8* bytes, size_t size, std::vector<DxbcChunk>& chunks) {
		chunks.clear();
		if (size < 32 || memcmp(bytes, "DXBC", 4) != 0) return false;
		u32 total = 0, count = 0;
		memcpy(&total, bytes + 24, 4);
		memcpy(&count, bytes + 28, 4);
		if (total > size || 32 + (u64)count * 4 > total) return false;
		for (u32 i = 0; i < count; ++i) {
			u32 offset = 0, head[2] = {};
			memcpy(&offset, bytes + 32 + i * 4, 4);
			if ((u64)offset + 8 > total) return false;
			memcpy(head, bytes + offset, 8);
			if ((u64)offset + 8 + head[1] > total) return false;
			chunks.push_back({head[0], std::vector<u8>(bytes + offset + 8, bytes + offset + 8 + head[1])});
		}
		return true;
	}

	std::vector<u8> WriteDxbc(const std::vector<DxbcChunk>& chunks) {
		const u32 count = (u32)chunks.size();
		std::vector<u32> offsets;
		u32 total = 32 + count * 4;
		for (const DxbcChunk& chunk : chunks) {
			offsets.push_back(total);
			total += 8 + (u32)AlignUp(chunk.data.size(), 4);
		}
		std::vector<u8> out(total, 0);
		memcpy(out.data(), "DXBC", 4);
		const u32 header[3] = {1, total, count};
		memcpy(out.data() + 20, header, 12);
		for (u32 i = 0; i < count; ++i) {
			memcpy(out.data() + 32 + i * 4, &offsets[i], 4);
			const u32 head[2] = {chunks[i].fourcc, (u32)chunks[i].data.size()};
			memcpy(out.data() + offsets[i], head, 8);
			if (!chunks[i].data.empty()) memcpy(out.data() + offsets[i] + 8, chunks[i].data.data(), chunks[i].data.size());
		}
		u32 checksum[4];
		DxbcChecksum(out.data(), out.size(), checksum);
		memcpy(out.data() + 4, checksum, 16);
		return out;
	}

	bool InstrumentDxbc(const u8* bytes, size_t size, const DxbcTarget& target, DxbcInstrumented& out) {
		out = DxbcInstrumented();
		std::vector<DxbcChunk> chunks;
		if (!ReadDxbc(bytes, size, chunks)) {
			out.problem = "not a DXBC container";
			return false;
		}
		Rewriter rewriter(target, out);
		if (!rewriter.Run(chunks)) return false;
		out.bytes = WriteDxbc(chunks);
		return true;
	}

	bool ReadDxbcSignature(const u8* bytes, size_t size, char which, std::vector<DxbcElement>& out) {
		out.clear();
		std::vector<DxbcChunk> chunks;
		if (!ReadDxbc(bytes, size, chunks)) return false;
		const std::string narrow = std::string(1, which) + "SGN", wide = std::string(1, which) + "SG1";
		for (const DxbcChunk& chunk : chunks) {
			const bool isWide = chunk.fourcc == FourCC(wide.c_str());
			if (chunk.fourcc != FourCC(narrow.c_str()) && !isWide) continue;
			std::vector<Element> elements;
			if (!ReadSignature(chunk.data, isWide, elements)) return false;
			for (const Element& e : elements) out.push_back({e.name, e.index, e.system, e.component, e.reg, e.mask});
			return true;
		}
		return false;
	}

	std::string DxbcValueText(const DxbcSite& site, const u32 values[4]) {
		std::string text;
		for (u32 k = 0; k < 4; ++k) {
			if (!(site.mask >> k & 1)) continue;
			if (!text.empty()) text += ", ";
			const u32 w = values[k];
			float f;
			memcpy(&f, &w, 4);
			switch (site.type) {
				case kInt: text += Format("%d", (i32)w); break;
				case kUint: text += Format("%u", w); break;
				case kBool: text += w ? "true" : "false"; break;
				case kRaw:
					// Bits of no known type: those with no exponent read as integers, the rest as floats.
					if (!(w & 0x7F800000)) text += Format("%u", w);
					else if ((w & 0x7F800000) == 0x7F800000) text += Format("0x%08x", w);
					else text += Format("%.6g", f);
					break;
				default: text += std::isnan(f) ? "NaN" : std::isinf(f) ? (f > 0 ? "Inf" : "-Inf") : Format("%.6g", f); break;
			}
		}
		return text;
	}
}
