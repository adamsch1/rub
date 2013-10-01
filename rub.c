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

#include <uv.h>
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

 
uv_loop_t *loop;
http_parser_settings settings;

void close_cb( uv_handle_t *handle );

/**
 * Handle reading in the data, which means we toss it in the http_parser
 * which dispatches to further callbacks, finally we release the buffer 
 * close anything on error
 */
void read_callback( uv_stream_t *stream, ssize_t read, const uv_buf_t *buf ) {
  struct client_t *client = stream->data;
  size_t parsed = http_parser_execute( &client->parser, &settings,
    buf->base, read == UV_EOF ? 0 : read );
  if( parsed != read ) {
    uv_close((uv_handle_t*)stream, close_cb);
  }

  // From alloc_buffer 
  free(buf->base);
}

/**
 *  Allocate read buffer for this connection
 */
void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf ) {
  buf->len = suggested_size;
  buf->base = malloc(suggested_size);
}

/**
 * Done with write
 */
void write_cb( uv_write_t *req, int status ) {
  uv_close((uv_handle_t*)req->data, close_cb);
}

/**
 *  Done with headers
 */
int on_headers_complete( http_parser *parser ) {
  struct client_t *client = parser->data;

  //bappend_printf( &client->outs, "<html>Dude: %s", "welcome!");
  //bappend_printf( &client->outs, "Dude: %s</html>", "welcome!");

  route_request( client, NULL );

  add_header( client, "\r\n\r\n");

  client->rheaders[ client->rheader_count ].base = client->outs.s; 
  client->rheaders[ client->rheader_count ].len = client->outs.len; 
  client->rheader_count++;

//  client->resbuf.base = client->outs.s;
//  client->resbuf.len = client->outs.len;

//  uv_write(&client->write_req, (uv_stream_t*)&client->tcp,
//           &client->resbuf, 1, write_cb );
  uv_write(&client->write_req, (uv_stream_t*)&client->tcp,
           client->rheaders, client->rheader_count, write_cb );

  return 1;
}

void add_header( struct client_t *client, const char *h ) {

  if( client->rheader_count < 18 ) {
    client->rheaders[ client->rheader_count ].base = strdup(h);
    client->rheaders[ client->rheader_count ].len = strlen(h);
    client->rheader_count++;
  } else {
    syslog( LOG_ERR, "Exceeded max header response");
  }
}

/**
 *  Parse header - build a linked list of key values
 */
int on_header_field( http_parser *parser, const char *at, size_t length ) {
  struct client_t *client = parser->data;

  struct kv_t *header = calloc(1, sizeof(struct kv_t));
  header->key = malloc(length+1);

  strncpy( header->key, at, length );
  *(header->key+length) = 0;
  header->next = client->headers;
  client->headers = header;

  return 0;
}

/**
 *  Parse header - build a linked list of key values
 */
int on_header_value( http_parser *parser, const char *at, size_t length ) {
  struct client_t *client = parser->data;
  struct kv_t *header = client->headers;

  header->value = malloc(length+1);
  strncpy( header->value, at, length );
  *(header->value+length) = 0;

  return 0;
}

/**
 *  Parse header - build a linked list of key values
 */
int on_url( http_parser *parser, const char *at, size_t length ) {
  struct client_t *client = parser->data;

  client->url = malloc( length + 1 );
  strncpy( client->url, at, length );
  *(client->url+length) = 0;

  return 0;
}

/**
 * Close connection
 */
void close_cb( uv_handle_t *handle ) {
  struct client_t *client = handle->data;
  struct kv_t *header = client->headers;
  int k;

  while( header ) {
    struct kv_t *temp = header->next;
    free(header->key);
    free(header->value);
    free(header);
    header = temp;
  }

  for( k=0; k<client->rheader_count; k++ ) {
    free( client->rheaders[k].base );
  }
 
  while( header ) {
    struct kv_t *temp = header->next;
    free(header->key);
    free(header->value);
    free(header);
    header = temp;
  } 



  free(client->resbuf.base);
  free(client->url);
  free(client);
}

/**
 * New connection stuff - 
 */
void on_new_connection(uv_stream_t *server, int status ) {
  if( status == -1 ) {
    return;
  }

  struct client_t *client = calloc(1, sizeof(struct client_t));
  if( client == NULL ) {
    syslog( LOG_ERR, "Could not allocate memory in on_new_connection");
    return;
  }

  uv_tcp_init(loop, &client->tcp);
  if (uv_accept(server, (uv_stream_t*) &client->tcp) == 0) {
    // Every structure we use for uv has a user provided field
    client->tcp.data = client;
    client->parser.data = client;
    client->write_req.data = client;
    // Initialize the http parser, note you don't set the parser cb here
    http_parser_init( &client->parser, HTTP_REQUEST ); 
   
    // Start reading
    uv_read_start((uv_stream_t*) &client->tcp, alloc_buffer, read_callback);
  }
  else {
    uv_close((uv_handle_t*) client, NULL);
  }
}


int main(int argc, char **argv)
{
  char   *config;
  uv_tcp_t server;

  loop = uv_default_loop();
  uv_tcp_init( loop, &server );

  settings.on_headers_complete = on_headers_complete;
  settings.on_header_field = on_header_field;
  settings.on_header_value = on_header_value;
  settings.on_url = on_url;

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

  // Setup libuv networking loop
  struct sockaddr_in bind_addr;
  int r =uv_ip4_addr(global_config->address, global_config->port, 
                     &bind_addr );
  if( r ) {
      syslog(LOG_ERR, "socket error %s\n", uv_err_name(r));
      return 1;
  }
  uv_tcp_bind(&server, (struct sockaddr *)&bind_addr);

  r = uv_listen((uv_stream_t*) &server, 128, on_new_connection);
  if (r) {
      syslog(LOG_ERR, "Listen error %s\n", uv_err_name(r));
      return 1;
  }

  syslog( LOG_INFO, "Rub is ready.");
  uv_run(loop, UV_RUN_DEFAULT);
  
/*
  port = config_get_int( "RPort" );
  doc_root = config_get_str( "RDocRoot" );
  address = config_get_str( "RAddress" );
  script_root = config_get_str( "RScriptRoot" );
  // Start setting up libevent for HTTP
  base = event_base_new();
  if (!base) {
    syslog( LOG_ERR, "Couldn't create an event_base: exiting\n");
    return 1;
  }

  http = evhttp_new(base);
  if (!http) {
    syslog( LOG_ERR, "Couldn't create evhttp: exiting\n");
    return 1;
  }

  //evhttp_set_gencb(http, send_document_cb, strdup(doc_root));
  evhttp_set_gencb(http, route_request_cb, NULL );

  handle = evhttp_bind_socket_with_handle(http, global_config->address, 
                                          global_config->port);
  if (!handle) {
    syslog( LOG_ERR, "Could not bind to port %d: exiting", 
            (int)global_config->port);
    return 1;
  }

  syslog( LOG_INFO, "Rub is ready.");
  event_base_dispatch(base);
*/

  return 0;
}