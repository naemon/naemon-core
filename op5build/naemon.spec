%if 0%{?suse_version}
%define daemon_group www
%else
%define daemon_group apache
%endif
%define daemon_user monitor

Summary: Core scheduling and checking engine for op5 Monitor
Name: op5-naemon
Version: %{op5version}
Release: %{op5release}%{?dist}
License: GPLv2
Group: op5/Monitor
URL: http://www.op5.se
Source0: %name-%version.tar.gz
Requires: sed >= 4.0.9
Requires: monitor-config >= 7.1.0
BuildRoot: %{_tmppath}/%{name}-%{version}
Obsoletes: monitor <= 6
Provides: monitor = 7
Obsoletes: op5-nagios <= 0:4.1
Provides: op5-nagios = 1:%version
Provides: naemon = %version
Obsoletes: monitor-command < 0.0.4
Provides: monitor-command = 0.0.4
BuildRequires: autoconf, automake, libtool
BuildRequires: gperf, help2man
BuildRequires: perl(Test::Simple)
BuildRequires: perl(Test::Harness)
%if 0%{?suse_version}
Requires: php53
Requires: apache2-mod_php53
#The suse distribution of glibc <= 2.11.3-17.43
#contains a dlclose/dlopen bug which caused globals
#in neb modules to never become unmapped - leading
#to weird behaviour on reload (HUP).
Requires: glibc >= 2.11.3-17.43.1
BuildRequires: apache2-mod_php53
BuildRequires: pwdutils
%else
BuildRequires: shadow-utils
%endif


%description
op5 Monitor is a system for monitoring network hosts and its various
services. It is based on Nagios, inheriting its plugin-driven design
and is thus highly extensible and flexible.

This package contains the op5 version of Nagios, which provides the
core scheduling, checking and notification logic.

%package devel
Summary: Header files and static library stuff for developers
Group: op5/Devel
# Automatic on rhel, because libnaemon.so in this package symlinks to
# libnaemon.so.0.0.0. But sles doesn't figure that out.
Requires: op5-naemon = %version

%description devel
This package contains header files and static libraries used to
develop eventbroker modules and Nagios addons.


%prep
%setup -q


%build
echo %{version} > .version_number
autoreconf -i -s
%configure --with-naemon-user=%daemon_user --with-naemon-group=%daemon_group --with-pkgconfdir=/opt/monitor/etc --with-logdir=/opt/monitor/var

# autofix modification time and program version with each package
sed -i \
	-e  's/^\(#define PROGRAM_MODIFICATION_DATE\).*/\1 \"'`date -I`'\"/' \
		src/naemon/common.h

%__make
%__make check
%__make distcheck


%install
rm -rf %buildroot

make install DESTDIR=%buildroot

# op5kad config file
mkdir -p %buildroot%_sysconfdir/op5kad/conf.d
cp op5build/kad.conf %buildroot%_sysconfdir/op5kad/conf.d/naemon.kad

# limits.conf
mkdir -p %buildroot%_sysconfdir/security/limits.d
install -m 644 op5build/limits.conf %buildroot%_sysconfdir/security/limits.d/98-monitor.conf

mkdir -p %buildroot/opt/monitor/bin
ln -s ../../../%_bindir/naemon %buildroot/opt/monitor/bin/monitor
ln -s naemon %buildroot/etc/init.d/monitor

%preun
if [ $1 -eq 0 ]; then
	service naemon stop || :
fi

%pre
# we do this unconditionally. The pidfile has moved, so
# an old instance of monitor may be running that can't
# otherwise be shut down without issuing "killall" or by
# looking up its pid. Since we start unconditionally too,
# it also provides a nice symmetry
service monitor stop >/dev/null 2>&1 || :

%if 0%{?suse_version}
if chkconfig --check monitor; then
%else
if chkconfig --list monitor &>/dev/null; then
%endif
	chkconfig --del monitor
fi

%post
if grep -q 'broker_module.*command\.so' /opt/monitor/etc/naemon.cfg; then
	sed --follow-symlinks -i \
		-e '/broker_module.*command\.so/d' \
		/opt/monitor/etc/naemon.cfg
fi

chkconfig --add naemon


%posttrans
# this is run after all other transactions, which means we
# *always* start the service even if it gets stopped by
# the "postun" scriptlet of an earlier package
service naemon start || :


%files
%defattr(-,root,root)
%_sysconfdir/init.d/naemon
%_sysconfdir/init.d/monitor
%_sysconfdir/logrotate.d/naemon
%doc README LEGAL UPGRADING
%_sysconfdir/op5kad/conf.d/naemon.kad
%_sysconfdir/security/limits.d/*.conf
%_libdir/libnaemon.so.*
%_bindir/naemon
%_bindir/naemonstats
%_mandir/man8/naemon*
/opt/monitor/bin/monitor
%attr(-,%daemon_user,%daemon_group) %dir %_localstatedir/cache/naemon/

# these are replaced by monitor-config:
%exclude /opt/monitor/etc
# I don't want to support this:
%exclude %_bindir/shadownaemon
%exclude %_mandir/man8/shadownaemon*
# https://fedoraproject.org/wiki/Packaging:Guidelines hates on static
# libraries, so I do, too
%exclude %_libdir/libnaemon.a
%exclude %_libdir/libnaemon.la

%files devel
%defattr(-,root,root)
%_includedir/naemon
%_libdir/pkgconfig/naemon.pc
%_libdir/libnaemon.so

%clean
rm -rf %buildroot

%changelog
* Fri Nov 2 2007 Andreas Ericsson <ae@op5.se>
- Package op5-specific files and upstream nagios stuff separately.
