# Upstream: Ethan Galstad <naemon$naemon,org>
# Modified version from original dag spec

### FIXME: TODO: Add sysv script based on template. (remove cmd-file on start-up)
%define logmsg logger -t %{name}/rpm
%define logdir %{_localstatedir}/log/naemon

# Setup some debugging options in case we build with --with debug
%if %{defined _with_debug}
  %define mycflags -O0 -pg -ggdb3
%else
  %define mycflags %{nil}
%endif

# Allow newer compiler to suppress warnings
%if 0%{?el6}
  %define myXcflags -Wno-unused-result
%else
  %define myXcflags %{nil}
%endif

Summary: Open Source host, service and network monitoring program
Name: naemon
Version: 4.0.1
Release: 1%{?dist}
License: GPL
Group: Applications/System
URL: http://www.naemon.org/
Packager: Daniel Wittenberg <dwittenberg2008@gmail.com>
Vendor: Naemon Core Development Team
Source0: http://www.naemon.org/download/naemon/naemon-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
BuildRequires: gd-devel > 1.8
BuildRequires: zlib-devel
BuildRequires: libpng-devel
BuildRequires: libjpeg-devel
BuildRequires: doxygen
BuildRequires: gperf
Requires: httpd,php

%description
Naemon is an application, system and network monitoring application.
It can escalate problems by email, pager or any other medium. It is
also useful for incident or SLA reporting. It is originally a fork
of Nagios, but with extended functionality, stability and performance.

It is written in C and is designed as a background process,
intermittently running checks on various services that you specify.

The actual service checks are performed by separate "plugin" programs
which return the status of the checks to Naemon. The plugins are
compatible with Nagios, and can be found in the nagios-plugins package.


%package devel
Summary: Header files, libraries and development documentation for %{name}
Group: Development/Libraries
Requires: %{name} = %{version}-%{release}

%description devel
This package contains the header files, static libraries and development
documentation for %{name}. If you are a NEB-module author or wish to
write addons for Naemon using Naemon's own API's, you should install
this package.

%package contrib
Summary: Files from the contrib directory
Group: Development/Utils
Requires: %{name} = %{version}-%{release}

%description contrib
This package contains all the files from the contrib directory

%prep
%setup

# /usr/local/naemon is hardcoded in many places
%{__perl} -pi.orig -e 's|/usr/local/naemon/var/rw|%{_localstatedir}/naemon/rw|g;' contrib/eventhandlers/submit_check_result

%build

CFLAGS="%{mycflags} %{myXcflags}" LDFLAGS="$CFLAGS" %configure \
    --datadir="%{_datadir}/naemon" \
    --libexecdir="%{_libdir}/naemon/plugins" \
    --localstatedir="%{_localstatedir}/naemon" \
    --with-checkresult-dir="%{_localstatedir}/naemon/spool/checkresults" \
    --sbindir="%{_libdir}/naemon/cgi" \
    --sysconfdir="%{_sysconfdir}/naemon" \
    --with-cgiurl="/naemon/cgi-bin" \
    --with-command-user="apache" \
    --with-command-group="apache" \
    --with-gd-lib="%{_libdir}" \
    --with-gd-inc="%{_includedir}" \
    --with-htmurl="/naemon" \
    --with-init-dir="%{_initrddir}" \
    --with-lockfile="%{_localstatedir}/naemon/naemon.pid" \
    --with-mail="/bin/mail" \
    --with-naemon-user="naemon" \
    --with-naemon-group="naemon" \
    --with-template-objects \
    --with-template-extinfo \
    --enable-event-broker
find . -type f -name Makefile -exec /usr/bin/perl -p -i -e "s/-mtune=generic/-march=nocona/g" Makefile {} \; -print
%{__make} %{?_smp_mflags} all

### Build our documentaiton
%{__make} dox

### Apparently contrib does not obey configure !
%{__make} %{?_smp_mflags} -C contrib

%install
export PATH=%{_bindir}:/bin:\$PATH
%{__rm} -rf %{buildroot}
%{__make} install-unstripped install-init install-commandmode install-config \
    DESTDIR="%{buildroot}" \
    INSTALL_OPTS="" \
    COMMAND_OPTS="" \
    INIT_OPTS=""

