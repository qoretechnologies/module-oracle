# Copyright (C) 2026 Qore Technologies, s.r.o.
# SPDX-License-Identifier: MIT
%global source_date_epoch_from_changelog 1
%global use_source_date_epoch_as_buildtime 1
%if v"%{rpmversion}" >= v"4.20"
%global build_mtime_policy clamp_to_source_date_epoch
%else
%global clamp_mtime_to_source_date_epoch 1
%endif
%bcond_without tests
%bcond_without docs
%global _find_debuginfo_dwz_opts %{nil}
Name: qore-oracle-module
Version: 3.4.1
Release: 2%{?dist}
Summary: Oracle database driver and extensions for Qore
License: MIT AND LGPL-2.1-or-later
URL: https://github.com/qoretechnologies/module-oracle
Source0: https://api.opensuse.org/public/source/home:davidnichols:qore:testing/%{name}/%{name}-%{version}.tar.xz
ExclusiveArch: x86_64 aarch64
Provides: bundled(ocilib)
BuildRequires: cmake >= 3.5
BuildRequires: make
BuildRequires: gcc-c++
BuildRequires: binutils
BuildRequires: python3
BuildRequires: oracle-instantclient-devel >= 23.26.3.0.0
BuildRequires: qore-devel >= 3.0.0~
BuildRequires: qore-rpm-macros >= 3.0.0~
%if %{with docs}
BuildRequires: doxygen
%if 0%{?suse_version}
BuildRequires: util-linux
%else
BuildRequires: util-linux-core
%endif
%endif
%{?qore_enable_aot_post}

%description
Native Oracle database access with transactions, prepared statements, named
types, large objects, Advanced Queuing and bulk loading. Includes source and
compiled OracleExtensions modules and compiler metadata. The proprietary
Oracle Instant Client is a separate dependency; no database server is installed.

%if %{with docs}
%package doc
Summary: Oracle module reference documentation and examples
BuildArch: noarch
%description doc
API reference and database test examples for Qore's Oracle driver.
%endif

%prep
%autosetup
%build
%{?set_build_flags}
. %{_rpmconfigdir}/qore/module-env.sh
unset ORACLE_HOME ORACLE_INSTANT_CLIENT ORACLE_INCLUDES TNS_ADMIN
qore_set_source_prefix_maps "%{qore_debug_source_dir}"
cmake -S . -B build -G 'Unix Makefiles' \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE=-DNDEBUG \
  -DCMAKE_INSTALL_PREFIX=%{_prefix} -DCMAKE_POLICY_DEFAULT_CMP0177=NEW \
  -DCMAKE_SKIP_RPATH=ON -DCMAKE_IGNORE_PREFIX_PATH=/usr/local \
  -DORACLE_PATH_INCLUDES:PATH=/usr/include/oracle/23/client64 \
  -DORACLE_PATH_LIB:PATH=/usr/lib/oracle/23/client64/lib \
  -DQore_DIR=%{_libdir}/cmake/Qore -DQORE_EXECUTABLE=/usr/bin/qore \
  -DQORE_QPP_EXECUTABLE=/usr/bin/qpp -DQORE_QCC_EXECUTABLE=/usr/bin/qcc \
  -DQORE_BUILD_AOT_MODULES=ON -DQORE_AOT_LINK_SOURCE_MODULES=OFF \
  -DQORE_GENERATE_JAVA_BINDINGS=OFF \
  -DQORE_QM_METADATA_ENV:STRING="QORE_MODULE_DIR=$PWD/build:$PWD/build/qlib-qmod:$PWD/qlib:$qore_stdlib_paths;QORE_MODULE_DIR_ONLY=1;QORE_INCLUDE_DIR=;LD_LIBRARY_PATH=" \
  -DCMAKE_DISABLE_FIND_PACKAGE_Doxygen=%{!?with_docs:ON}%{?with_docs:OFF}
