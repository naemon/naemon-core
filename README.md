## Welcome to Naemon Core ##

![GitHub Workflow Status](https://img.shields.io/github/workflow/status/naemon/naemon-core/citest)

Naemon is a host/service/network monitoring program written in C and
released under the GNU General Public License. It works by scheduling
checks of the configured objects and then invoking plugins to do the
actual checking. The plugin interface is 100% Nagios compatible, since
Naemon is a fork of the aforementioned project.


### Contributing ###
Contributing to Naemon is meant to be easy, fun and profitable. I'm
not sure where the profit will come from, but if you get a warm glow
of pride when getting a patch accepted, you can consider that your
reward if you like.

The easiest way is probably to fork this project on github, and then
send pull requests to the original project. You can also send patches
to <naemon-dev@monitoring-lists.org>.

#### Commit messages ####
Commit messages MUST contain a Signed-off-by line and have a proper
author name and email address (even though "Anonhacker42 <anon@hax.com>
is considered "proper" in these circles). If you run `git commit -s`
you'll get the signed-off-by for free. The signed-off-by indicates that
you're telling us you have the right to submit this patch and that we
shouldn't worry about lawyers from whatever company you're working for
will come at us later and demand that we remove your contributions from
the code. It might not be much of a protection against such things,
but it's more or less standard praxis in the git-using projects, so
please just stick to it, ok?

Messages MUST contain a brief statement of why the change was made.
"Fix bugs" is a bad message, as it means people will have to know
which bugs you're fixing. "Make sure we don't segfault when the disk
is full" is a useful message, because it points to a problem and
makes it clear that the patch should fix it. In case deep analysis
was required in order to figure out the root cause of the problem,
you're encouraged to also write your findings there. It also makes
it look as if you did a whole lot of work and did it thoroughly,
which is pretty good for your resumé.

Messages SHOULD be written in imperative form, as if you're giving the
code orders on how it should change. It provides a much nicer basis
for discussion when a patch has to be reviewed online, as it indicates
that the change is about to take place but is open for discussion
rather than that it already has and that discussion isn't welcome.

Messages SHOULD have lines shorter than 72 chars. Most of the time,
people will inspect logs or blame output in a terminal or in a limited
width program, and it's a pain to have to scroll sideways all the time
to see the message. Please keep the lines short and it'll save some
annoyance on behalf of other people.


#### Coding standards ####

Common sense applies.
* Don't break backwards compatibility without a really good reason.
* Don't remove or alter API's unless absolutely necessary.
* Don't write huge functions that do a lot. It's hard to test those,
and we do like tests.
* Use the indentation already found in the files, or reindent to your
liking and then run "sh indent-all.sh" when you're done. That should
bring the tree back to some semblance of unity.
* Avoid sending patches with a lot of whitespace changes. They're hard
to review so they probably won't be.
* Don't engage in useless codechurn. If the patch you're submitting
doesn't solve an actual problem or paves the way for solving some
sort of problem or adding a feature, it's most likely not worth the
trouble.

### Installing ###

When installing from a released tarball, all you need to do is to run
```
./configure
make
sudo make install
```

If you want to help out with development and hence download the source from
git, you instead need to run
```
./autogen.sh
make
sudo make install
```


### More info ###

Visit the Naemon homepage at https://www.naemon.io
