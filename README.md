
This is a game simulator for examining the behavior of online algorithms for solving
a stochastic constraint satisfaction problem. A more detailed description is available
in the static HTML, but in brief it is a game in which the player is presented with
a randomly generated symbol according to a partially known distribution and must choose
whether to accept or reject that symbol, with the goal being to accept a certain
number of symbols subject to constraints on the sequence of accepted symbols
while minimizing the number of rejected symbols.

To build you will need to have libuuid, libmicrohttpd, and maybe something I've missed
installed globally on your system. You also need to init the submodules and build
libvalkey locally, but it should not be installed globally--the project will reference the
output files exactly. You will also need libgjm access, which is currently not public
because it is both incomplete and has some rather embarassing and dangerous mistakes in
how it handles dynamic strings (not used in this project anyway though) that need to be
addressed before it is released.

With the deps installed it can be built by invoking make directly. If something is missing
you'll see an error message that should be pretty straightforward to unravel.

To run the server you will need to:

- Configure nginx in a manner similar to what is shown in nginx.conf to act as both
  a proxy for the game and to handle static files
- Start a valkey server using valkey.conf, plus any modifications you want to memory
  size, etc:
  $ path/to/valkey ./valkey.conf
- Finally just run the server:
  $ ./berghain-server

This will provide output to indicate that the server is ready:

$ ./berghain-server
initializing game
parameter set 0 ready
parameter set 1 ready
parameter set 2 ready
parameter set 3 ready
web server active, game is ready
