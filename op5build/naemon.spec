%if 0%{?suse_version}
%define daemon_group www
%define apache_service apache2
%define htmlroot /srv/www/htdocs
%define httpconfdir apache2/conf.d
%else
%define daemon_group apache
%define apache_service httpd
%define htmlroot /var/www/html
%define httpconfdir httpd/conf.d
%endif

%define daemon_user monitor
%define daemon_shell /bin/bash
%define daemon_uid 299
%define daemon_group2 sysmon
%define daemon_gid 48

Summary: Core scheduling and checking engine for op5 Monitor
Name: op5-naemon
Version: %{op5version}
Release: %{op5release}%{?dist}
License: GPLv2
Group: op5/Monitor
URL: http://www.op5.se
Source0: %name-%version.tar.gz
Requires: sed >= 4.0.9
Requires: monitor-config
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

%description devel
This package contains header files and static libraries used to
develop eventbroker modules and Nagios addons.


%prep
%setup -q


%build
echo %{version} > .version_number
autoreconf -i -s
%configure --with-naemon-user=monitor --with-naemon-group=monitor --with-pkgconfdir=/opt/monitor/etc --with-logdir=/opt/monitor/var

# autofix modification time and program version with each package
sed -i \
	-e  's/^\(#define PROGRAM_MODIFICATION_DATE\).*/\1 \"'`date -I`'\"/' \
		naemon/common.h

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

%post
chkconfig --add naemon

service naemon start || :

%files
%defattr(-,%daemon_user,%daemon_group)
%_sysconfdir/init.d/naemon
%_sysconfdir/init.d/monitor
%_sysconfdir/logrotate.d/naemon
%doc README LEGAL UPGRADING
%_sysconfdir/op5kad/conf.d/naemon.kad
%_sysconfdir/security/limits.d/*.conf
%_libdir/libnaemon.so.*
%_bindir/naemon
%_bindir/naemonstats
%_bindir/oconfsplit
%_mandir/man8/naemon*
%_mandir/man8/oconfsplit*
/opt/monitor/bin/monitor
%dir %_localstatedir/cache/naemon/

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
