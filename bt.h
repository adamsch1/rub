#ifndef __BT__H
#define __BT__H

#include <stdlib.h>
#include <stdarg.h>

/**
 *  Growable string buffer - intentionally minimialist
 * 
 *  struct bt string = {0};
 * 
 *  // Printf concatentation
 *  bappend_printf( &string, "Dude: %d", 5 ); // "Dude: 5"
 * 
 *  // Copy up to N characters from src, always null terminate
 *  bappend_strncat( &string, "APPLE!", 5  ); // "Dude: 5APPLE"
 *
 *  bfree( &string );
 */
struct bt {
  char *s;
  int len;
  int alen;
  int allocated: 1;
};

int bnew( struct bt *buf, char *str );
void bappend_strncat( struct bt *buf, const char *src, int n );
void bfree( struct bt *buf );
void bappend_vprintf( struct bt *buf, const char *format, va_list args );
void bappend_printf( struct bt *buf, const char *format, ... );

#endif
