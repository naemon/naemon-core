%define logmsg logger -t naemon/rpm

Summary: Open Source Host, Service And Network Monitoring Program
Name: naemon-core
Version: 1.4.2
Release: 0
License: GPL-2.0-only
Group: Applications/System
URL: https://www.naemon.io/
Packager: Naemon Core Development Team <naemon-dev@monitoring-lists.org>
Vendor: Naemon Core Development Team
Source0: naemon-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
BuildRequires: gperf
BuildRequires: logrotate
BuildRequires: autoconf
BuildRequires: automake
BuildRequires: libtool
BuildRequires: gcc-c++
BuildRequires: help2man
BuildRequires: libicu-devel
BuildRequires: pkgconfig
BuildRequires: glib2-devel
BuildRequires: check-devel

# sles / rhel specific requirements
%if 0%{?el9}%{?el8}%{?el7}%{?is_fc}
BuildRequires: chrpath
%endif
%if 0%{?el9}%{?el8}
BuildRequires: gdb-headless
%endif
%if 0%{?systemd_requires}
%systemd_requires
%endif

%if %{defined suse_version}
# Specifically require systemd for SUSE
# as the macro doesn't seem to function well
Requires(pre): systemd
Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd
BuildRequires: pkgconfig(systemd)
%if 0%{suse_version} < 1230
Requires(pre): pwdutils
%else
Requires(pre): shadow
%endif
%if 0%{?suse_version} < 1315
Requires(pre): shadow-utils
%endif
%endif
Requires:  logrotate
Requires:  libnaemon >= %{version}

%description
Naemon is an application, system and network monitoring application.
It can escalate problems by email, pager or any other medium. It is
also useful for incident or SLA reporting. It is originally a fork
of Nagios, but with extended functionality, stability and performance.

It is written in C and is designed as a background process,
intermittently running checks on various services that you specify.

The actual service checks are performed by separate "plugin" programs
which return the status of the checks to Naemon. The plugins are
compatible with Nagios, and can be found in the monitoring-plugins package.

Naemon ships the Thruk gui with extended reporting and dashboard features.

# disable binary striping
%global __os_install_post %{nil}


%package dbg
Summary:   Naemon Monitoring Debug Core
Group:     Applications/System

%description dbg
contains the naemon core with debug symbols.


%package -n naemon-devel
Summary: Development Files For Naemon
Group: Development/Libraries
Requires: libnaemon = %version
Requires: glib2-devel

%description -n naemon-devel
This package contains the header files, static libraries for naemon.
If you are a NEB-module author or wish to write addons for Naemon
using Naemons own APIs, you should install this package.


%package -n libnaemon
Summary: Shared Library for Naemon and NEB modules
Group: Development/Libraries

%description -n libnaemon
libnaemon contains the shared library for building NEB modules or addons for
Naemon.



%prep
%setup -q -n naemon-%{version}

%build
test -f configure || ./autogen.sh
%configure \
    --datadir="%{_datadir}/naemon" \
    --libdir="%{_libdir}/naemon" \
    --includedir="%{_includedir}" \
    --localstatedir="%{_localstatedir}/lib/naemon" \
    --sysconfdir="%{_sysconfdir}/naemon" \
    --with-pkgconfdir="%{_sysconfdir}/naemon" \
    --with-pluginsdir="%{_libdir}/naemon/plugins" \
    --with-tempdir="%{_localstatedir}/cache/naemon" \
    --with-checkresultdir="%{_localstatedir}/cache/naemon/checkresults" \
    --with-logdir="%{_localstatedir}/log/naemon" \
    --with-initdir="%{_initrddir}" \
    --with-logrotatedir="%{_sysconfdir}/logrotate.d" \
    --with-naemon-user="naemon" \
    --with-naemon-group="naemon" \
    --with-lockfile="/run/naemon/naemon.pid"
%{__make} %{?_smp_mflags} -j 1 all

%install
%{__rm} -rf %{buildroot}
%{__make} install \
    DESTDIR="%{buildroot}" \
    INSTALL_OPTS="" \
    COMMAND_OPTS="" \
    INIT_OPTS=""

# because we globally disabled binary striping, we have to do this manually for some files
%{__cp} -p %{buildroot}%{_bindir}/naemon %{buildroot}%{_bindir}/naemon-dbg
%{__strip} %{buildroot}%{_bindir}/naemon
%{__strip} %{buildroot}%{_bindir}/naemonstats
%{__strip} %{buildroot}%{_libdir}/naemon/libnaemon.so.0.0.0
%{__mv} %{buildroot}%{_sysconfdir}/logrotate.d/naemon %{buildroot}%{_sysconfdir}/logrotate.d/%{name}
%{__mv} %{buildroot}%{_libdir}/naemon/pkgconfig %{buildroot}%{_libdir}/pkgconfig
%{__mkdir_p} -m 0755 %{buildroot}%{_datadir}/naemon/examples
%{__mv} %{buildroot}%{_sysconfdir}/naemon/conf.d %{buildroot}%{_datadir}/naemon/examples/
%{__mkdir_p} -m 0755 %{buildroot}%{_sysconfdir}/naemon/conf.d
%{__mkdir_p} -m 0755 %{buildroot}%{_sysconfdir}/naemon/module-conf.d
%{__mkdir_p} -m 0755 %{buildroot}%{_localstatedir}/lib/naemon
%{__mkdir_p} -m 2775 %{buildroot}%{_localstatedir}/cache/naemon/checkresults
%{__mkdir_p} -m 0755 %{buildroot}%{_localstatedir}/cache/naemon

