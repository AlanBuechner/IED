// vkassemble.cpp - SPIR-V assembled from a listing in DisassembleSpirv's form (vkspirv.h), for Edit and Replay (docs/22
// "Edit and Replay"): a line per instruction, `%name = OpXxx operands`, read with the disassembler's own operand
// patterns, so what it prints reads back. Constants read by their type (0.5 for a float), enumerants by name or number.
#include "vkspirv.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <unordered_map>

namespace fb::replay::vulkan
{
	namespace
	{
		constexpr u32 kMaxProblems = 20;
		constexpr u32 kOpTypeInt = 21, kOpTypeFloat = 22, kOpConstant = 43, kOpSpecConstant = 50, kBuiltInDecoration = 11;

		struct Token
		{
			std::string text;
			bool quoted = false;
		};

		// One line's tokens; a ';' outside a string starts a comment. Strings are taken as printed: up to the next quote.
		bool Tokenize(const std::string& line, std::vector<Token>& out, std::string& problem) {
			size_t at = 0;
			while (at < line.size()) {
				const char c = line[at];
				if (c == ' ' || c == '\t' || c == '\r') {
					++at;
					continue;
				}
				if (c == ';') break;
				Token token;
				if (c == '"') {
					const size_t close = line.find('"', at + 1);
					if (close == std::string::npos) {
						problem = "a string has no closing quote";
						return false;
					}
					token.text = line.substr(at + 1, close - at - 1);
					token.quoted = true;
					at = close + 1;
				}
				else
					while (at < line.size() && line[at] != ' ' && line[at] != '\t' && line[at] != '\r' && line[at] != ';') token.text += line[at++];
				out.push_back(token);
			}
			return true;
		}

		bool Integer(const std::string& text, bool allowNegative, u64& value) {
			if (text.empty()) return false;
			char* end = nullptr;
			const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
			if (text[0] == '-') {
				if (!allowNegative) return false;
				value = (u64)strtoll(text.c_str(), &end, 10);
			}
			else value = strtoull(text.c_str(), &end, hex ? 16 : 10);
			return end && *end == '\0';
		}

		void StringWords(const std::string& text, std::vector<u32>& out) {
			std::vector<u8> bytes(text.begin(), text.end());
			bytes.push_back(0);
			while (bytes.size() % 4) bytes.push_back(0);
			for (size_t i = 0; i < bytes.size(); i += 4) out.push_back((u32)bytes[i] | (u32)bytes[i + 1] << 8 | (u32)bytes[i + 2] << 16 | (u32)bytes[i + 3] << 24);
		}

		class Assembler
		{
		public:
			Assembler(const u32* original, size_t count) {
				if (!original || count < 5) return;
				original_ = true;
				version_ = original[1];
				generator_ = original[2];
				next_ = original[3];
				for (const auto& name : SpirvNames(original, count)) ids_[name.second] = name.first;
			}

			bool Run(const std::string& text, std::vector<u32>& words, std::string& log) {
				std::vector<std::vector<Token>> lines;
				size_t at = 0;
				for (u32 number = 1; at <= text.size(); ++number) {
					size_t end = text.find('\n', at);
					if (end == std::string::npos) end = text.size();
					const std::string line = text.substr(at, end - at);
					Header(line);
					std::vector<Token> tokens;
					std::string problem;
					if (!Tokenize(line, tokens, problem)) Problem(number, problem);
					lines.push_back(tokens);
					at = end + 1;
				}
				// Numbered ids keep their numbers: new names come after the highest.
				for (const auto& tokens : lines)
					for (const Token& t : tokens) {
						u64 n = 0;
						if (!t.quoted && t.text.size() > 1 && t.text[0] == '%' && Integer(t.text.substr(1), false, n) && n + 1 > next_) next_ = (u32)(n + 1);
					}
				std::vector<u32> body;
				for (size_t i = 0; i < lines.size(); ++i)
					if (!lines[i].empty()) Instruction((u32)i + 1, lines[i], body);
				if (!problems_.empty()) {
					log = problems_;
					return false;
				}
				words = {0x07230203, version_, generator_, std::max(next_, top_ + 1), 0};
				words.insert(words.end(), body.begin(), body.end());
				return true;
			}

		private:
			// "; SPIR-V 1.3, generator 0x00080001, bound 40": the version and generator, when there is no original.
			void Header(const std::string& line) {
				unsigned major = 0, minor = 0, generator = 0;
				if (sawHeader_ || sscanf(line.c_str(), "; SPIR-V %u.%u, generator 0x%x", &major, &minor, &generator) != 3) return;
				sawHeader_ = true;
				if (original_) return;
				version_ = major << 16 | minor << 8;
				generator_ = generator;
			}

			void Problem(u32 line, const std::string& text) {
				if (++problemCount_ > kMaxProblems) return;
				problems_ += Format("line %u: %s\n", line, text.c_str());
			}

