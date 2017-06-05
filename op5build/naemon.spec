%if 0%{?rhel} >= 7
%define initdir --with-initdir=
%else
%define initdir %{nil}
%endif

%define daemon_user monitor
%define daemon_group apache

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
Requires: glib2
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
BuildRequires: glib2-devel
BuildRequires: check
BuildRequires: check-devel
BuildRequires: shadow-utils
%if 0%{?rhel} >= 7
BuildRequires: systemd
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
Requires: glib2-devel

%description devel
This package contains header files and static libraries used to
develop eventbroker modules and Nagios addons.


%prep
%setup -q


%build
echo %{version} > .version_number
%if 0%{?rhel} >= 7
# modify the existing configure.ac file so that it can generate
# naemon.service file.
sed --follow-symlinks --in-place '/AC_CONFIG_FILES/ a op5build/naemon.service' configure.ac
%endif

autoreconf -i -s
%configure \
		--with-naemon-user=%daemon_user \
		--with-naemon-group=%daemon_group \
		--localstatedir=/var/cache/naemon \
		--with-pkgconfdir=/opt/monitor/etc \
		--with-logdir=/opt/monitor/var \
		--with-lockfile=/var/cache/naemon/naemon.pid \
		--with-tempdir=/var/cache/naemon \
		--with-checkresultdir=/var/cache/naemon/checkresults \
		%initdir


%__make V=1
%__make check V=1
%__make distcheck V=1


%install
rm -rf %buildroot

make install DESTDIR=%buildroot

%if 0%{?rhel} >= 7
mkdir --parents %{buildroot}%{_unitdir}
cp op5build/naemon.service %{buildroot}%{_unitdir}/naemon.service
%else
# op5kad config file
mkdir -p %buildroot%_sysconfdir/op5kad/conf.d
cp op5build/kad.conf %buildroot%_sysconfdir/op5kad/conf.d/naemon.kad
ln --symbolic naemon %buildroot/etc/init.d/monitor

# Replace installed (el7 compat) logrotate file with el6 version
cp naemon.logrotate.el6 %buildroot/%_sysconfdir/logrotate.d/naemon
%endif

# limits.conf
mkdir -p %buildroot%_sysconfdir/security/limits.d
install -m 644 op5build/limits.conf %buildroot%_sysconfdir/security/limits.d/98-monitor.conf

mkdir -p %buildroot/opt/monitor/bin
ln -s ../../../%_bindir/naemon %buildroot/opt/monitor/bin/monitor

%preun
%if 0%{?rhel} >= 7
if [ $1 -eq 0 ]; then
	systemctl stop naemon || :
fi
%else
if [ $1 -eq 0 ]; then
	service naemon stop || :
fi
%endif

%pre
# we do this unconditionally. The pidfile has moved, so
# an old instance of monitor may be running that can't
# otherwise be shut down without issuing "killall" or by
# looking up its pid. Since we start unconditionally too,
# it also provides a nice symmetry
%if 0%{?rhel} >= 7
systemctl stop naemon >/dev/null 2>&1 || :
%else
service monitor stop >/dev/null 2>&1 || :
%endif

%if 0%{?rhel} >= 7
%else
if chkconfig --list monitor &>/dev/null; then
	chkconfig --del monitor
fi
%endif

%post
if grep -q 'broker_module.*command\.so' /opt/monitor/etc/naemon.cfg; then
	sed --follow-symlinks -i \
		-e '/broker_module.*command\.so/d' \
		/opt/monitor/etc/naemon.cfg
fi

%if 0%{?rhel} >= 7
systemctl daemon-reload
systemctl enable naemon.service
%else
chkconfig --add naemon
%endif

#Do nagios/monitor legacy init migrations (MON-7972 & MON-7975)
if [ ! -f /etc/sysconfig/naemon ]; then
	if [ -f /etc/profile.d/check_oracle.sh ]; then
		#Migrate check_oracle.sh solution from /etc/profile.d as suggested by KB to
		# /etc/sysconfig/naemon
		#We don't remove the check_oracle.sh thing, since users might make use of it
		# in interactive sessions or whatever, and it costs us nothing to keep it around
		cat /etc/profile.d/check_oracle.sh >> /etc/sysconfig/naemon
	fi
	if [ -f /etc/sysconfig/monitor ]; then
		# Migrate /etc/sysconfig/monitor to /etc/sysconfig/naemon and remove it
		cat /etc/sysconfig/monitor >> /etc/sysconfig/naemon
		rm /etc/sysconfig/monitor
	fi
	# Link the old path to the new path for backwards compat.
	ln -s /etc/sysconfig/naemon /etc/sysconfig/monitor
	# Now, do ramdisk directory creation if configured above (presumably,
	# $USE_RAMDISK was set in /etc/sysconfig/monitor above)
	cat <<"RAMDISKDOC" >> /etc/sysconfig/naemon
# Check if we are going to use RAMDISK or not to save
# our perfdata ans checkresults, to lower I/O load
if [ ${USE_RAMDISK:-0} == 1 ]; then
	if [ ! -d /dev/shm/monitor/var/spool/perfdata ]; then
		mkdir -p /dev/shm/monitor/var/spool/perfdata
	fi
	if [ ! -d /dev/shm/monitor/var/spool/checkresults ]; then
		mkdir -p /dev/shm/monitor/var/spool/checkresults
	fi
	chmod -R 775 /dev/shm/monitor/var/spool
	chown -R %{daemon_user}:%{daemon_group} /dev/shm/monitor
fi
RAMDISKDOC
fi

# Since we didn't handle the PERLLIB/op5plugins.sh stuff in f15cdc80 already,
# we must handle this now, separately from the block above, by inserting it
# into the sysconfig/naemon file, even if the file already exists. Otherwise,
# users that already upgraded to a package that contains f15cdc80 won't get
# a "proper" environment for naemon in some cases (such as its boot-time env).
# Quite nasty, but we supply check plugins that requires this env var.
if ! egrep -q '^[[:space:]]*export[[:space:]]+PERLLIB=' /etc/sysconfig/naemon; then
	# As sed(1) puts it:
	# i \
	# text   Insert text, which has each embedded newline preceded by a backslash.
	sed_insert_block='\
if [ "$PERLLIB" == /opt/plugins ]; then\
	export PERLLIB\
elif [ -n "$PERLLIB" ]; then\
	export PERLLIB="/opt/plugins:$PERLLIB"\
else\
	export PERLLIB=/opt/plugins\
fi\
'
	# Insert at BOF/line 1.
	sed -i "1 i$sed_insert_block" /etc/sysconfig/naemon
fi

%posttrans
# this is run after all other transactions, which means we
# *always* start the service even if it gets stopped by
# the "postun" scriptlet of an earlier package
%if 0%{?rhel} >= 7
systemctl start naemon || :
%else
service naemon start || :
%endif


%files
%defattr(-,root,root)
%if 0%{?rhel} >= 7
%attr(664, root, root) %{_unitdir}/naemon.service
%else
%_sysconfdir/init.d/naemon
%_sysconfdir/init.d/monitor
%_sysconfdir/op5kad/conf.d/naemon.kad
%endif
%_sysconfdir/logrotate.d/naemon
%doc README LEGAL UPGRADING
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
