// selftest.h - Startup checks of the sensitivity, cm/360 and FOV math.
// Compiled into every build, but only run automatically in Debug builds.
#pragma once

#include <string>

// Runs all checks. Returns true if everything passed; 'report' receives a
// human readable summary (one line per check).
bool RunSelfTest(std::string& report);
