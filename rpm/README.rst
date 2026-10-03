Oracle RPM packages
===================

Copyright 2026 Qore Technologies, s.r.o.

The multi-distribution ``qore-oracle-module.spec`` targets Fedora 44,
AlmaLinux 10 and openSUSE Leap 16 with the Qore 3.0 SDK. It builds the native
Oracle driver, source and AOT forms of OracleExtensions, compiler metadata,
separate debug information and a documentation package. AOT metadata survives
the distribution's stripping and debug-package processing; Qore sources are
included in the debug-source package.

The AOT package omits LLVM's optional precomputed name index, which distribution
GDB ignores. Full DWARF symbols, source and Qore metadata are retained; initial
debugger loading may be slower.

Oracle client dependency
------------------------

Install Oracle's unmodified, signed Instant Client 23 Basic and development
RPMs before building. The tested version is ``23.26.3.0.0-1.el9``; Oracle's
OL9 Instant Client 23 repository supplies the packages and signing key:

* https://yum.oracle.com/repo/OracleLinux/OL9/oracle/instantclient23/x86_64/
* https://yum.oracle.com/faq.html#a10

Verify the signing key against Oracle's published fingerprint and check each
RPM with ``rpm -Kv`` after importing that key. Install the RPMs with the
distribution package manager so their dependencies are resolved. For example,
on Fedora or AlmaLinux, from a directory containing the verified RPMs::

    sudo dnf install ./oracle-instantclient-basic-23.26.3.0.0-1.el9.x86_64.rpm \
        ./oracle-instantclient-devel-23.26.3.0.0-1.el9.x86_64.rpm

On openSUSE, use ``sudo zypper install`` with the same two filenames. The
runtime package needs Basic; development headers are needed only for building.
The spec uses Oracle's installed ``/usr/include/oracle/23/client64`` and
``/usr/lib/oracle/23/client64/lib`` paths. It does not bundle or repackage the
proprietary client, and no database server is installed.

OBS explicitly lists ``oracle-instantclient`` among packages not approved for
normal hosting. An existing home-project package does not confirm an exception.
Client uploads require a confirmed hosting exception or a separate permitted
build and hosting route. See:
https://en.opensuse.org/openSUSE:Build_Service_application_blacklist

Build and test
--------------

Use a clean build root with the Qore SDK and the spec's build dependencies.
Prepare the source archive with the same exclusions recorded in
``debian/copyright``: ``packaging/instantclient``, ``m4/acx_pthread.m4`` and
``cmake/FindOracle.cmake``. The last two are unused legacy build helpers with
missing license information. The supported CMake build uses
``cmake/FindOracleClient.cmake``. The embedded, modified OCILIB implementation
is LGPL-2.1-or-later; OracleExtensions is MIT. Both license texts and the
component copyright notices are packaged.

Place the versioned source archive in RPM's SOURCES directory and run::

    rpmbuild -ba qore-oracle-module.spec

Tests and documentation are enabled by default. The tests load the exact
native module and each source/AOT extension, outside the source tree, with
Qore debugging enabled. Artifact selection fails if an output is missing or
ambiguous. They exercise driver registration, parameter validation and value
binding without opening a database connection, plus the native UTF-8 helper
regression. Documentation generation fails on warnings.

For installed packages, from the matching source archive::

    python3 -B -W error rpm/test_fixture.py -v
    python3 -B -W error rpm/run-tests.py --installed
    python3 -B -W error rpm/run-tests.py --installed --compiler

The final command additionally needs the Qore SDK and compiles and runs a
consumer. These offline checks do not qualify Oracle server connectivity,
transactions, Advanced Queuing or server-version compatibility. Run the
database integration suites in ``test/`` against the intended Oracle server
before a production rollout.

Packaging changes in 3.4.1-2
---------------------------

Replaces the legacy spec with distribution compiler flags, isolated offline
tests, complete AOT and compiler artifacts, strict reference documentation,
examples and separate debug packages. Proprietary client binaries remain an
external dependency.
