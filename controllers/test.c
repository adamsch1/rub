#include <stdlib.h>
#include <stdio.h>

#include "rub.h"

int k=0;
int main( int argc, char **argv ) {
  struct client_t *client = rub_get_request();

  
  bappend_printf( &client->outs, "<html>Count: %d</html>", ++k);  
  return 200;
}
