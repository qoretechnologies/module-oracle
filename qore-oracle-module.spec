%define mod_ver 3.4.0

%{?_datarootdir: %global mydatarootdir %_datarootdir}
%{!?_datarootdir: %global mydatarootdir /usr/share}

%define module_api %(qore --latest-module-api 2>/dev/null)
%define module_dir %{_libdir}/qore-modules
%global user_module_dir %{mydatarootdir}/qore-modules/

%if 0%{?sles_version}

%define dist .sles%{?sles_version}

%else
%if 0%{?suse_version}

# get *suse release major version
%define os_maj %(echo %suse_version|rev|cut -b3-|rev)
# get *suse release minor version without trailing zeros
%define os_min %(echo %suse_version|rev|cut -b-2|rev|sed s/0*$//)

%if %suse_version
%define dist .opensuse%{os_maj}_%{os_min}
%endif

%endif
%endif

# see if we can determine the distribution type
%if 0%{!?dist:1}
%define rh_dist %(if [ -f /etc/redhat-release ];then cat /etc/redhat-release|sed "s/[^0-9.]*//"|cut -f1 -d.;fi)
%if 0%{?rh_dist}
%define dist .rhel%{rh_dist}
%else
%define dist .unknown
%endif
%endif

Summary: Oracle DBI module for Qore
Name: qore-oracle-module
Version: %{mod_ver}
Release: 1%{dist}
License: MIT
Group: Development/Languages/Other
URL: http://www.qoretechnologies.com/qore
Source: http://prdownloads.sourceforge.net/qore/%{name}-%{version}.tar.bz2
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
BuildRequires: cmake >= 3.5
BuildRequires: gcc-c++
BuildRequires: qore-devel >= 1.12.4
BuildRequires: qore-stdlib >= 1.12.4
BuildRequires: qore >= 1.12.4
BuildRequires: doxygen
BuildRequires: oracle-instantclient
BuildRequires: oracle-instantclient-devel
Requires: /usr/bin/env
Requires: qore-module(abi)%{?_isa} = %{module_api}

%description
Oracle DBI driver module for the Qore Programming Language. The Oracle driver is
character set aware, supports multithreading, transaction management, stored
procedure and function execution, etc.

%if 0%{?suse_version}
%debug_package
%endif

%prep
%setup -q

%build
%if 0%{?el7}
# enable devtoolset7
. /opt/rh/devtoolset-7/enable
unset msgpackPATH
%endif
export CXXFLAGS="%{?optflags}"
cmake -DCMAKE_INSTALL_PREFIX=%{_prefix} -DCMAKE_BUILD_TYPE=RELWITHDEBINFO -DCMAKE_SKIP_RPATH=1 -DCMAKE_SKIP_INSTALL_RPATH=1 -DCMAKE_SKIP_BUILD_RPATH=1 -DCMAKE_PREFIX_PATH=${_prefix}/lib64/cmake/Qore -DORACLE_PATH_INCLUDES=/usr/include/oracle/21.1.0.0.0/client .
make %{?_smp_mflags}
make %{?_smp_mflags} docs
sed -i 's/#!\/usr\/bin\/env qore/#!\/usr\/bin\/qore/' test/*.q*

%install
make DESTDIR=%{buildroot} install %{?_smp_mflags}

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%{module_dir}
%{user_module_dir}
%doc COPYING.MIT COPYING.LGPL README RELEASE-NOTES AUTHORS

%package doc
Summary: oracle module for Qore
Group: Development/Languages/Other

%description doc
Oracle module for the Qore Programming Language.

This RPM provides API documentation, test and example programs

%files doc
%defattr(-,root,root,-)
%doc docs/oracle test/*.q*

%changelog
* Sat Aug 8 2026 David Nichols <david@qore.org> - 3.4.0
- added native OCI direct path bulk loading

* Wed Jun 19 2024 David Nichols <david@qore.org> - 3.3.3
- updated to version 3.3.3

* Mon Dec 19 2022 David Nichols <david@qore.org> - 3.3.2
- updated to version 3.3.2
- updated spec file to use cmake

* Sat Jan 15 2022 David Nichols <david@qore.org> - 3.3.1
- updated to version 3.3.1

* Sun Jun 11 2017 David Nichols <david@qore.org> - 3.3
- updated to version 3.3

* Tue Sep 13 2016 David Nichols <david@qore.org> - 3.2.1
- updated to version 3.2.1

* Sun Jun 5 2016 David Nichols <david@qore.org> - 3.2
- added test scripts
- updated to version 3.2

* Fri Aug 2 2013 David Nichols <david@qore.org> - 3.1
- updated to version 3.1

* Mon Mar 18 2013 David Nichols <david@qore.org> - 3.0
- updated to version 3.0

* Sun Nov 11 2012 David Nichols <david@qore.org> - 2.3
- updated to version 2.3

* Tue Oct 30 2012 David Nichols <david@qore.org> - 2.2.1
- updated to version 2.2.1

* Fri Jun 8 2012 David Nichols <david@qore.org> - 2.2
- updated to version 2.2

* Fri Jan 21 2011 David Nichols <david@qore.org> - 2.1
- updated to version 2.1

* Tue Aug 3 2010 David Nichols <david@qore.org>
- updated to version 2.0

* Thu Jul 2 2010 David Nichols <david@qore.org>
- updated to version 1.3

* Thu Apr 15 2010 David Nichols <david_nichols@users.sourceforge.net>
- updated to version 1.2

* Mon Dec 7 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated to version 1.1

* Tue Aug 18 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated to version 1.0.9

* Thu Jun 18 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated to version 1.0.8

* Mon Apr 6 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated to version 1.0.7

* Tue Mar 24 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated to version 1.0.6

* Wed Feb 4 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated to version 1.0.5

* Wed Jan 7 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated to version 1.0.4

* Thu Dec 4 2008 David Nichols <david_nichols@users.sourceforge.net>
- updated to version 1.0.3

* Fri Nov 28 2008 David Nichols <david_nichols@users.sourceforge.net>
- updated to version 1.0.2

* Fri Oct 17 2008 David Nichols <david_nichols@users.sourceforge.net>
- updated to version 1.0.1

* Tue Sep 2 2008 David Nichols <david_nichols@users.sourceforge.net>
- initial spec file for separate oracle module release
