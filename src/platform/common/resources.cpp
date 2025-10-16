#include "pixel/platform/resources.hpp"
#include <filesystem>
#include <iostream>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace pixel::platform {

std::string get_resource_path() {
#ifdef __APPLE__
  // macOS: Get Resources directory from app bundle
  CFBundleRef mainBundle = CFBundleGetMainBundle();
  if (!mainBundle) {
    std::cerr << "Warning: Not running from app bundle" << std::endl;
    // Fallback for running from Xcode or command line during development
    return "./";
  }

  CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
  if (!resourcesURL) {
    std::cerr << "Warning: Could not find Resources directory" << std::endl;
    return "./";
  }

  char path[PATH_MAX];
  if (!CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8 *)path,
                                        PATH_MAX)) {
    CFRelease(resourcesURL);
    std::cerr << "Warning: Could not get resource path" << std::endl;
    return "./";
  }

  CFRelease(resourcesURL);

  std::string result(path);
  if (!result.empty() && result.back() != '/') {
    result += '/';
  }

  std::cout << "macOS Bundle Resources: " << result << std::endl;
  return result;

#else
  // Windows/Linux: Use executable directory or current working directory
  // Try to find assets directory relative to executable

  // Option 1: Check if assets exists in current directory
  if (std::filesystem::exists("assets")) {
    std::cout << "Resources: ./assets/ (current directory)" << std::endl;
    return "./";
  }

  // Option 2: Check if assets exists one level up (common for build
  // directories)
  if (std::filesystem::exists("../assets")) {
    std::cout << "Resources: ../assets/ (parent directory)" << std::endl;
    return "../";
  }

  // Fallback to current directory
  std::cout << "Resources: ./ (fallback to current directory)" << std::endl;
  return "./";
#endif
}

std::string get_resource_file(const std::string &relative_path) {
  static std::string resource_base = get_resource_path();
  std::string full_path = resource_base + relative_path;

  // Verify file exists and print helpful error if not
  if (!std::filesystem::exists(full_path)) {
    std::cerr << "Warning: Resource not found: " << full_path << std::endl;
    std::cerr << "  Relative path: " << relative_path << std::endl;
    std::cerr << "  Resource base: " << resource_base << std::endl;
  }

  return full_path;
}

} // namespace pixel::platform
