Notes about the Patch to Expat
==============================

This is a patch made against Expat 1.95.8 by Tero Hasu and Kenneth Oksanen.
The purpose of the patch is to allow Expat to be compiled for Symbian
without warnings, using either MSVC or GCC. The patch is not intended to
affect support for other platforms or compilers.

Not all of the changes in the patch are necessary if one does not mind
having a large number of compiler and linker warnings. For a minimal
patch that still allows building for Symbian, probably only the
constness changes are necessary; without them, the ``petran`` tool
will complain about "initialised data". WINS builds would appear to be
possible without any changes at all.

Affected Files
--------------

``expat_external.h``
~~~~~~~~~~~~~~~~~~~~

Changed a ``#define`` declaration so that ``XML_USE_MSC_EXTENSIONS``
is not defined in Symbian builds; if present, the definition causes
linker warnings when compiling for WINS using MSVC. Something to keep
in mind here is that just because we are using MSVC does not
necessarily mean that we are compiling for Windows.

``xmlparse.c``
~~~~~~~~~~~~~~

Added pragma(s) to remove excessive warnings when using MSVC.

Added some ``const`` declarations to remove static globals. The
``features`` table is now a constant, also, and uses ``sizeof`` in the
declaration -- hopefully this is okay with all compilers.

Added some casts to remove warnings.

Initialized ``enum XML_Error`` result likewise to avoid a warning.

``xmlrole.c``
~~~~~~~~~~~~~

Added pragma(s) to remove excessive warnings when using MSVC.

Added an extra ``const`` declaration, for ``types``.

``xmltok.c``
~~~~~~~~~~~~

Added pragma(s) to remove excessive warnings when using MSVC.

Changed the ``.c`` includes to ``.inl`` includes, to account for the
renamed source files.

Added some ``const`` declarations to remove static globals.

``xmltok_impl.c`` / ``xmltok_impl.inl``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The file ``xmltok_impl.c`` was renamed as ``xmltok_impl.inl``, as the
Symbian build system (or MSVC, rather) appeared to be confused by
included ``*.c`` files.

Added a directive to tell Emacs the file type since the extension no
longer reveals it.

Added pragma(s) to remove excessive warnings when using MSVC.

``xmltok_ns.c`` / ``xmltok_ns.inl``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The file ``xmltok_ns.c`` was renamed as ``xmltok_ns.inl``, as the
Symbian build system (or MSVC, rather) appeared to be confused by
included ``*.c`` files.

Added a directive to tell Emacs the file type since the extension no
longer reveals it.

Added an extra ``const`` declaration, for ``NS``.
