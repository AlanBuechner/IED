#include "vkinstrument.h"
#include "vkspirv.h"

#include <algorithm>
#include <map>

namespace fb::replay::vulkan
{
	namespace
	{
		// The opcodes this file reads or writes.
		constexpr u32 kOpUndef = 1, kOpEntryPoint = 15, kOpTypeBool = 20, kOpTypeInt = 21, kOpTypeFloat = 22, kOpTypeVector = 23,
			kOpTypeRuntimeArray = 29, kOpTypeStruct = 30, kOpTypePointer = 32, kOpConstantFalse = 42, kOpConstant = 43, kOpFunction = 54,
			kOpFunctionEnd = 56, kOpVariable = 59, kOpLoad = 61, kOpStore = 62, kOpAccessChain = 65, kOpDecorate = 71, kOpMemberDecorate = 72,
			kOpCompositeExtract = 81, kOpConvertFToU = 109, kOpBitcast = 124, kOpIAdd = 128, kOpIMul = 132, kOpLogicalAnd = 167, kOpSelect = 169,
			kOpIEqual = 170, kOpULessThan = 176, kOpULessThanEqual = 178, kOpAtomicIAdd = 234, kOpPhi = 245, kOpLabel = 248, kOpLine = 8,
			kOpNoLine = 317;
		// Storage classes, decorations and built-ins.
		constexpr u32 kInput = 1, kUniform = 2, kPrivate = 6, kStorageBuffer = 12;
		constexpr u32 kBlock = 2, kBufferBlock = 3, kArrayStride = 6, kBuiltIn = 11, kBinding = 33, kDescriptorSet = 34, kOffset = 35;
		constexpr u32 kFragCoord = 15, kGlobalInvocationId = 28, kVertexIndex = 42, kInstanceIndex = 43;

		class Emitter
		{
		public:
			void Emit(u32 op, std::initializer_list<u32> operands) {
				words.push_back(((u32)operands.size() + 1) << 16 | op);
				words.insert(words.end(), operands);
			}
			std::vector<u32> words;
		};

		// How a value of a type logs: the type's kind (0 float, 1 int, 2 uint, 3 bool), its components, its scalar type.
		struct ValueKind
		{
			bool loggable = false;
			u32 kind = 0;
			u32 components = 1;
			u32 scalar = 0;
		};

		std::string Literal(const u32* w, size_t length) {
			std::string text;
			for (size_t i = 0; i < length; ++i)
				for (int b = 0; b < 4; ++b) {
					const char c = (char)((w[i] >> (b * 8)) & 0xFF);
					if (!c) return text;
					text += c;
				}
			return text;
		}

		size_t LiteralWords(const u32* w, size_t length) {
			for (size_t i = 0; i < length; ++i)
				if (!(w[i] & 0xFF000000u) || !(w[i] & 0xFF0000u) || !(w[i] & 0xFF00u) || !(w[i] & 0xFFu)) return i + 1;
			return length;
		}

		class Rewriter
		{
		public:
			Rewriter(const u32* words, size_t count, const TraceTarget& target) : words_(words), count_(count), target_(target), bound_(words[3]) {}

			bool Run(const std::string& entry, Instrumented& out) {
				if (!Read(entry, out.problem)) return false;
				const u32 version = words_[1];
				storage_ = version >= 0x00010300 ? kStorageBuffer : kUniform;
				Declare();
				std::vector<u32> bodies;
				if (!Rewrite(bodies, out)) return false;
				// The entry point lists every input it reads, and from SPIR-V 1.4 every global it uses.
				if (version >= 0x00010400) interface_.insert(interface_.end(), {trace_, selected_, base_, cursor_});
				out.words.assign(words_, words_ + 5);
				out.words[3] = bound_;
				for (size_t i = 0; i < list_.size(); ++i) {
					if (i == firstDeclaration_) out.words.insert(out.words.end(), notes_.words.begin(), notes_.words.end());
					if (i == firstFunction_) break;
					const u32* w = words_ + list_[i].at;
					if (i == entryIndex_) {
						out.words.push_back((u32)(list_[i].length + interface_.size()) << 16 | kOpEntryPoint);
						out.words.insert(out.words.end(), w + 1, w + list_[i].length);
						out.words.insert(out.words.end(), interface_.begin(), interface_.end());
						continue;
					}
					out.words.insert(out.words.end(), w, w + list_[i].length);
				}
				out.words.insert(out.words.end(), globals_.words.begin(), globals_.words.end());
				out.words.insert(out.words.end(), bodies.begin(), bodies.end());
				return true;
			}

