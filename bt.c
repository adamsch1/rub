#include "bt.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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
static void bregrow( struct bt *buf, int len ) {
  if( buf->len + len >= buf->alen ) {
    buf->alen = nearest_power(1, buf->len + len + 1 );
    buf->s = realloc( buf->s, buf->alen );
  }
}

/**
 *  Allocate a new bt string - str is optional
 */
int bnew( struct bt *buf, char *str ) {

  if( str ) {
    buf->s = strdup(str);
    buf->len = buf->alen = strlen(str)+1;
  } else {
    buf->s = 0;
    buf->len = buf->alen = 0;
  }

  return 0;
}

/**
 *  Free bt string
 */
void bfree( struct bt *buf ) {
  free(buf->s);
  if( buf->allocated ) free(buf);
}

/**
 *  Append printf to bt string
 */
void bappend_printf( struct bt *buf, const char *format, ... ) {
  va_list args;
  va_start(args, format);
  bappend_vprintf( buf, format, args );
  va_end(args);
}

/**
 *  Append printf to bt string using va_list
 */
void bappend_vprintf( struct bt *buf, const char *format, va_list args ) {
  char *buffer=NULL;
  int len;

  len = vasprintf( &buffer, format, args );

  if( len >= 0 ) {
    bregrow( buf, len );
    memcpy( buf->s + buf->len, buffer, len + 1 );
    buf->len += len;
    free(buffer);
  }

}









