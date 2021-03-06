/* -*- Mode: C; tab-width: 2; indent-tabs-mode: f; c-basic-offset: 2 -*- */
/*
  A trivial static http webserver using Libevent's evhttp.

  This is not the best code in the world, and it does some fairly stupid stuff
  that you would never want to do in a production webserver. Caveat hackor!

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

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
#include <event2/http_struct.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>

#include "tcc/libtcc.h"
#include "rub.h"

/**
 *  For Compilation of our config file 
 */
TCCState *config_state;
const struct config_t *global_config;
const struct table_entry *content_type_table;

/**
 *  Dump command line usage
 */
static void syntax(void)
{
  fprintf(stdout, "Syntax: rub /path/to/rub.conf\n");
}

/** 
 * read in contents of file specified at path 
 * you free memory  - how many times do I have to fucking write this
 * function?
 */
char * source_file( const char *fpath ) {
  int fd = 0;
  char * buffer = NULL;
  struct stat sb;

  if ((fd = open(fpath, O_RDONLY, 0)) < 0 || fstat(fd, &sb))  {
    goto err;
  }

  // One extra byte for \0 at end
  buffer = malloc( sb.st_size+1 );
  if( !buffer ) {
    goto err;
  }

  // Read in the file in one shot
  if( read( fd, buffer, sb.st_size ) < 0 ) {
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
  if( !p ) return -1;
  return *(int*)p;
}

/**
 *  Read an str (char *) config value
 */
const char * config_get_str( const char *name ) {
  void *p = tcc_get_symbol(config_state, name);
  if( !p ) return 0;
  return *(const char **)p;
}

/**
 * Get generic symbol as config value
 */
const void * config_get_obj( const char *name ) {
  void *p = tcc_get_symbol(config_state, name);
  if( !p ) return 0;
  return p;
}

/**
 * Weak attempt at log formatting using apache2 formatting not very efficient
 */
const char * log_format( const char *fmt, struct evhttp_request *req, 
                         int response_size ) {
  static char buffer[1024];
  char timebuff[1024];
  char *s = buffer;
  const char*p = fmt;
  struct tm *tmp;
  time_t t;
  int b=0;
  #define AVAIL (sizeof(buffer)-(s-buffer))

  memset(buffer,0,sizeof(buffer));
  buffer[0] = 0;

  while( p && *p && AVAIL > 0 ) {
    *s = 0;
    if( *p == '%' ) {
      p++;
      switch( *p ) {
        case 'l':
        case 'u':
          // Old crap that nobody uses
          snprintf( s, 1024-(s-buffer), "%s-", s );
          break;
        case 't':
          t = time(NULL);
          tmp = localtime(&t);
          strftime( timebuff, sizeof(timebuff), "[%d/%b/%Y:%T %z]", tmp );
          snprintf( s, AVAIL, "%s%s", s, timebuff ); 
          break;
        case 'h':
          snprintf( s, AVAIL, "%s%s", s,req->remote_host );
          break;
        case 'r':
          snprintf( s, AVAIL, "%s%d", s,response_size);
          break;
        case 's':
          snprintf( s, AVAIL, "%s%d", s,req->response_code );
          break;
        default:
          break;
      }
    } else {
      // Copy non-control characters in log we know we have at least 1 byte
      *s = *p;
    }
    p++;
    s += strlen(s);
  } 
  return buffer;
}


int main(int argc, char **argv)
{
  struct event_base *base;
  struct evhttp *http;
  struct evhttp_bound_socket *handle;
  char   *config;

  openlog("rub", LOG_PID|LOG_CONS, LOG_LOCAL0);
  
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
    syslog( LOG_ERR, "Could not read in: %s", argv[1]);
    return 1;
  }
  read_config(config);
  free(config);

  global_config = config_get_obj( "config" );
  content_type_table = config_get_obj("content_type_table");

/*
  port = config_get_int( "RPort" );
  doc_root = config_get_str( "RDocRoot" );
  address = config_get_str( "RAddress" );
  script_root = config_get_str( "RScriptRoot" );
*/
  // Start setting up libevent for HTTP
  base = event_base_new();
  if (!base) {
    syslog( LOG_ERR, "Couldn't create an event_base: exiting\n");
    return 1;
  }

  /* Create a new evhttp object to handle requests. */
  http = evhttp_new(base);
  if (!http) {
    syslog( LOG_ERR, "Couldn't create evhttp: exiting\n");
    return 1;
  }

  /* We want to accept arbitrary requests, so we need to set a "generic"
   * cb.  We can also add callbacks for specific paths. */
  //evhttp_set_gencb(http, send_document_cb, strdup(doc_root));
  evhttp_set_gencb(http, route_request_cb, NULL );

  /* Now we tell the evhttp what port to listen on */
  handle = evhttp_bind_socket_with_handle(http, global_config->address, 
                                          global_config->port);
  if (!handle) {
    syslog( LOG_ERR, "Could not bind to port %d: exiting", 
            (int)global_config->port);
    return 1;
  }

  syslog( LOG_INFO, "Rub is ready.");
  event_base_dispatch(base);

  return 0;
}
