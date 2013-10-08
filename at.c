#include "at.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/**
 *  Given base, keeps doublying until greater than num
 *  Heavily borrowed this stuff from GLIB
 */
static int nearest_power( int base, int num ) {
  int n = base;

  while( n < num ) {
    n <<= 1;
  }

  return n;
}

/**
 *  Rewgrows buffer to store up to len more bytes
 */
static void aregrow( struct at *buf, int len ) {
  if( buf->len + len >= buf->alen ) {
    buf->alen = nearest_power(1, buf->len + len + 1 );
    buf->s = realloc( buf->s, buf->alen*buf->size );
  }
}

int anew( struct at *buf, size_t size, int capacity ) {
  buf->size = size;
  buf->s = capacity > 0 ? malloc( capacity * size ) : 0;
  buf->alen = capacity;
  buf->len = 0;
}

void afree( struct at *buf ) {
  free(buf->s);
}

void aadd( struct at *buf, void * thing ) {
  
  aregrow( buf, 1 );
  memcpy( buf->s + (buf->len * buf->size), thing, buf->size );
  buf->len += 1;
}

void * aget( struct at *buf, int pos ) {
  if( pos < buf->len ) {
    return buf->s+(buf->size*pos);
  } else {
    return NULL;
  }
}
#if 0
int main() {
  struct at array={0};
  int k;
  anew( &array, sizeof(int), 10 );

  for( k=0; k<11; k++  ){ 
    aadd( &array, &k );
  }
  printf("L: %d\n", array.alen );

  for( k=0; k<11; k++ ) {
    printf("L: %d\n", *(int *)aget( &array, k ));
  }
}
#endif
