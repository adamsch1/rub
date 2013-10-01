#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "tcc/libtcc.h"

#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
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

  int   prog_size;
  char *program;

  int (*prog_main)(int, char **);

  struct controller_t *next;
};

// Maintains the list of controllers
struct controller_t *controllers;

// Anything we want passed to child script we place in this
struct client_t *gclient;

extern struct config_t *global_config;
extern const struct table_entry *content_type_table;

/**
 *  Symbols you want exported to controller set here
 */
struct symbol_map_t {
  char * name;
  void * symbol;
} symbol_map[] = {
  { "rub_get_request", rub_get_request },
  NULL
};

/**
 *  Add all symbols to controller in one shot
 */
void set_symbols( struct controller_t *cont ) {

  int k=0;
  
  while( symbol_map[k].name ) {
    tcc_add_symbol( cont->state, symbol_map[k].name,
      symbol_map[k].symbol );
    k++;
  }
}

/**
 *  Parse request data [FORM/Query]
 */
void parse_request_data( struct evbuffer *input, struct evkeyvalq *kv )  {
  int length = evbuffer_get_length(input);
  if( length ) {
    char *data = malloc(length);
    evbuffer_copyout( input, data, length );
    //syslog(LOG_INFO, "LENGTH=%d %s", length, data);
    evhttp_parse_query_str( data, kv );
    free(data);
  }
}

/**
 *  We chuck a bunch of libevent http stuff in a  struct
 */
struct client_t * rub_get_request()  {

  //rub.post_data = calloc(1, sizeof( struct evkeyvalq));
  //rub.query_data = calloc(1, sizeof( struct evkeyvalq));

  // Parse POST form data, if any  
  
  //parse_request_data( rub.req->input_buffer, rub.post_data );

  //return client;
}

/**
 * Free memory
 */
void rub_reset_request() {

#if 0
  if( rub.post_data ) {
    evhttp_clear_headers( rub.post_data );
    free(rub.post_data);
  }
  if( rub.query_data ) {
    evhttp_clear_headers( rub.query_data );
    free(rub.query_data);
  }
  if( rub.evb ) evbuffer_free( rub.evb );
#endif
}


void error_function( void *user, const char *msg ) {

  if( global_config->display_error  ) {
    bappend_printf( &gclient->outs, "<html><head><title>Rub Compilation error</title></head>");
    bappend_printf( &gclient->outs, "Error: %s", msg );
    bappend_printf( &gclient->outs, "</html>");
  }

  syslog(LOG_ERR, "Compilation error: %s", msg );
}

/**
 *  Free controller, it does not unlink from main list
 *  or other data structures
 */
