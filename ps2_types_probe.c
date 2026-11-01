#include <sys/stat.h>
#include <sys/time.h>
int main(void) {
  mode_t mode = 0;
  dev_t device = 0;
  struct timeval tv = {0, 0};
  return (int)mode + (int)device + (int)tv.tv_sec;
}
