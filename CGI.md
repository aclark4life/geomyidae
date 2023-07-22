# Introduction to CGI

Geomyidae has  support for running  scripts on each request,  which will
generate dynamic content.

There are three modes: standard cgi, dynamic cgi and http compatibility
mode. (»CGI« as name was just taken, because that's easier to compare
to the web.)


## Permissions

The scripts are run using the permissions of geomyidae. It is advised to
use the -g and -u options of geomyidae.


## Beforehand

In these examples C: is what the client sends and S: what the server is
sending.

## Stdout/Stdin I/O of the Scripts

All scripts called below, in TLS or Non-TLS mode, will get full access of
the socket of the connection, with the socket bound to stdin and stdout.
Geomyidae does not check for any connection duration. This allows to
create long-lasting streaming services, like radio or TV stations.

## Calling Convention

Geomyidae will call the script like this:

	% $gopherroot/test.cgi $search $arguments $host $port $traversal
			$selector

When it is a plain request, the arguments will have these values:

	C: /test.cgi
	-> $search = ""
	-> $arguments = ""
	-> $host = server host
	-> $port = server port
	-> $traversal = ""
	-> $selector = "/test.cgi"

If the request is for a type 7 search element, then the entered string by
the user will be seen as following:

	C: /test.cgi	searchterm		(There is a TAB in-between)
	-> $search = »searchterm«
	-> $arguments = ""
	-> $host = server host
	-> $port = server port
	-> $traversal = ""
	-> $selector = "/test.cgi\tsearchterm"

When you are trying to give your script some calling arguments, the syntax
is:

	C: /test.cgi?hello
	-> $search = ""
	-> $arguments = "hello"
	-> $host = server host
	-> $port = server port
	-> $traversal = ""
	-> $selector = "/test.cgi?hello"

If both ways of input are combined, the variables are set as following:

	C: /test.cgi?hello=world	searchterm	(Beware! A Tab!)
	-> $search = "searchterm"
	-> $arguments = "hello=world"
	-> $host = server host
	-> $port = server port
	-> $traversal = ""
	-> $selector = "/test.cgi?hello=world\tsearchterm"

## REST Calling Convention

There is a special mode in geomyidae to imitate REST calling abilities.

When a user requests some non-existing path, geomyidae will start from
the base and go up the path directories, until it reaches the first not
existing directory.

	C: /base/some/dir/that/does/not/exist?some-arguments	searchterm
	-> base exists
	-> some exists
	-> dir does not exist
	-> search for index.cgi or index.dcgi in /base/some
	-> if found, call index.cgi or index.dcgi as follows:
		-> $search = "searchterm"
		-> $arguments = "some-arguments"
		-> $host = server host
		-> $port = server port
		-> $traversal = "/dir/that/does/not/exist"
		-> $selector = "/base/some/dir/that/does/not/exist?some-arguments\tsearchterm"

## Standard CGI

The file  extension "cgi" switches to  this mode, where the  output of
the script is not interpreted at all  by the server and the script needs
to send raw content.

	% cat test.cgi
	#!/bin/sh
	echo "Hello my friend."
	%

The client will receive:

	S: Hello my friend.


## Dynamic CGI

For using  dynamic CGI, the  file needs to  end in "dcgi",  which will
switch on  the interpretation of the  returned lines by the  server. The
interpreted for- mat is the same as in the .gph files.

	% cat test.dcgi
	#!/bin/sh
	echo "[1|Some link|/somewhere|server|port]"
	%

Here  geomyidae will  interpret the  .gph format  and return  the valid
gopher menu item.

	S: 1Some link	/somewhere	gopher.r-36.net	70

## HTTP Compatibility

In case someone sends some HTTP request to geomyidae and other cases,
geomyidae will do this:

	C: GET /some/dir HTTP/1.1
	-> /GET does exist and is executable
	-> call GET as follows:
		-> $search = ""
		-> $arguments = ""
		-> $host = server host
		-> $port = server port
		-> $traversal = ""
		-> $selector = "GET /some/dir HTTP/1.1\r\n"
		   (full raw request by the client.)

This allows to serve HTTP next go gopher and get TLS for free. Other
HTTP-like protocols can be used over gopher in simple scripts, like the
icecast upload protocol.

## Environment Variables

Please see the manpage geomyidae(8) for all variables and their content.
All states of the script execution environment and client request are
available.


Have fun!

