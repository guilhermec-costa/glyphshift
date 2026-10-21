#pragma once
#include <string>
#include <vector>

// A Windows command line from UTF-8 arguments, quoted so the Microsoft C
// runtime (CommandLineToArgvW, argv) parses back exactly the same arguments.
namespace glyphshift::platform::windows {
std::wstring command_line(const std::vector<std::string>& argv);
}  // namespace glyphshift::platform::windows
