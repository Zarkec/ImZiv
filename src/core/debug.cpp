#include <cstdio>
#include <cstdlib>

namespace hex::dbg {

    [[noreturn]] void assertionHandler(const char* file, int line, const char* function, const char* exprString) {
        std::fprintf(stderr, "ImGui assertion failed: %s:%d %s: %s\n",
                     file != nullptr ? file : "<unknown>",
                     line,
                     function != nullptr ? function : "<unknown>",
                     exprString != nullptr ? exprString : "<unknown>");
        std::abort();
    }

}
