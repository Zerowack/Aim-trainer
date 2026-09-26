// selftest_main.cpp - console runner for the math self-test (no window).
// Prints one line per check and returns 0 only if every check passed.
#include <cstdio>
#include <string>

#include "selftest.h"

int main() {
    std::string report;
    const bool ok = RunSelfTest(report);
    std::fputs(report.c_str(), stdout);
    return ok ? 0 : 1;
}