cmake --build build -- %{?_smp_mflags}
%if %{with docs}
printf '\nWARN_AS_ERROR = FAIL_ON_WARNINGS\n' >> build/Doxyfile
cmake --build build --target docs -- %{?_smp_mflags}
%endif
%install
DESTDIR=%{buildroot} cmake --install build
%qore_install_aot_sources qlib
chmod 755 %{buildroot}%{_libdir}/qore-modules/*.qmod
# Distribution GDB ignores LLVM's optional name index. Keep full DWARF and
# source while preserving Qore's appended metadata around the ELF edit.
python3 %{qore_rpm_helper} %{buildroot} objcopy --remove-section=.debug_names \
  %{buildroot}%{_libdir}/qore-modules/OracleExtensions.qmod
%if %{with docs}
install -d %{buildroot}%{_docdir}/%{name}-doc
cp -a build/docs/. %{buildroot}%{_docdir}/%{name}-doc/
install -d %{buildroot}%{_docdir}/%{name}-doc/examples
python3 - <<'PYTHON'
from pathlib import Path
import shutil
root = Path('%{buildroot}%{_docdir}/%{name}-doc/examples/test')
for source in sorted(Path('test').rglob('*')):
    if source.is_file() and source.suffix in ('.q', '.qtest', '.qclass', '.sql'):
        target = root / source.relative_to('test')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        target.chmod(0o644)
PYTHON
# Examples remain directly runnable with the packaged interpreter.
find %{buildroot}%{_docdir}/%{name}-doc/examples -type f \( -name '*.q' -o -name '*.qtest' \) \
  -exec sed -i '1s|^#!/usr/bin/env qore$|#!/usr/bin/qore|' {} +
hardlink -t -O %{buildroot}%{_docdir}/%{name}-doc
%endif
%check
%if %{with tests}
. %{_rpmconfigdir}/qore/module-env.sh
# Inspect the packaged output after RPM's strip/debug processing, so a missing
# preservation hook fails the build even when the unstripped module loads.
python3 -B -W error - <<'PYTHON'
import importlib.util
from pathlib import Path
import subprocess
spec = importlib.util.spec_from_file_location('aot', '%{qore_rpm_helper}')
aot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(aot)
binary = Path('%{buildroot}%{_libdir}/qore-modules/OracleExtensions.qmod')
assert b'QAMD' in aot.read_trailers(binary), 'AOT metadata was lost during RPM processing'
sections = subprocess.check_output(['readelf', '-SW', str(binary)], text=True)
assert '.gnu_debuglink' in sections, 'Separate AOT debug information is missing'
assert '.debug_names' not in sections and '.debug_info' not in sections
PYTHON
python3 -B -W error rpm/test_fixture.py -v
python3 -B -W error rpm/run-tests.py --build-dir "$PWD/build"
ORACLE_BUILD_DIR="$PWD/build" python3 -B -W error test/test_ocilib_utf8_length.py -v
%endif
%files
%license COPYING.MIT COPYING.LGPL debian/copyright
%doc README RELEASE-NOTES AUTHORS rpm/README.rst
%{_libdir}/qore-modules/oracle-api-*.qmod
%{_libdir}/qore-modules/OracleExtensions.qmod
%{_datadir}/qore-modules/OracleExtensions.qm
%dir %{_datadir}/qore/metadata/oracle
%{_datadir}/qore/metadata/oracle/*.meta.json
%if %{with docs}
%files doc
%license COPYING.MIT COPYING.LGPL debian/copyright
%doc %{_docdir}/%{name}-doc/
%endif
%changelog
* Sat Oct 03 2026 David Nichols <david@qore.org> - 3.4.1-2
- Build with the packaged Qore SDK and the separate Oracle Instant Client RPMs.
- Package source and AOT extensions, metadata, strict API docs and examples.
- Run isolated offline driver/source/AOT and native UTF-8 helper regressions.