			u32 Id(const std::string& name) {
				u64 n = 0;
				u32 id = 0;
				if (Integer(name, false, n)) id = (u32)n;
				else {
					auto found = ids_.find(name);
					if (found != ids_.end()) id = found->second;
					else id = ids_[name] = next_++;
				}
				top_ = std::max(top_, id);
				return id;
			}

			void Instruction(u32 line, const std::vector<Token>& tokens, std::vector<u32>& body) {
				size_t at = 0;
				u32 result = 0;
				bool hasResult = false;
				if (tokens.size() >= 2 && !tokens[0].quoted && tokens[0].text[0] == '%' && tokens[1].text == "=") {
					result = Id(tokens[0].text.substr(1));
					hasResult = true;
					at = 2;
				}
				if (at >= tokens.size() || tokens[at].quoted || tokens[at].text.compare(0, 2, "Op") != 0) {
					Problem(line, "an instruction starts with Op (\"%name = OpFAdd %float %a %b\")");
					return;
				}
				const std::string name = tokens[at++].text.substr(2);
				u32 op = 0;
				const char* pattern = nullptr;
				u64 number = 0;
				if (!SpirvOpNamed(name, op, pattern)) {
					if (!Integer(name, false, number) || number > 0xFFFF) {
						Problem(line, "no instruction Op" + name);
						return;
					}
					op = (u32)number;
					if (hasResult) {
						Problem(line, "Op" + name + " is written by number, so its operands are too: no %result = before it");
						return;
					}
				}
				std::vector<u32> operands;
				Operands(line, op, pattern ? pattern : "", tokens, at, result, hasResult, operands);
				if (operands.size() + 1 > 0xFFFF) Problem(line, "the instruction is too long");
				body.push_back((u32)(operands.size() + 1) << 16 | op);
				body.insert(body.end(), operands.begin(), operands.end());
				if (op == kOpTypeFloat && operands.size() >= 2) floats_[operands[0]] = operands[1];
				if (op == kOpTypeInt && operands.size() >= 3) ints_[operands[0]] = {operands[1], operands[2]};
			}

			bool Want(u32 line, const std::vector<Token>& tokens, size_t at, const char* what) {
				if (at < tokens.size()) return true;
				Problem(line, std::string("missing ") + what);
				return false;
			}

			bool IdToken(u32 line, const Token& token, u32& id) {
				if (token.quoted || token.text.size() < 2 || token.text[0] != '%') {
					Problem(line, "expected an id (%name), found " + token.text);
					return false;
				}
				id = Id(token.text.substr(1));
				return true;
			}

			// An operand of no known kind: an id, a string, or a number.
			void Loose(u32 line, const Token& token, std::vector<u32>& out) {
				u64 n = 0;
				if (token.quoted) StringWords(token.text, out);
				else if (token.text[0] == '%') out.push_back(Id(token.text.substr(1)));
				else if (Integer(token.text, true, n)) out.push_back((u32)n);
				else Problem(line, "cannot read " + token.text);
			}

			// A constant's literal by its type: a float, a 64-bit value in two words, an integer.
			void Constant(u32 line, u32 type, const Token& token, std::vector<u32>& out) {
				u64 bits = 0;
				const bool raw = token.text.size() > 2 && token.text[0] == '0' && (token.text[1] == 'x' || token.text[1] == 'X');
				auto isFloat = floats_.find(type);
				auto isInt = ints_.find(type);
				const u32 width = isFloat != floats_.end() ? isFloat->second : isInt != ints_.end() ? isInt->second.first : 32;
				if (isFloat != floats_.end() && !raw) {
					char* end = nullptr;
					const double value = strtod(token.text.c_str(), &end);
					if (!end || *end) {
						Problem(line, "not a number: " + token.text);
						return;
					}
					if (width == 64) memcpy(&bits, &value, 8);
					else {
						const float f = (float)value;
						u32 w = 0;
						memcpy(&w, &f, 4);
						bits = w;
					}
				}
				else if (!Integer(token.text, true, bits)) {
					Problem(line, "not an integer: " + token.text);
					return;
				}
				out.push_back((u32)bits);
				if (width == 64) out.push_back((u32)(bits >> 32));
			}

