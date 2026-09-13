#include "app.h"

int main(int argc, char *argv[]) {
  int result = peranti_init(argc, argv);
  if (result != 0) {
    return result;
  }

  result = peranti_mainloop();

  peranti_shutdown();
  return result;
}
