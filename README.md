## Welcome to Naemon Core ##

Naemon is a host/service/network monitoring program written in C and
released under the GNU General Public License. It works by scheduling
checks of the configured objects and then invoking plugins to do the
actual checking. The plugin interface is 100% Nagios compatible, since
Naemon is a fork of the aforementioned project.

### Installing ###

If you get this stuff from git, you should run "autoreconf -i" and then
```
./configure; make all; make install
```
to install everything in the default paths.

If you get a tarball you should be able to skip the "autoreconf" step.


### More info ###

Visit the Naemon homepage at http://www.naemon.org
