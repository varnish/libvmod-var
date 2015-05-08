============
vmod_example
============

----------------------
Varnish Example Module
----------------------

:Author: Tollef Fog Heen
:Date: 2015-05-08
:Version: 1.0
:Manual section: 3

SYNOPSIS
========

import var;

DESCRIPTION
===========

Association list in VCL. Can be used to mimick variables.

FUNCTIONS
=========

set_string
----------

Prototype
	set_string(STRING S, STRING T)
        set(STRING S, STRING T) - shorthand
Return value
	NONE
Description
	Sets the variable identified by S to the value T.
Example
	var.set_string("bar", "some random string");

get_string
----------

Prototype
	get_string(STRING S)
        get(STRING S) - shorthand
Return value
	STRING
Description
	Returns the string identified by the supplied string.
Example
	set resp.http.foo = var.get_string("bar");

Similar functions
-----------------

There are similar functions named:

* set_int(STRING, INT)
* get_int(STRING)
* set_real(STRING, REAL)
* get_real(STRING)
* set_duration(STRING, DURATION)
* get_duration(STRING)
* set_ip(STRING, IP)
* get_ip(STRING)

get and set are shorthand for get_string and set_string.

clear
-----

Prototype
	Function VOID clear()
Returns
	NONE
Description
	Clears out all the variables.
Example
	

HISTORY
=======

This manual page was written by Per Buer. It might contain
errors. Patches are welcome.

COPYRIGHT
=========

This document is licensed under the same license as the
libvmod-example project. See LICENSE for details.

* Copyright (c) 2012-2015 Varnish Software