void free_controller( struct controller_t *c ) {
  free(c->path);
  tcc_delete(c->state);
  free(c); 
}

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
  tcc_set_error_func( cont->state, source, error_function );

  if( tcc_compile_string( cont->state, source ) == -1 ) {
    // File was found and it was not compilable
    *ecode = HTTP_INTERNAL;
  } else {
    // File was found and it compiled successfully

    set_symbols( cont ); 

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

/**
 *  Iterate through controllers looking for one that handles the 
 *  current request.  If this is the first time a controller has run
 *  compile and cache the result.
 */
int run_controller( struct client_t *client, char * fpath  ){
  struct controller_t *iter = controllers;
  struct controller_t *prev = controllers;
  int err=HTTP_OK;

  while( iter ) {
    if( strcmp(iter->path, fpath) == 0 ) {
      break;
    } 
    // Keep link to previous node so we can delete the iter if necessary
    prev = iter;
    iter = iter->next;
  }

  // Nothing found - try loading and compiling
  if( !iter ) {
    iter = load_controller( fpath, &err );
    if( iter )  {
      // We loaded it, link it in our list of controllers
      iter->next = controllers;
      prev = controllers = iter;
    }
  }

  // Finally run it if we have one
  if( iter && iter->prog_main ) {
    iter->prog_main(0,0);
  } else {
    // We found it but could not compile it 
    if( iter && !iter->prog_main ) {
      // Compile error - cleanup so we can reload the script next time
      prev->next = iter->next;
      if( prev == iter ) {
        // If iter is the first [prev == iter] then reset head pointer
        controllers = prev->next; 
      } 
      free_controller( iter );
    }
    return err;
  }

}

/* Try to guess a good content-type for 'path' */
static const char *
guess_content_type(const char *path)
{
    const char *last_period, *extension;
    const struct table_entry *ent;

    last_period = strrchr(path, '.');
    if (!last_period || strchr(last_period, '/'))
        goto not_found; /* no exension */
    extension = last_period + 1;
    for (ent = content_type_table; ent->extension; ++ent) {
        if (!evutil_ascii_strcasecmp(ent->extension, extension))
            return ent->content_type;
    }

not_found:
    return "application/misc";
}

/**
 *  Send a normal file back - no attempt to compile or interpret
 */
int send_file( const char *path, struct client_t *rub ) {
  int ecode = HTTP_NOTFOUND;
  struct stat st;
  char *final_path = NULL;
  int fd;
 
  if( !global_config->doc_root ) {
    // Dont' serve files
    return ecode;
  }

  // Default to public document
  asprintf( &final_path, "%s%s", global_config->doc_root, 
            path[0] == '/' ? path+1 : path );
  if( strstr(path, "../" ))  {
    //evhttp_send_reply(rub->req, HTTP_BADREQUEST, "OK", NULL );
    goto done;
  }

  const char *type = guess_content_type( final_path );

  if( (fd=open(final_path, O_RDONLY)) < 0 ) {
    syslog(LOG_ERR, "Could not open: %s", final_path );
    goto err;
  }

  if( fstat( fd, &st) < 0 ) {
    syslog(LOG_ERR, "Could not fstat: %s", final_path );
    goto err;
  }

  char *fb = malloc( st.st_size +1 );
  read(fd, fb, st.st_size );
  rub->outs.s = fb;
  rub->outs.len = st.st_size;
  
/*
  evhttp_add_header( evhttp_request_get_output_headers(rub->req),
                     "Content-Type", type );
  evbuffer_add_file( rub->evb, fd, 0, st.st_size );
*/
  ecode = HTTP_OK;

  goto done;

err:

  if( fd > 0 ) close(fd);

done:

  if( final_path ) free(final_path);

  return ecode;
}

void client_send_reply( struct client_t *client, int code, 
                        const char *reason ) {
  client->http_code = code;
  char *k;
   
  asprintf( &k, "HTTP/1.1 %d %s\r\n", code, reason );
  add_header( client, k );
  free(k);

  asprintf( &k, "Content-Encoding: %s\r\n", "text/html");
  add_header( client, k );
  free(k);
}

const char * FMT = "%h %l %u %t %s %r";

void route_request( struct client_t *client, void *arg ) {
  char * final_path = NULL;

  // Calculate final script path  
  if( strstr(client->url, "../" ))  {
    //evhttp_send_reply(req, HTTP_BADREQUEST, "OK", NULL );
    goto done;
  }

  asprintf( &final_path, "%s%s", global_config->script_root, 
            *client->url == '/' ? client->url+1 : client->url );

  gclient = client;

  int ecode = run_controller( client, final_path );
  if( ecode == HTTP_NOTFOUND ) {
    ecode =send_file( client->url, client );
  }

  int response_size = client->outs.len;
  client_send_reply( client, ecode, "OK");
  //evhttp_send_reply(req, ecode, "OK", rub.evb );

  //syslog( LOG_INFO, "%s", log_format( FMT, req, response_size) );
done:

  if( final_path ) free(final_path); 

  rub_reset_request();
}
#if 0
void route_request_cb( struct evhttp_request *req, void *arg ) {
  const struct evhttp_uri *decoded;
  const char *uri = evhttp_request_get_uri(req);
  int response_size = 0;
  char * final_path = NULL;

  // Setup data structure we pass to controllers
  memset(&rub, 0, sizeof(rub));
  rub.req = req;
  rub.evb = evbuffer_new();

  // Parse HTTP request URI 
  decoded = evhttp_request_get_evhttp_uri(req);  
  if( ! decoded ) {
    // Bail if bogus
    evhttp_send_reply(req, HTTP_BADREQUEST, "OK", NULL );
    goto done;
  }

  // Calculate final script path  
  const char *path = evhttp_uri_get_path(decoded);
  if( strstr(path, "../" ))  {
    evhttp_send_reply(req, HTTP_BADREQUEST, "OK", NULL );
    goto done;
  }

  asprintf( &final_path, "%s%s", global_config->script_root, 
            path[0] == '/' ? path+1 : path );

  int ecode = run_controller( &rub, final_path );
  if( ecode == HTTP_NOTFOUND ) {
    ecode =send_file( path, &rub );
  }

  response_size = evbuffer_get_length( rub.evb );
  evhttp_send_reply(req, ecode, "OK", rub.evb );

  syslog( LOG_INFO, "%s", log_format( FMT, req, response_size) );
done:

  if( final_path ) free(final_path); 

  rub_reset_request();
   
}
#endif
