#include <stdlib.h>
#include <stdio.h>

#include <event2/buffer.h>

#include "rub.h"

int main( int argc, char **argv ) {
  struct rub_t *rub = rub_get_request();

  evbuffer_add_printf( rub->evb, "Dude!");
  return 200;
}