# Put the new RC sysconfig in place
%if 0%{?suse_version} >= 1315
sed -i daemon-systemd -e '/EnvironmentFile/d'
%else
%{__install} -d -m 0755 %{buildroot}/%{_sysconfdir}/sysconfig/
%{__install} -m 0644 sample-config/naemon.sysconfig %{buildroot}/%{_sysconfdir}/sysconfig/naemon
%endif

# Make sure the default run directory exists
mkdir -p -m 0755 %{buildroot}%{_localstatedir}/run/naemon

%{__mkdir_p} -m 0755 %{buildroot}%{_libdir}/naemon/
%{__ln_s} %{_libdir}/nagios/plugins %{buildroot}%{_libdir}/naemon/plugins

%if %{?_unitdir:1}0
# Install systemd entry
%{__install} -D -m 0644 -p daemon-systemd %{buildroot}%{_unitdir}/naemon.service
%{__install} -D -m 0644 -p naemon.tmpfiles.conf %{buildroot}%{_tmpfilesdir}/naemon.conf
%{__install} -d -m 0755 %{buildroot}/%{_localstatedir}/run/naemon/
# Move SystemV init-script
%{__mv} -f %{buildroot}%{_initrddir}/naemon %{buildroot}/%{_bindir}/naemon-ctl
%endif

%if 0%{?el6}
%{__rm} %{buildroot}%{_sysconfdir}/logrotate.d/%{name}
%{__install} -m 0644 naemon.logrotate.el6 %{buildroot}%{_sysconfdir}/logrotate.d/%{name}
%endif

%clean
%{__rm} -rf %{buildroot}



%pre
if ! /usr/bin/id naemon &>/dev/null; then
    /usr/sbin/useradd -r -d %{_localstatedir}/lib/naemon -s /bin/sh -c "naemon" naemon || \
        %logmsg "Unexpected error adding user \"naemon\". Aborting installation."
fi
if ! /usr/bin/getent group naemon &>/dev/null; then
    /usr/sbin/groupadd naemon &>/dev/null || \
        %logmsg "Unexpected error adding group \"naemon\". Aborting installation."
fi

