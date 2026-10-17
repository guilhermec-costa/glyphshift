#include <sys/prctl.h>

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--signal") {
    std::raise(SIGTERM);
    return 99;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--exit") return 37;
  if (argc == 2 && std::string_view(argv[1]) == "--profile-env") {
    for (const char* name : {"GLYPHSHIFT_PROFILE", "GLYPHSHIFT_PRESET"}) {
      const char* value = std::getenv(name);
      std::cout << name << '=' << (value ? value : "") << '\n';
    }
    return 0;
  }
  const char* preload = std::getenv("LD_PRELOAD");
  std::cout << "LD_PRELOAD=" << (preload ? preload : "") << '\n';
  for (const char* name :
       {"VK_LAYER_PATH", "VK_ADD_LAYER_PATH", "VK_INSTANCE_LAYERS"}) {
    const char* value = std::getenv(name);
    std::cout << name << "=" << (value ? value : "<unset>") << '\n';
  }
  std::cout << "NO_NEW_PRIVS=" << prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0)
            << '\n';
  for (int i = 0; i < argc; ++i) std::cout << "ARG=" << argv[i] << '\n';
}