%{__install} -d -m 0755 %{buildroot}%{_includedir}/naemon/
%{__install} -p -m 0644 include/*.h %{buildroot}%{_includedir}/naemon/
%{__mkdir} -p -m 0755 %{buildroot}/%{_includedir}/naemon/lib
%{__install} -m 0644 lib/*.h %{buildroot}/%{_includedir}/naemon/lib

%{__install} -Dp -m 0644 sample-config/httpd.conf %{buildroot}%{_sysconfdir}/httpd/conf.d/naemon.conf

### FIX log-paths
%{__perl} -pi -e '
        s|log_file.*|log_file=%{logdir}/naemon.log|;
        s|log_archive_path=.*|log_archive_path=%{logdir}/archives|;
        s|debug_file=.*|debug_file=%{logdir}/naemon.debug|;
   ' %{buildroot}%{_sysconfdir}/naemon/naemon.cfg

### make logdirs
%{__mkdir_p} %{buildroot}%{logdir}/
%{__mkdir_p} %{buildroot}%{logdir}/archives/

### Install logos
%{__mkdir_p} %{buildroot}%{_datadir}/naemon/images/logos

### Install documentation
%{__mkdir_p} %{buildroot}%{_datadir}/naemon/documentation
%{__cp} -a Documentation/html/* %{buildroot}%{_datadir}/naemon/documentation

# Put the new RC script in place
%{__install} -m 0755 daemon-init %{buildroot}/%{_initrddir}/naemon
%{__install} -d -m 0755 %{buildroot}/%{_sysconfdir}/sysconfig/
%{__install} -m 0644 naemon.sysconfig %{buildroot}/%{_sysconfdir}/sysconfig/naemon

### Apparently contrib wants to do embedded-perl stuff as well and does not obey configure !
%{__make} install -C contrib \
    DESTDIR="%{buildroot}" \
    INSTALL_OPTS=""

### Install libnaemon
%{__install} -m 0644 lib/libnaemon.a %{buildroot}%{_libdir}/libnaemon.a

%{__install} -d -m 0755 %{buildroot}%{_libdir}/naemon/plugins/eventhandlers/
%{__cp} -afpv contrib/eventhandlers/* %{buildroot}%{_libdir}/naemon/plugins/eventhandlers/
%{__mv} contrib/README contrib/README.contrib

CGI=`find contrib/ -name '*.cgi' -type f |sed s/'contrib\/'//g`
CGI=`for i in $CGI; do echo -n "$i|"; done |sed s/\|$//`
find %{buildroot}/%{_libdir}/naemon/cgi -type f -print | sed s!'%{buildroot}'!!g | egrep -ve "($CGI)" > cgi.files
find %{buildroot}/%{_libdir}/naemon/cgi -type f -print | sed s!'%{buildroot}'!!g | egrep "($CGI)" > contrib.files



%pre
if ! /usr/bin/id naemon &>/dev/null; then
    /usr/sbin/useradd -r -d %{logdir} -s /bin/sh -c "naemon" naemon || \
        %logmsg "Unexpected error adding user \"naemon\". Aborting installation."
fi
if ! /usr/bin/getent group nagiocmd &>/dev/null; then
    /usr/sbin/groupadd nagiocmd &>/dev/null || \
        %logmsg "Unexpected error adding group \"nagiocmd\". Aborting installation."
fi

%post
/sbin/chkconfig --add naemon

if /usr/bin/id apache &>/dev/null; then
    if ! /usr/bin/id -Gn apache 2>/dev/null | grep -q naemon ; then
        /usr/sbin/usermod -a -G naemon,nagiocmd apache &>/dev/null
    fi
else
    %logmsg "User \"apache\" does not exist and is not added to group \"naemon\". Sending commands to naemon from the command CGI is not possible."
fi

%preun
if [ $1 -eq 0 ]; then
    /sbin/service naemon stop &>/dev/null || :
    /sbin/chkconfig --del naemon
fi

%postun
# This could be bad if files are left with this uid/gid and then get owned by a new user
#if [ $1 -eq 0 ]; then
#    /usr/sbin/userdel naemon || %logmsg "User \"naemon\" could not be deleted."
#    /usr/sbin/groupdel naemon || %logmsg "Group \"naemon\" could not be deleted."
#fi
/sbin/service naemon condrestart &>/dev/null || :

%clean
%{__rm} -rf %{buildroot}

%files -f cgi.files
%defattr(-, root, root, 0755)
%doc Changelog INSTALLING LEGAL LICENSE README THANKS UPGRADING
%attr(0644,root,root) %config(noreplace) %{_sysconfdir}/httpd/conf.d/naemon.conf
%attr(0644,root,root) %config(noreplace) %{_sysconfdir}/sysconfig/naemon
%attr(0755,root,root) %config %{_initrddir}/naemon
%attr(0755,root,root) %{_bindir}/naemon
%attr(0755,root,root) %{_bindir}/naemonstats
%attr(0644,root,root) %{_libdir}/naemon/plugins/
%attr(0755,root,root) %{_datadir}/naemon/
%attr(0755,naemon,naemon) %dir %{_sysconfdir}/naemon/
%attr(0644,naemon,naemon) %config(noreplace) %{_sysconfdir}/naemon/*.cfg
%attr(0755,naemon,naemon) %{_sysconfdir}/naemon/objects/
%attr(0755,naemon,naemon) %dir %{_localstatedir}/naemon/
%attr(0755,naemon,naemon) %{_localstatedir}/naemon/
%attr(0755,naemon,naemon) %{logdir}/
%attr(0755,naemon,apache) %{_localstatedir}/naemon/rw/
%attr(0644,root,root) %{_libdir}/libnaemon.a

%files devel
%attr(0755,root,root) %{_includedir}/naemon/

%files contrib -f contrib.files
%%doc contrib/README.contrib
%attr(0755,root,root) %{_bindir}/convertcfg
%attr(0755,root,root) %{_libdir}/naemon/plugins/eventhandlers/

%changelog
* Wed Sep 18 2013 Daniel Wittenberg <dwittenberg2008@gmail.com> 4.0.0rc2-1
- Fix find command - Florin Andrei, bug #489
- Remove compiler warning option that breaks older builds, bug #488

* Fri Mar 15 2013 Daniel Wittenberg <dwittenberg2008@gmail.com> 3.99.96-1
- Major updates for version 4.0
- New spec file, new RC script, new sysconfig
