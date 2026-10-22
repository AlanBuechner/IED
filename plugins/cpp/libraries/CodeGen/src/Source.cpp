#define IMPLEMENT_LOGGER
#include "Validate.h"

#include "StaticReflection/ReflectionData.h"
#include "CodeGen.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <queue>

namespace fs = std::filesystem;

static void ParseArgs(int argc, char** argv, std::string& generatedFolder, std::string& sourceFolder) {
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if (arg == "--generated-folder" && i + 1 < argc) {
			++i;
			generatedFolder = argv[i];
		}
		else if (arg == "--source-folder" && i + 1 < argc) {
			++i;
			sourceFolder = argv[i];
		}
	}
}

int main(int argc, char** argv) {

	std::string generatedFolder = "generated";
	std::string sourceFolder = "src";
	ParseArgs(argc, argv, generatedFolder, sourceFolder);

	std::string projectDir = fs::current_path().string();

	// remove trailing "\" from path
	if (projectDir[projectDir.size() - 1] == '\\')
		projectDir = projectDir.substr(0, projectDir.size() - 1);

	// get the name of the project from the folder name
	std::string projectName = std::filesystem::path(projectDir).filename().string();
	std::replace(projectName.begin(), projectName.end(), '-', '_');
	std::replace(projectName.begin(), projectName.end(), ' ', '_');

	// validate all reflection files are present
	std::cout << "Validating Reflection" << std::endl;
	Validate(sourceFolder, generatedFolder);

	// get reflection data from .reflected files
	std::cout << "building reflection data" << std::endl;
	Reflection::ReflectionData data;
	data.LoadFolder(generatedFolder);
	data.Collect();
	std::cout << "finished building reflection data" << std::endl;

	// write reflection code
	fs::path gen = fs::path(generatedFolder) / "generated.cpp";
	std::cout << "generating source" << std::endl;
	WriteCode(gen, projectName, data);
	std::cout << "finished generating source" << std::endl;

	return 0;
}
