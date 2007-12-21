Notes about the Symbian Port of ``pyexpat``
===========================================

This is a Symbian port of ``pyexpat`` by Kenneth Oksanen and Tero
Hasu. It has been tested to compile for Symbian without errors or
compiler warnings, using either MSVC or GCC.

When compiling for Symbian, all the required header and LIB files must
be available. Some of the code makes use of Symbian Platform APIs
introduced as recently as in Symbian 7.0s. There are also some
references to Python for Series 60 specific APIs, in addition to the
standard Python/C API.

The source code for this component was taken from the Python 2.2.3 source
tree, or more specifically from its ``Modules`` directory.

The motivation for the port to allow the Python XML APIs to be used on
Python for Series 60. The component been patched to compile for
Symbian, but not with intention to submit a patch to Python itself, as
Python 2.2 is probably no longer being maintained. As this is the
case, we have not attempted to maintain compatibility with other
platforms, and the patched code presumably will not build for
platforms other than Symbian, not unless further changes are made.

Affected Files
--------------

``dllentry.cpp``
~~~~~~~~~~~~~~~~

This file was added. It contains the DLL entry point only.

``expat_config.h``
~~~~~~~~~~~~~~~~~~

This file was added, as we cannot really use the Python ``configure``
script to produce a suitable configuration for Symbian.

``logging.cpp``
~~~~~~~~~~~~~~~

This file was added. It contains logging routines used for debugging.
We probably only want to use this during development, although logging
needs to be specifically enabled (by creating a directory) to actually
happen. The ``pyexpat`` code, as it presently stands, does not do any
logging.

``pyexpat.c`` / ``pyexpat.cpp``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The file ``pyexpat.c`` was renamed as ``pyexpat.cpp``. This was done
as the Symbian API is C++, and we face plenty of compilation errors if
we try to include Symbian headers from a C source file. As we switched
to C++, we also added some ``extern "C"`` declarations.

Added an ``#include "symbian_python_ext_util.h"`` statement, as the
port makes use of some of the Python for Series 60 specific utilities.

``Xmlparsetype`` is now a Python global variable rather than a native
one; i.e. we have::

  #define Xmlparsetype  ((PyTypeObject *) SPyGetGlobalString("Xmlparsetype"))

instead of::

  staticforward const PyTypeObject Xmlparsetype;

Other static writable data has also been dealt with, either as above,
or by such data as function parameters, or by including into
structures, or by simply using ``const`` where possible. In some
cases, as with a number of docstrings, static globals have been
removed altogether.

``template_buffer`` is now initialized statically, instead of using an
initializer function; this involved writing out all 256 values.

Some compiler warnings caused by unused function variables have been
fixed by commenting out the names of unused variables.

The initialization code has changed quite considerably, due to the
need to add objects into the global hash table and other Python for
Series 60 quirks.

Others
~~~~~~

Various Symbian-specific build-related files have also been added.
This should be okay since we are not intending to submit for inclusion
into Python proper.
