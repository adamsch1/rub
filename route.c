#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tcc/libtcc.h"

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>

#include "rub.h"

/**
 *  Holder of compilated script or "controller"
 *  I call it a controller so I don't sound like I'm 40 years
 *  old, which I am.
 */
struct controller_t {
  char *path; 
  TCCState *state;

  int (*prog_main)(int, char **);

  struct controller_t *next;
};

// Maintains the list of controllers
struct controller_t *controllers;

/**
 *  Load a controller and compile it, we map general system errors 
 *  to a HTTP friendly value set in ecode, or we set ecode to 404 if 
 *  the script is not found.  If we compile successfully we leave ecode
 *  alone
 */
struct controller_t * load_controller( char * fpath, int *ecode ) {

  // Allocate memory for this new controller script
  struct controller_t *cont = calloc(1, sizeof(struct controller_t));
  if( !cont ) {
    *ecode = HTTP_INTERNAL;
    goto err;
  }
 
  // Read in the file in its entirety 
  char * source = source_file( fpath );
  if( !source ) {
    *ecode = HTTP_NOTFOUND;
    goto err;
  }

  // Setup TCC to compile this mother
  cont->state = tcc_new();
  
  cont->path = strdup(fpath);

  // XXX This is annoying but necessary need to figure out a good way 
  tcc_set_lib_path( cont->state, "./tcc" );

  tcc_set_output_type( cont->state, TCC_OUTPUT_MEMORY );
  if( tcc_compile_string( cont->state, source ) == -1 ) {
    // File was found and it was not compilable
    *ecode = HTTP_INTERNAL;
  } else {
    // File was found and it compiled successfully

    tcc_relocate( cont->state, TCC_RELOCATE_AUTO );
    cont->prog_main = tcc_get_symbol(cont->state, "main");

    *ecode = HTTP_OK;
  }

done:
  if( source ) free(source);
  return cont;

err:
  if( source ) free(source);

  if( cont ) {
    if( cont->path ) free(cont->path);
    free(cont);
  }

  return NULL; 
}

int run_controller( struct evhttp_request *req, char * fpath  ){
  struct controller_t *iter = controllers;
  int err=HTTP_OK;

  while( iter ) {
    if( strcmp(iter->path, fpath) == 0 ) {
      break;
    }
  }

  if( !iter ) {
    iter = load_controller( fpath, &err );
    if( iter )  {
      // We loaded it, link it in our list of controllers
      iter->next = controllers;
      controllers = iter;
    }
  }

  // Finally run it if we have one
  if( iter ) {
    iter->prog_main(0,0);
  } else {
    // There was a system error, a compilation error or the script was not
    // found 
    return err;
  }

}

const char *script_root = NULL;

void route_request_cb( struct evhttp_request *req, void *arg ) {
  const struct evhttp_uri *decoded;
  const char *uri = evhttp_request_get_uri(req);

  char * final_path = NULL;
  struct evbuffer *evbuffer = NULL;

  // Setup where we are sourcing scripts from
  if( ! script_root ) {
    script_root = config_get_str( "RScriptRoot" );
  }

  // Parse HTTP request URI 
  decoded = evhttp_request_get_evhttp_uri(req);  
  if( ! decoded ) {
    // Bail if bogus
    evhttp_send_reply(req, HTTP_BADREQUEST, "OK", NULL );
    goto done;
  }

  // calculate final script path 
  const char *path = evhttp_uri_get_path(decoded);
  asprintf( &final_path, "%s%s", script_root, path[0] == '/' ? path+1 : path );

  int ecode = run_controller( req, final_path );

  evhttp_send_reply(req, ecode, "OK", NULL );

done:

  if( final_path ) free(final_path); 
   
}