		private:
			struct Instruction
			{
				size_t at;
				u32 op;
				u32 length;
			};

			u32 Fresh() { return bound_++; }

			bool Read(const std::string& entry, std::string& problem) {
				if (count_ < 5 || words_[0] != 0x07230203) {
					problem = "not SPIR-V";
					return false;
				}
				for (size_t at = 5; at < count_;) {
					const u32 length = words_[at] >> 16, op = words_[at] & 0xFFFF;
					if (!length || at + length > count_) {
						problem = "the SPIR-V is damaged";
						return false;
					}
					list_.push_back({at, op, length});
					at += length;
				}
				for (size_t i = 0; i < list_.size(); ++i) {
					const u32* w = words_ + list_[i].at;
					const u32 op = list_[i].op, length = list_[i].length;
					if (op == kOpEntryPoint && length >= 4 && w[1] == target_.model && Literal(w + 3, length - 3) == entry) {
						entryIndex_ = i;
						entryFunction_ = w[2];
						for (size_t k = 3 + LiteralWords(w + 3, length - 3); k < length; ++k) listed_.push_back(w[k]);
					}
					if (op == kOpDecorate && length >= 4 && w[2] == kBuiltIn) builtins_[w[3]] = w[1];
					const bool declaration = (op >= 19 && op <= 39) || (op >= 41 && op <= 52) || op == kOpVariable || op == kOpUndef;
					if (firstFunction_ == SIZE_MAX) {
						if ((declaration || op == kOpFunction) && firstDeclaration_ == SIZE_MAX) firstDeclaration_ = i;
						if (op == kOpFunction) firstFunction_ = i;
						if (op == kOpVariable && length >= 4) pointers_[w[2]] = w[1];
					}
					if (op >= 19 && op <= 39 && length >= 2) types_[w[1]] = Definition(op, w + 2, length - 2);
					bool typed = false;
					if (SpirvResult(op, typed) && typed && length >= 3) results_[w[2]] = w[1];
				}
				if (entryIndex_ == SIZE_MAX) {
					problem = "the module has no entry point " + entry + " for this stage";
					return false;
				}
				if (firstFunction_ == SIZE_MAX) {
					problem = "the module has no functions";
					return false;
				}
				return true;
			}

			static std::vector<u32> Definition(u32 op, const u32* operands, size_t count) {
				std::vector<u32> definition = {op};
				definition.insert(definition.end(), operands, operands + count);
				return definition;
			}

			// A type the module declares, else one declared here.
			u32 Type(std::vector<u32> definition) {
				for (const auto& type : types_)
					if (type.second == definition) return type.first;
				const u32 id = Fresh();
				globals_.words.push_back((u32)(definition.size() + 1) << 16 | definition[0]);
				globals_.words.push_back(id);
				globals_.words.insert(globals_.words.end(), definition.begin() + 1, definition.end());
				types_[id] = definition;
				return id;
			}

			u32 Uint(u32 value) {
				auto found = constants_.find(value);
				if (found != constants_.end()) return found->second;
				const u32 id = Fresh();
				globals_.Emit(kOpConstant, {uint_, id, value});
				constants_[value] = id;
				return id;
			}

			u32 Pointee(u32 variable) {
				const std::vector<u32>& pointer = types_[pointers_[variable]];
				return pointer.size() >= 3 ? pointer[2] : 0;
			}

			// The variable of a built-in input, declared when the module has none, listed by the entry point either way.
			u32 Input(u32 builtin, u32 type) {
				auto found = builtins_.find(builtin);
				u32 variable = found != builtins_.end() ? found->second : 0;
				if (!variable) {
					const u32 pointer = Fresh(), id = Fresh();
					globals_.Emit(kOpTypePointer, {pointer, kInput, type});
					globals_.Emit(kOpVariable, {pointer, id, kInput});
					notes_.Emit(kOpDecorate, {id, kBuiltIn, builtin});
					types_[pointer] = {kOpTypePointer, kInput, type};
					pointers_[id] = pointer;
					variable = id;
				}
				if (std::find(listed_.begin(), listed_.end(), variable) == listed_.end()) interface_.push_back(variable);
				return variable;
			}

