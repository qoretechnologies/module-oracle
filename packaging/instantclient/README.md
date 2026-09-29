# Oracle Instant Client dependency

This recipe packages Oracle Instant Client 23.26.3 Basic and SDK for local
Ubuntu 26.04 amd64 builds. Oracle binaries and SDK content remain unchanged.
The archive checksums and download URLs are pinned in `upstream.json`.

Oracle's supplied Free Distribution, Hosting, and Use Terms permit distribution
of unmodified programs subject to the included conditions. The Basic and SDK
licenses and all vendor notices are preserved. This is proprietary software,
not a source rebuild or a Debian-main candidate. **Do not upload to the testing
PPA until Canonical approves hosting this license.** See the
[Launchpad eligibility policy](https://ubuntu.com/docs/launchpad/user/reference/launchpad-and-community/legal/launchpad-policies/#personal-package-archive-eligibility).

Download both archives from the URLs in `upstream.json`, then run:

    python3 prepare-source.py --archives /path/to/downloads --output /path/to/new-work

The helper verifies both SHA-256 digests, writes an orig archive containing the
unaltered ZIP files, and copies this Debian recipe. Build with `dpkg-buildpackage
-S -us -uc`, extract the resulting `.dsc`, and run `dpkg-buildpackage -b -us -uc`
as an unprivileged user in an offline native environment with Build-Depends.
No download, license prompt or host installation occurs during the build.

The Basic and SDK packages use `/usr/lib/x86_64-linux-gnu/oracle/23` with an
ldconfig configuration file. The Basic package requires the separate
`qore-oracle-libaio-compat` package, which owns only the `libaio.so.1` symlink to
the distribution `libaio.so.1t64`; it conflicts with the old `libaio1` package
to avoid an unowned overwrite. Debian libaio 0.3.113-8 documents that this SONAME
change is ABI-compatible. Only amd64 is qualified by this recipe.

The vendor ELF and JAR files are not stripped, patched, or normalized; no debug
symbols are fabricated. The Qore Oracle module itself is built from source
with normal Debian hardening and detached debug symbols. Verify byte-for-byte
parity of every vendor member, installed OCI version without the source tree or
library environment overrides, reproducibility, and package lifecycle before
publishing. Maintain Oracle security updates independently of Qore releases.
