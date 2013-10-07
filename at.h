#ifndef __AT__H
#define __AT__H

#include <stdlib.h>
#include <stdarg.h>

struct at {
  char *s;
  int len;
  int alen;
  size_t size;
};

int anew( struct at *buf, size_t size, int capacity );
void afree( struct at *buf );
void aadd( struct at *buf, void *thing );
void * aget( struct at *buf, int pos ) ;

#endif