			void Declare() {
				uint_ = Type({kOpTypeInt, 32, 0});
				bool_ = Type({kOpTypeBool});
				const u32 array = Fresh();
				globals_.Emit(kOpTypeRuntimeArray, {array, uint_});
				notes_.Emit(kOpDecorate, {array, kArrayStride, 4});
				const u32 block = Fresh();
				globals_.Emit(kOpTypeStruct, {block, array});
				notes_.Emit(kOpMemberDecorate, {block, 0, kOffset, 0});
				notes_.Emit(kOpDecorate, {block, storage_ == kStorageBuffer ? kBlock : kBufferBlock});
				const u32 blockPointer = Fresh();
				globals_.Emit(kOpTypePointer, {blockPointer, storage_, block});
				uintPointer_ = Fresh();
				globals_.Emit(kOpTypePointer, {uintPointer_, storage_, uint_});
				trace_ = Fresh();
				globals_.Emit(kOpVariable, {blockPointer, trace_, storage_});
				notes_.Emit(kOpDecorate, {trace_, kDescriptorSet, target_.set});
				notes_.Emit(kOpDecorate, {trace_, kBinding, 0});
				const u32 boolPrivate = Fresh(), uintPrivate = Fresh(), no = Fresh();
				globals_.Emit(kOpTypePointer, {boolPrivate, kPrivate, bool_});
				globals_.Emit(kOpTypePointer, {uintPrivate, kPrivate, uint_});
				globals_.Emit(kOpConstantFalse, {bool_, no});
				const u32 zero = Uint(0);
				selected_ = Fresh();
				base_ = Fresh();
				cursor_ = Fresh();
				globals_.Emit(kOpVariable, {boolPrivate, selected_, kPrivate, no});
				globals_.Emit(kOpVariable, {uintPrivate, base_, kPrivate, zero});
				globals_.Emit(kOpVariable, {uintPrivate, cursor_, kPrivate, zero});
				if (target_.model == 4) {
					const u32 floatType = Type({kOpTypeFloat, 32});
					coords_ = Input(kFragCoord, Type({kOpTypeVector, floatType, 4}));
				}
				else if (target_.model == 0) {
					const u32 intType = Type({kOpTypeInt, 32, 1});
					vertexIndex_ = Input(kVertexIndex, intType);
					instanceIndex_ = Input(kInstanceIndex, intType);
				}
				else coords_ = Input(kGlobalInvocationId, Type({kOpTypeVector, uint_, 3}));
			}

			// A 32-bit word of an integer value of type `type` (an int is bitcast).
			u32 AsUint(Emitter& e, u32 value, u32 type) {
				if (type == uint_) return value;
				const u32 id = Fresh();
				e.Emit(kOpBitcast, {uint_, id, value});
				return id;
			}

			u32 Equal(Emitter& e, u32 a, u32 b) {
				const u32 id = Fresh();
				e.Emit(kOpIEqual, {bool_, id, a, b});
				return id;
			}

			u32 And(Emitter& e, u32 a, u32 b) {
				const u32 id = Fresh();
				e.Emit(kOpLogicalAnd, {bool_, id, a, b});
				return id;
			}

