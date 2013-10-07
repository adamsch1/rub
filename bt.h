#ifndef __BT__H
#define __BT__H

#include <stdlib.h>
#include <stdarg.h>

struct bt {
  char *s;
  int len;
  int alen;
  int allocated: 1;
};

struct bt * bnew( struct bt *buf, char *str );
void bfree( struct bt *buf );
void bappend_vprintf( struct bt *buf, const char *format, va_list args );
void bappend_printf( struct bt *buf, const char *format, ... );

#endif
