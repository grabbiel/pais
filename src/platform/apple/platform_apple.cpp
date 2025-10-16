// Add to: src/platform/apple/platform_apple.cpp (or create it)

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <iostream>
#include <string>

namespace pixel::platform {

// Get the path to the Resources directory inside the .app bundle
std::string get_resource_path() {
  CFBundleRef mainBundle = CFBundleGetMainBundle();
  if (!mainBundle) {
    std::cerr << "Warning: Not running from app bundle, using working directory"
              << std::endl;
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

  std::cout << "Resource path: " << result << std::endl;
  return result;
}

// Helper to get full path to a resource file
std::string get_resource_file(const std::string &relative_path) {
  static std::string resource_base = get_resource_path();
  return resource_base + relative_path;
}

} // namespace pixel::platform

#endif // __APPLE__