			// The entry point's first code: whether this invocation is the one chosen, and its region of the trace.
			void Choose(Emitter& e) {
				u32 chosen = 0;
				if (target_.model == 4) {
					const u32 vec = Pointee(coords_), scalar = types_[vec].size() >= 2 ? types_[vec][1] : 0;
					const u32 loaded = Fresh();
					e.Emit(kOpLoad, {vec, loaded, coords_});
					u32 axes[2];
					const u32 wanted[2] = {target_.x, target_.y};
					for (u32 k = 0; k < 2; ++k) {
						const u32 component = Fresh(), whole = Fresh();
						e.Emit(kOpCompositeExtract, {scalar, component, loaded, k});
						e.Emit(kOpConvertFToU, {uint_, whole, component});
						axes[k] = Equal(e, whole, Uint(wanted[k]));
					}
					chosen = And(e, axes[0], axes[1]);
				}
				else if (target_.model == 0) {
					u32 equal[2];
					const u32 variables[2] = {vertexIndex_, instanceIndex_}, wanted[2] = {target_.vertex, target_.instance};
					for (u32 k = 0; k < 2; ++k) {
						const u32 type = Pointee(variables[k]), loaded = Fresh();
						e.Emit(kOpLoad, {type, loaded, variables[k]});
						equal[k] = Equal(e, AsUint(e, loaded, type), Uint(wanted[k]));
					}
					chosen = And(e, equal[0], equal[1]);
				}
				else {
					const u32 vec = Pointee(coords_), loaded = Fresh();
					e.Emit(kOpLoad, {vec, loaded, coords_});
					u32 equal[3];
					for (u32 k = 0; k < 3; ++k) {
						const u32 component = Fresh();
						e.Emit(kOpCompositeExtract, {uint_, component, loaded, k});
						equal[k] = Equal(e, component, Uint(target_.thread[k]));
					}
					chosen = And(e, And(e, equal[0], equal[1]), equal[2]);
				}
				const u32 region = Uint(kTraceRecords * kTraceRecordWords);
				const u32 amount = Fresh(), counter = Fresh(), start = Fresh(), end = Fresh(), fits = Fresh();
				e.Emit(kOpSelect, {uint_, amount, chosen, region, Uint(0)});
				e.Emit(kOpAccessChain, {uintPointer_, counter, trace_, Uint(0), Uint(0)});
				e.Emit(kOpAtomicIAdd, {uint_, start, counter, Uint(1), Uint(0), amount});
				e.Emit(kOpIAdd, {uint_, end, start, region});
				e.Emit(kOpULessThanEqual, {bool_, fits, end, Uint(kTraceWords)});
				e.Emit(kOpStore, {selected_, And(e, chosen, fits)});
				e.Emit(kOpStore, {base_, start});
				e.Emit(kOpStore, {cursor_, Uint(0)});
			}

			ValueKind Kind(u32 type) {
				auto found = types_.find(type);
				if (found == types_.end()) return {};
				const std::vector<u32>& t = found->second;
				if (t[0] == kOpTypeBool) return {true, 3, 1, type};
				if (t[0] == kOpTypeInt && t.size() >= 3 && t[1] == 32) return {true, t[2] ? 1u : 2u, 1, type};
				if (t[0] == kOpTypeFloat && t.size() >= 2 && t[1] == 32) return {true, 0, 1, type};
				if (t[0] == kOpTypeVector && t.size() >= 3 && t[2] <= 4) {
					ValueKind scalar = Kind(t[1]);
					if (scalar.loggable && scalar.components == 1) return {true, scalar.kind, t[2], t[1]};
				}
				return {};
			}

			// Logs `value` (of kind `kind`) as site `site` into the chosen invocation's next record.
			void Log(Emitter& e, u32 value, const ValueKind& kind, u32 site) {
				const u32 chosen = Fresh(), cursor = Fresh(), room = Fresh(), base = Fresh(), offset = Fresh(), address = Fresh(), at = Fresh();
				e.Emit(kOpLoad, {bool_, chosen, selected_});
				e.Emit(kOpLoad, {uint_, cursor, cursor_});
				e.Emit(kOpULessThan, {bool_, room, cursor, Uint(kTraceRecords)});
				const u32 writes = And(e, chosen, room);
				e.Emit(kOpLoad, {uint_, base, base_});
				e.Emit(kOpIMul, {uint_, offset, cursor, Uint(kTraceRecordWords)});
				e.Emit(kOpIAdd, {uint_, address, base, offset});
				e.Emit(kOpSelect, {uint_, at, writes, address, Uint(1)});
				for (u32 k = 0; k < kTraceRecordWords; ++k) {
					u32 word = Uint(0);
					if (k == 0) word = Uint(site + 1);
					else if (k - 1 < kind.components) {
						u32 component = value;
						if (kind.components > 1) {
							component = Fresh();
							e.Emit(kOpCompositeExtract, {kind.scalar, component, value, k - 1});
						}
						if (kind.kind == 3) {
							word = Fresh();
							e.Emit(kOpSelect, {uint_, word, component, Uint(1), Uint(0)});
						}
						else if (kind.kind == 2) word = component;
						else {
							word = Fresh();
							e.Emit(kOpBitcast, {uint_, word, component});
						}
					}
					u32 slot = at;
					if (k) {
						slot = Fresh();
						e.Emit(kOpIAdd, {uint_, slot, at, Uint(k)});
					}
					const u32 pointer = Fresh();
					e.Emit(kOpAccessChain, {uintPointer_, pointer, trace_, Uint(0), slot});
					e.Emit(kOpStore, {pointer, word});
				}
				const u32 next = Fresh(), advanced = Fresh();
				e.Emit(kOpIAdd, {uint_, next, cursor, Uint(1)});
				e.Emit(kOpSelect, {uint_, advanced, writes, next, cursor});
				e.Emit(kOpStore, {cursor_, advanced});
			}

