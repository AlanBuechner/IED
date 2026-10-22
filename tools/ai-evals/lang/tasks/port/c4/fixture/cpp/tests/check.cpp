#include "check.h"

#include <iostream>

namespace check {
namespace {

int failures = 0;

}  // namespace

std::vector<Case>& Registry() {
    static std::vector<Case> cases;
    return cases;
}

void Fail(const char* file, int line, const std::string& message) {
    ++failures;
    std::cout << "  FAIL " << file << ":" << line << ": " << message << "\n";
}

int RunAll() {
    for (const Case& test : Registry()) {
        int before = failures;
        test.run();
        std::cout << (failures == before ? "ok     " : "FAILED ") << test.name << "\n";
    }
    std::cout << Registry().size() << " tests, " << failures << " failed checks\n";
    return failures;
}

}  // namespace check

int main() { return check::RunAll() == 0 ? 0 : 1; }
