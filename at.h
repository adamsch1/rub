#ifndef __AT__H
#define __AT__H

#include <stdlib.h>
#include <stdarg.h>

/** 
 * Growable array.  Intentionally minimialist.  
 *
 * struct at arr = {0};
 *
 * // Create an array to hold 3 ints to begin with
 * anew( &arr, sizeof(int), 3 );
 * 
 * for( int k=0; k<5; k++ ) { 
 *   aadd( &arr, &k );
 * }
 *
 * int *n = aget( &arr, 3 ); // *n == 4
 * 
 */
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