			// A site for a value of `type` made by instruction `index` (the pointer stored to for a store); its kind's
			// loggable false when it does not log.
			ValueKind Site(size_t index, u32 id, u32 type, bool store, Instrumented& out, u32& site) {
				const ValueKind kind = Kind(type);
				if (!kind.loggable) return kind;
				site = (u32)out.sites.size();
				TraceSite s;
				s.offset = list_[index].at;
				s.id = id;
				s.type = kind.kind;
				s.components = kind.components;
				s.store = store;
				out.sites.push_back(s);
				return kind;
			}

			bool Rewrite(std::vector<u32>& bodies, Instrumented& out) {
				Emitter e;
				u32 function = 0;
				bool inBlock = false, firstBlock = false, chosen = false;
				std::vector<size_t> phis;
				const auto flushPhis = [&]() {
					for (size_t index : phis) {
						const u32* w = words_ + list_[index].at;
						u32 site = 0;
						const ValueKind kind = Site(index, w[2], w[1], false, out, site);
						if (kind.loggable) Log(e, w[2], kind, site);
					}
					phis.clear();
				};
				for (size_t i = firstFunction_; i < list_.size(); ++i) {
					const u32* w = words_ + list_[i].at;
					const u32 op = list_[i].op, length = list_[i].length;
					if (op == kOpFunction) {
						function = w[2];
						firstBlock = true;
						inBlock = false;
					}
					else if (op == kOpLabel) {
						if (inBlock) firstBlock = false;
						inBlock = true;
						phis.clear();
					}
					else if (op == kOpFunctionEnd) inBlock = false;
					const bool marker = op == kOpLine || op == kOpNoLine || op == kOpLabel || op == kOpFunction || op == kOpFunctionEnd;
					if (inBlock && !marker && op != kOpPhi && !(firstBlock && op == kOpVariable)) {
						flushPhis();
						if (function == entryFunction_ && firstBlock && !chosen) {
							Choose(e);
							chosen = true;
						}
					}
					e.words.insert(e.words.end(), w, w + length);
					if (!inBlock || marker) continue;
					if (op == kOpPhi) {
						phis.push_back(i);
						continue;
					}
					if (op == kOpVariable) continue;
					u32 site = 0;
					if (op == kOpStore && length >= 3) {
						auto type = results_.find(w[2]);
						const ValueKind kind = type == results_.end() ? ValueKind{} : Site(i, w[1], type->second, true, out, site);
						if (kind.loggable) Log(e, w[2], kind, site);
						continue;
					}
					bool typed = false;
					if (!SpirvResult(op, typed) || !typed || length < 3) continue;
					const ValueKind kind = Site(i, w[2], w[1], false, out, site);
					if (kind.loggable) Log(e, w[2], kind, site);
				}
				if (!chosen) {
					out.problem = "the entry point has no code to instrument";
					return false;
				}
				bodies = std::move(e.words);
				return true;
			}

			const u32* words_;
			size_t count_;
			TraceTarget target_;
			u32 bound_;
			u32 storage_ = kStorageBuffer;
			std::vector<Instruction> list_;
			std::map<u32, std::vector<u32>> types_;
			std::map<u32, u32> results_;      // result id -> its type
			std::map<u32, u32> builtins_;     // built-in -> its variable
			std::map<u32, u32> pointers_;     // global variable -> its pointer type
			std::map<u32, u32> constants_;    // uint value -> its constant
			std::vector<u32> listed_;         // the entry point's interface
			std::vector<u32> interface_;      // what it lists more
			size_t entryIndex_ = SIZE_MAX, firstDeclaration_ = SIZE_MAX, firstFunction_ = SIZE_MAX;
			u32 entryFunction_ = 0;
			Emitter notes_, globals_;
			u32 uint_ = 0, bool_ = 0, uintPointer_ = 0, trace_ = 0, selected_ = 0, base_ = 0, cursor_ = 0;
			u32 coords_ = 0, vertexIndex_ = 0, instanceIndex_ = 0;
		};
	}

	bool InstrumentSpirv(const u32* words, size_t count, const std::string& entry, const TraceTarget& target, Instrumented& out) {
		out = Instrumented();
		Rewriter rewriter(words, count, target);
		return rewriter.Run(entry, out);
	}
}
