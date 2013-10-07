#include <stdlib.h>
#include <stdio.h>

#include "rub.h"

int k=0;
int main( int argc, char **argv ) {
  struct client_t *client = rub_get_request();
  int j=0;

  ++k;

  bappend_printf( &client->outs, "<html>Count: %d<br>", k);  

  for( j=0; j<client->get_fields.len; j++ ) {
    struct kv_t *kv;
    kv = aget( &client->get_fields, j ); 
    bappend_printf( &client->outs, "%s %s <br>", kv->key, kv->value );
  }
    
  bappend_printf( &client->outs, "</html>");
  return 200;
}
