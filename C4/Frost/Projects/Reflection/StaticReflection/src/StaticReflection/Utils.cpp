#include "Utils.h"

// helper functions
bool isToken(char c, const Core::Vector<char> tokens) {
	for (uint32_t i = 0; i < tokens.Count(); i++) {
		if (c == tokens[i])
			return true;
	}
	return false;
}

std::queue<Core::String> Tokenize(const Core::String& str,
	const Core::Vector<char> seperatingTokens) {
	std::queue<Core::String> tokens;

	uint32_t lastTokenLocation = 0;
	for (uint32_t i = 0; i < str.size(); i++) {
		if (isToken(str[i], seperatingTokens)) {
			tokens.push(str.substr(lastTokenLocation, i - lastTokenLocation));
			tokens.push(std::string(1, str[i]));
			lastTokenLocation = i + 1;
		}
	}
	tokens.push(str.substr(lastTokenLocation, str.size() - lastTokenLocation));
	return tokens;
}
