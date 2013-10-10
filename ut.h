#ifndef __UT__H
#define __UT__H

/** 
 * URL encoder and decoder - intenionally minimialistic, no external 
 * dependencies
 * 
 * char *p = "/test.c?dude=b%20ob&fred=guy";
 * char *outs = url_decode(p); // "/test.c?dude=b ob&fred=guy"
 * free(outs);
 */
char *url_encode(char *str);
char *url_decode(char *str);

#endif