			void Operands(u32 line, u32 op, const char* pattern, const std::vector<Token>& tokens, size_t& at, u32 result, bool hasResult, std::vector<u32>& out) {
				u32 decoration = UINT32_MAX;
				for (const char* p = pattern; *p; ++p) {
					u32 id = 0, value = 0;
					u64 n = 0;
					switch (*p) {
						case ' ': continue;
						case 'R':
							if (!hasResult) {
								Problem(line, "this instruction makes a result: %name = before it");
								return;
							}
							out.push_back(result);
							continue;
						case 'T':
						case 'i':
							if (at >= tokens.size()) return;    // the disassembler stops at the instruction's end too
							if (!IdToken(line, tokens[at++], id)) return;
							out.push_back(id);
							continue;
						case '*':
							while (at < tokens.size())
								if (IdToken(line, tokens[at++], id)) out.push_back(id);
							continue;
						case 'l':
							if (at >= tokens.size()) return;
							if (!Integer(tokens[at].text, true, n)) Problem(line, "not an integer: " + tokens[at].text);
							++at;
							out.push_back((u32)n);
							continue;
						case '+':
							if ((op == kOpConstant || op == kOpSpecConstant) && at < tokens.size() && !out.empty()) Constant(line, out[0], tokens[at++], out);
							while (at < tokens.size()) {
								if (decoration == kBuiltInDecoration && SpirvEnumerant('b', tokens[at].text, value)) {
									out.push_back(value);
									++at;
								}
								else Loose(line, tokens[at++], out);
							}
							continue;
						case 's':
							if (!Want(line, tokens, at, "a string")) return;
							if (!tokens[at].quoted) Problem(line, "expected a \"string\", found " + tokens[at].text);
							StringWords(tokens[at++].text, out);
							continue;
						case 'X':
							if (!Want(line, tokens, at, "an extended instruction")) return;
							if (SpirvGlslInstruction(tokens[at].text, value) || (Integer(tokens[at].text, false, n) && (value = (u32)n, true))) out.push_back(value);
							else Problem(line, "no GLSL.std.450 instruction " + tokens[at].text);
							++at;
							while (at < tokens.size())
								if (IdToken(line, tokens[at++], id)) out.push_back(id);
							continue;
						case 'e': {
							const char kind = *++p;
							if (!Want(line, tokens, at, "an enumerant")) return;
							if (SpirvEnumerant(kind, tokens[at].text, value) || (Integer(tokens[at].text, false, n) && (value = (u32)n, true))) out.push_back(value);
							else Problem(line, "unknown " + tokens[at].text);
							if (kind == 'd') decoration = value;
							++at;
							continue;
						}
						default: continue;
					}
				}
				while (at < tokens.size()) Loose(line, tokens[at++], out);
			}

			std::unordered_map<std::string, u32> ids_;
			std::unordered_map<u32, u32> floats_;                     // float type -> width
			std::unordered_map<u32, std::pair<u32, u32>> ints_;       // int type -> width, signedness
			u32 version_ = 0x00010000, generator_ = 0, next_ = 1, top_ = 0;
			bool original_ = false, sawHeader_ = false;
			std::string problems_;
			u32 problemCount_ = 0;
		};
	}

	std::string TrivialFragmentShader(const std::vector<TargetOutput>& targets) {
		const char* scalars[] = {"float", "int", "uint"};
		std::string interface, decorations, types, variables, stores;
		std::set<std::string> declared;
		for (const TargetOutput& t : targets) {
			const std::string scalar = scalars[std::min<u32>(t.kind, 2)];
			const std::string type = t.components > 1 ? scalar + std::to_string(t.components) : scalar;
			if (t.components > 1 && declared.insert(type).second) types += Format("%%%s = OpTypeVector %%%s %u\n", type.c_str(), scalar.c_str(), t.components);
			if (declared.insert("ptr_" + type).second) {
				types += Format("%%ptr_%s = OpTypePointer Output %%%s\n", type.c_str(), type.c_str());
				types += Format("%%zero_%s = OpConstantNull %%%s\n", type.c_str(), type.c_str());
			}
			interface += Format(" %%target%u", t.reg);
			decorations += Format("OpDecorate %%target%u Location %u\n", t.reg, t.reg);
			variables += Format("%%target%u = OpVariable %%ptr_%s Output\n", t.reg, type.c_str());
			stores += Format("OpStore %%target%u %%zero_%s\n", t.reg, type.c_str());
		}
		return "; SPIR-V 1.0, generator 0x00000000, bound 1\n"
			"OpCapability Shader\n"
			"OpMemoryModel Logical GLSL450\n"
			"OpEntryPoint Fragment %main \"main\"" + interface + "\n"
			"OpExecutionMode %main OriginUpperLeft\n" + decorations +
			"%void = OpTypeVoid\n%fn = OpTypeFunction %void\n%float = OpTypeFloat 32\n%int = OpTypeInt 32 1\n%uint = OpTypeInt 32 0\n" + types + variables +
			"%main = OpFunction %void 0 %fn\n%entry = OpLabel\n" + stores + "OpReturn\nOpFunctionEnd\n";
	}

	bool AssembleSpirv(const std::string& text, const u32* original, size_t count, std::vector<u32>& words, std::string& log) {
		words.clear();
		log.clear();
		return Assembler(original, count).Run(text, words, log);
	}
}
