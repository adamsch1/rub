/* -*- Mode: C; tab-width: 2; indent-tabs-mode: f; c-basic-offset: 2 -*- */
/*
  A trivial static http webserver using Libevent's evhttp.

  This is not the best code in the world, and it does some fairly stupid stuff
  that you would never want to do in a production webserver. Caveat hackor!

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>

#include "tcc/libtcc.h"
#include "rub.h"

/**
 *  For Compilation of our config file 
 */
TCCState *config_state;

/**
 *  Dump command line usage
 */
static void syntax(void)
{
  fprintf(stdout, "Syntax: rub /path/to/rub.conf\n");
}

/** 
 * read in contents of file specified at path 
 * you free memory 
 */
char * source_file( const char *fpath ) {
  int fd = 0;
  char * buffer = NULL;
  struct stat sb;

  if ((fd = open(fpath, O_RDONLY, 0)) < 0 || fstat(fd, &sb))
   err(EXIT_FAILURE, "source_file: %s", fpath);

  // One extra byte for \0 at end
  buffer = malloc( sb.st_size+1 );
  if( !buffer ) {
    err(EXIT_FAILURE, "source_file: malloc %s", fpath);
    goto err;
  }

  // Read in the file in one shot
  if( read( fd, buffer, sb.st_size ) < 0 ) {
    err(EXIT_FAILURE, "source_file: malloc %s", fpath);
    goto err;
  }

  // Null terminate this string
  buffer[sb.st_size] = 0;
  return buffer;

  goto done;

err:

  if( buffer ) free(buffer);
  buffer = 0;

done:
  
  if( fd ) close(fd);

  return buffer;
}


/**
 *  Extract config variables for use in the program
 */
static int read_config( char * config)  {
  TCCState *s = NULL;
  s = tcc_new();

  if( !s ) {
    err(EXIT_FAILURE, "read_config: could not create tcc state");
    goto err;
  }

  tcc_set_lib_path( s, "./tcc" );

  tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

  if( tcc_compile_string(s, config) == -1 ) {
    err(EXIT_FAILURE, "read_config: Could not compile config file");
    goto err;
  } 

  if( tcc_relocate(s, TCC_RELOCATE_AUTO) < 0 ) {
    err(EXIT_FAILURE, "read_config: COuld not relocate code");
    goto err;
  }

  config_state = s;

  goto done;

err:
  if( s ) tcc_delete( s );

  return -1;

done:

  return 0;
}

/**
 *  Read an int config value
 */
int config_get_int( const char *name ) {
  void *p = tcc_get_symbol(config_state, name);
  return *(int*)p;
}

/**
 *  Read an str (char *) config value
 */
const char * config_get_str( const char *name ) {
  void *p = tcc_get_symbol(config_state, name);
  return *(const char **)p;
}

int main(int argc, char **argv)
{
  struct event_base *base;
  struct evhttp *http;
  struct evhttp_bound_socket *handle;
  char   *config;
  const char *doc_root;

  unsigned short port = 0;

  // Ignore sigpipe
  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    return (1);

  // Bitch comamndline args
  if (argc < 2) {
    syntax();
    return 1;
  }

  // Read in config file
  config = source_file(argv[1]);
  if( config == NULL ) {
    return 1;
  }
  read_config(config);
  free(config);

  port = config_get_int( "RPort" );
  doc_root = config_get_str( "RDocRoot" );

  // Start setting up libevent for HTTP
  base = event_base_new();
  if (!base) {
    fprintf(stderr, "Couldn't create an event_base: exiting\n");
    return 1;
  }

  /* Create a new evhttp object to handle requests. */
  http = evhttp_new(base);
  if (!http) {
    fprintf(stderr, "couldn't create evhttp. Exiting.\n");
    return 1;
  }

  /* We want to accept arbitrary requests, so we need to set a "generic"
   * cb.  We can also add callbacks for specific paths. */
  //evhttp_set_gencb(http, send_document_cb, strdup(doc_root));
  evhttp_set_gencb(http, route_request_cb, strdup(doc_root));

  /* Now we tell the evhttp what port to listen on */
  handle = evhttp_bind_socket_with_handle(http, "0.0.0.0", port);
  if (!handle) {
    fprintf(stderr, "couldn't bind to port %d. Exiting.\n",
      (int)port);
    return 1;
  }

  event_base_dispatch(base);

  return 0;
}
