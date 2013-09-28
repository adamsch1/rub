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


Yes, the config file is just C code that gets compiled dynimcally.  Internally the code then does something like this:

int port = config_get_int( "RPort" );

Pretty crazy but it works like a charm.



For more info on TinyC check out -

http://bellard.org/tcc/tcc-doc.html#SEC22
