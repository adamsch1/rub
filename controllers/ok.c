#include <stdlib.h>
#include <stdio.h>

#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>

#include "rub.h"

int main( int argc, char **argv ) {
  struct rub_t *rub = rub_get_request();
  struct evkeyval *kv;

  for( kv = rub->post_data->tqh_first; 
       kv != NULL; 
       kv = kv->next.tqe_next ) {
    evbuffer_add_printf( rub->evb, "%s=%s<br>", kv->key, kv->value );
  }
  evbuffer_add_printf( rub->evb, "Dude!");
  return 200;
}
