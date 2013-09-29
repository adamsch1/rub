rub
===

Web development application for C

Rub is a web development application that combines scripting language ease of development to C.

It uses TinyC, a ligtening quick C compiler, which compiles C code dynamically.  Your scripts are just 
regular C programs that are compiled to memory and executed.  You get the performance benefit of C
with a development cycle more akin to PHP, edit your code, reload the browser.

The HTTP part of the application is based around libevent which is a great library and I have used it before
so I went with it.

The rub application is controlled by a config file that looks something like this:

    shane@slurp:~/rub$ more rub.conf
    // Word
    const int RPort = 8888;
    const char *RDocRoot = "/tmp";
    const char *RScriptRoot = "controllers/";

Yes, the config file is just C code that gets compiled dynimcally.  Internally the code then does something like this:

    int port = config_get_int( "RPort" );

Pretty crazy but it works like a charm.

    shane@slurp:~/rub$ more controllers/test.c
    #include <stdlib.h>
    #include <stdio.h>
    
    int main( int argc, char **argv ) {
        printf("Hello world\n\n");
        return 200;
    }

The above program does what you think it would do.  Rub compiles it once, caches that compilation and executes it.

Planned changes:
  - Get system to properly send data back to the browser [or whomever]
  - Config option to check if scripts should be recompiled when updated or not
  - Config option to dump compilation errors of script to HTML for ease of debugging
  - #pragma in tcc to add additional libraries and paths for compilation something like 
  - Utilize more than one core 
  - add feature in tcc to support reflection for C 
  - Once reflection is done implement Annotations for functions, variables, structs etc
  
For more info on TinyC check out -

http://bellard.org/tcc/tcc-doc.html#SEC22