%post
case "$*" in
  2)
    # Upgrade, don't do anything
    # Restarts are handled in posttrans
  ;;
  1)
    # install example conf.d only once on the first installation
    if [ ! -d %{_sysconfdir}/naemon/conf.d/templates ]; then
        mkdir -p %{_sysconfdir}/naemon/conf.d/
        cp -rp %{_datadir}/naemon/examples/conf.d/* %{_sysconfdir}/naemon/conf.d/
    fi
    chown naemon:naemon \
        /etc/naemon/conf.d \
        /etc/naemon/conf.d/*.cfg \
        /etc/naemon/conf.d/templates \
        /etc/naemon/conf.d/templates/*.cfg
    chown naemon:naemon \
        /etc/naemon/module-conf.d/*.cfg 2>/dev/null
    chmod 0664 /etc/naemon/conf.d/*.cfg /etc/naemon/conf.d/templates/*.cfg
    chmod 2775 /etc/naemon/conf.d /etc/naemon/conf.d/templates
    %if %{?_unitdir:1}0
      %systemd_post naemon.service
    %else
      chkconfig --add naemon
    %endif
  ;;
  *) echo case "$*" not handled in post
esac

touch /var/log/naemon/naemon.log
chmod 0664 /var/log/naemon/naemon.log
chown naemon:naemon /var/log/naemon/naemon.log
%if %{?_unitdir:1}0
systemd-tmpfiles --create %{_tmpfilesdir}/naemon.conf
%endif

%preun
case "$*" in
  1)
    # Upgrade, don't do anything
  ;;
  0)
    # Uninstall, go ahead and stop before removing
    %if %{?_unitdir:1}0
      %systemd_preun naemon.service
    %else
      /etc/init.d/naemon stop >/dev/null 2>&1 || :
      service naemon stop >/dev/null 2>&1 || :
      chkconfig --del naemon || :
    %endif
    rm -f %{_libdir}/naemon/status.dat
    rm -f %{_libdir}/naemon/naemon.qh
    rm -f %{_libdir}/naemon/naemon.tmp*
  ;;
  *) echo case "$*" not handled in preun
esac
exit 0

%postun
case "$*" in
  0)
    # POSTUN
    rm -f %{_localstatedir}/cache/naemon/naemon.configtest \
          %{_localstatedir}/lib/naemon/objects.cache \
          %{_localstatedir}/lib/naemon/objects.precache \
          %{_localstatedir}/lib/naemon/retention.dat \
          %{_localstatedir}/lib/naemon/naemon.cmd \
          %{_localstatedir}/log/naemon/naemon.log \
          %{_localstatedir}/log/naemon/archives
    rm -rf /var/run/naemon
    %if 0%{?insserv_cleanup}
      %{insserv_cleanup}
    %endif
    chkconfig --del naemon >/dev/null 2>&1 || :
    systemctl try-restart naemon.service >/dev/null 2>&1 || :
    rm -rf %{_libdir}/naemon/.local
    ;;
  1)
    # POSTUPDATE
    ;;
  *) echo case "$*" not handled in postun
esac
exit 0

%posttrans
# try and restart if already running
# For systemctl systems we need to reload the configs
# because it'll complain if we just installed a new
# init script
%if %{?_unitdir:1}0
  systemctl daemon-reload &>/dev/null || true
  systemctl condrestart naemon.service &>/dev/null || true
%else
  /etc/init.d/naemon condrestart &>/dev/null || true
%endif



%files
%doc README.md
%doc AUTHORS
%doc COPYING
%doc ChangeLog
%doc INSTALL
%doc LEGAL
%doc NEWS
%doc README
%doc THANKS
%doc UPGRADING
%doc naemon.rpmlintrc
%attr(0755,root,root) %{_bindir}/naemon
%if %{?_unitdir:1}0
  %attr(0644,root,root) %{_unitdir}/naemon.service
  %attr(0644,root,root) %{_tmpfilesdir}/naemon.conf
  %attr(0755,root,root) %{_bindir}/naemon-ctl
  %attr(-,-,-) %dir %{_tmpfilesdir}
%else
  %attr(0755,root,root) %{_initrddir}/naemon
%endif
%config(noreplace) %{_sysconfdir}/logrotate.d/naemon-core
%attr(0755,root,root) %dir %{_sysconfdir}/naemon/
%attr(0755,root,root) %dir %{_libdir}/naemon
%attr(0755,root,root) %dir %{_datadir}/naemon
%attr(2775,naemon,naemon) %dir %{_sysconfdir}/naemon/conf.d
%attr(0755,naemon,naemon) %dir %{_sysconfdir}/naemon/module-conf.d
%attr(0644,naemon,naemon) %config(noreplace) %{_sysconfdir}/naemon/naemon.cfg
%attr(0640,naemon,naemon) %config(noreplace) %{_sysconfdir}/naemon/resource.cfg
%if 0%{?suse_version} < 1315
%attr(0644,root,root) %config(noreplace) %{_sysconfdir}/sysconfig/naemon
%endif
%attr(2775,naemon,naemon) %dir %{_localstatedir}/cache/naemon
%attr(2775,naemon,naemon) %dir %{_localstatedir}/cache/naemon/checkresults
%attr(0755,naemon,naemon) %dir %{_localstatedir}/lib/naemon
%attr(0755,naemon,naemon) %dir %{_localstatedir}/log/naemon
%attr(0755,naemon,naemon) %dir %{_localstatedir}/log/naemon/archives
%attr(-,root,root) %{_libdir}/naemon/plugins
%{_mandir}/man8/naemon.8*
%{_datadir}/naemon/examples
%attr(0755,root,root) %{_bindir}/naemonstats
%{_mandir}/man8/naemonstats.8*

%files -n libnaemon
%attr(0755,root,root) %dir %{_libdir}/naemon
%attr(-,root,root) %{_libdir}/naemon/libnaemon.so*

%files dbg
%attr(0755,root,root) %{_bindir}/naemon-dbg

%files -n naemon-devel
%attr(-,root,root) %{_includedir}/naemon/
%attr(0755,root,root) %dir %{_libdir}/naemon
%attr(-,root,root) %{_libdir}/naemon/libnaemon.a
%attr(-,root,root) %{_libdir}/naemon/libnaemon.la
%attr(-,root,root) %{_libdir}/pkgconfig/naemon.pc

%changelog
* Tue Sep 19 2017 Sven Nierlein <sven.nierlein@consol.de> 1.0.7-1
- Decouple naemon-core and naemon-livestatus

* Sun Jun 21 2015 Sven Nierlein <sven.nierlein@consol.de> 1.0.4-1
- Decouple thruk and replace with metapackage

* Sun Feb 23 2014 Daniel Wittenberg <dwittenberg2008@gmail.com> 0.8.0-2
- Add native and full systemctl control on el7

* Thu Feb 06 2014 Daniel Wittenberg <dwittenberg2008@gmail.com> 0.1.0-1
- Add reload for systemctl-based setups

* Thu Feb 06 2014 Sven Nierlein <sven.nierlein@consol.de> 0.1.0-1
- moved thruks reporting addon into separate package

* Tue Nov 26 2013 Sven Nierlein <sven.nierlein@consol.de> 0.0.1-1
- initial naemon meta package
