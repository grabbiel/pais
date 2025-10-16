#pragma once
#include <string>

namespace pixel::platform {

// Get the base path to resources
// On macOS: Returns path to .app/Contents/Resources/
// On other platforms: Returns current working directory or asset folder
std::string get_resource_path();

// Get full path to a specific resource file
// Example: get_resource_file("assets/textures/brick.png")
// Returns: "/path/to/App.app/Contents/Resources/assets/textures/brick.png"
std::string get_resource_file(const std::string &relative_path);

} // namespace pixel::platform
