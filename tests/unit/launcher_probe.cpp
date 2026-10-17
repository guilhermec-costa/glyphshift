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
  const char* preload = std::getenv("LD_PRELOAD");
  std::cout << "LD_PRELOAD=" << (preload ? preload : "") << '\n';
  std::cout << "NO_NEW_PRIVS=" << prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0)
            << '\n';
  for (int i = 0; i < argc; ++i) std::cout << "ARG=" << argv[i] << '\n';
}
