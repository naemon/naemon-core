# Naemon development environment

Contributing to an open source project can be a challenging task,
even without figuring out how to launch the corresponding software
inside an IDE.
We are more than happy to see that you are interested in
contributing to the Naemon Core project.

To help you getting started, we provide predefined configurations
for [Visual Studio Code](https://code.visualstudio.com/) which will
attach a debugger and has predefined tasks to run the tests.

Basically this is everything you need to start coding.

Due to Naemon is program for Linux, a Linux system is required for development.
The shown configuration is tested on Ubuntu 26.04 and Fedora 44.
This documentation will most likely also work flowlesly on new or
older versions of Ubuntu and Fedora.

In addition it is possible to develop on a Windows system using WSL2 ([Windows Subsystem for Linux](https://learn.microsoft.com/en-us/windows/wsl/install))

On hosts that cannot run Naemon natively, for example macOS, use the
[dev container](#development-in-a-dev-container-macos-and-others) described below.


## Reqirements
- [Visual Studio Code](https://code.visualstudio.com/)
  - [C/C++ Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)

VS Code offers to install the recommended extensions as soon as you open this
repository, see `.vscode/extensions.json`. If you dismissed that notification,
run `Extensions: Show Recommended Extensions` from the command palette (`F1`).


## Install build dependencies
Naemon itself depends on several libraries. Everything needed can be easily
installed via the package manager of your distribution.

#### Fedora 44
```
sudo dnf group install development-tools
sudo dnf install git glib2-devel help2man gperf gcc gcc-c++ gdb cmake pkgconfig automake autoconf nagios-plugins-all libtool perl-Test-Simple

sudo ln -s /usr/lib64/nagios /usr/lib/nagios
```

### Ubuntu 26.04
```
sudo apt-get install git build-essential automake gperf gcc g++ gdb cmake help2man libtool libglib2.0-dev pkg-config libtest-simple-perl monitoring-plugins
```

## Setup VS Code
1. Clone this repository and open the folder with Visual Studio Code

2. Naemon requires a configuration file to launch.
Luckily there is a pre-configured task that will do all that for you.
From the menu select `Terminal > Run Task... > initial`
Normally you only need to run this task once.

3. You are ready to rock! Make your code changes, create breakpoints and so on.
To run Naemon with an debugger attached, select `Run and Debug > Start Debugging`
![VSCode with running Debugger](/.vscode/vscode_debugger.png)

4. Before you push your code changes, please make sure that all the tests are still green.
Again there is a predefined task you can execute via
`Terminal > Run Task... > Run Tests`
If all tests passed, feel free to push you code and to create a pull request.

## Naemon configuration files
Just in case you want to provide your own `naemon.cfg` or any other configuration file
just copy the files to `build/etc/naemon/`

## Windows Subsystem for Linux
If you prefer to use Windows as platform, please make sure to install a Ubuntu or Fedora WSL2 instance.
The steps are the exactly the same as described above. We recommend to use the [Windows Terminal](https://apps.microsoft.com/store/detail/windows-terminal/9N0DX20HK701?hl=de-de&gl=de)
to get access to the Linux shell.

Make sure you have Visual Studio Code installed on your Windows System. To launch VS Code with the files
from the Naemon project, simply run the `code` command.

Run these commands on your WSL linux instance:
```
git clone https://github.com/naemon/naemon-core.git
cd naemon-core/
code .
```
![Using WSL2](/.vscode/vscode_wsl.png)


## Development in a dev container (macOS and others)

Naemon only builds and runs on Linux, but you do not need a virtual machine for
it. The `.devcontainer/` directory in this repository describes a ready to use
Ubuntu 26.04 container. Visual Studio Code runs its backend inside that
container, so the debugger, IntelliSense, the integrated terminal and the test
suite all behave exactly like on a native Linux machine. Everything described
above stays valid, including the tasks and the launch configurations.

### Requirements
- [Docker Desktop](https://www.docker.com/products/docker-desktop/) (or any
  other Docker runtime)
- [Visual Studio Code](https://code.visualstudio.com/)
  - [Dev Containers Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers),
    which is part of the recommended extensions mentioned above

### Getting started
1. Clone this repository and open the folder with Visual Studio Code.

2. VS Code will offer to reopen the folder in the container. If it does not,
   run `Dev Containers: Reopen in Container` from the command palette
   (`F1`). The first start builds the image and takes a few minutes.

3. Continue exactly as on native Linux: run the task `initial` once via
   `Terminal > Run Task... > initial`, then start the debugger via
   `Run and Debug > Start Debugging`.

The container runs as the non-root user `ubuntu`, because Naemon refuses to
start as root. It is started with `SYS_PTRACE` and an unconfined seccomp
profile, otherwise gdb would not be able to attach to the process.

### Event broker modules

Naemon can load event broker modules such as
[mod_gearman](https://github.com/sni/mod_gearman) or the
[Statusengine broker](https://github.com/statusengine/broker) as shared
libraries. Both can be built and debugged inside the same container.

Clone the modules **next to** your naemon-core checkout:

```
git clone https://github.com/naemon/naemon-core.git
git clone https://github.com/sni/mod_gearman.git
git clone https://github.com/statusengine/broker.git
```

That parent directory is mounted at `/workspaces/modules` inside the container.
If you keep your module sources somewhere else, copy `.devcontainer/.env.example`
to `.devcontainer/.env` and set `NAEMON_MODULES_DIR`.

Both modules need a Gearman job server, which is started as a second container
alongside the dev container. It is reachable as `gearmand:4730` from within the
container, and as `localhost:4730` from your host.

Then, inside the container:

1. Run the task `initial` if you have not already. Both modules locate Naemon
   through `build/lib/pkgconfig/naemon.pc`, which is created by `make install`.

2. Build the module you are interested in:
   - `Terminal > Run Task... > broker module: build (mod_gearman)`
   - `Terminal > Run Task... > broker module: build (statusengine)`

3. Install the example configuration via
   `Terminal > Run Task... > broker modules: install config drop-ins`.
   This copies a `broker_module=` drop-in into
   `build/etc/naemon/module-conf.d/` and the matching module configuration into
   `build/etc/naemon/`. Existing files are never overwritten. Delete the drop-in
   of the module you do not want to load, and adjust the paths if needed.

4. Start the debugger with the launch configuration
   `Launch in gdb (with broker modules)`.

### Setting breakpoints inside a broker module

You do not need a second VS Code window. The module sources are mounted into
the very same container, so the running VS Code can open them directly.

The most comfortable way is to add the module to your workspace:
`File > Add Folder to Workspace...` and pick for example
`/workspaces/modules/statusengine`. It then shows up in the Explorer next to
naemon-core and you set breakpoints by clicking in the gutter, exactly like in
the Naemon sources. Alternatively just open a single file through
`File > Open File...` without adding the folder.

Two things are worth knowing:

- Until Naemon has `dlopen()`ed the module, a breakpoint in it stays **grey and
  unverified** and hovering it says the source file is not known yet. That is
  expected and not an error. The launch configuration sets
  `set breakpoint pending on`, so the breakpoint binds by itself the moment
  `neb_load_all_modules()` loads the module, and execution stops there.
- This only works if the module carries debug symbols. The build tasks above
  take care of that (`--enable-debug` for mod_gearman, `--buildtype=debug` for
  statusengine) and deliberately do not install the module, so it is loaded
  straight from the build directory where those symbols live.

Note that IntelliSense in the added folder uses its own defaults. Red squiggles
there say nothing about whether the module builds or whether the debugger
works.

To actually have checks executed by mod_gearman, start a worker in a second
terminal:

```
/workspaces/modules/mod_gearman/mod_gearman_worker \
    --config=/workspaces/naemon-core/build/etc/naemon/mod_gearman_worker.conf
```

`gearadmin --host gearmand --status` shows the queues Naemon has created, which
is a quick way to check that the module reached the job server.

![Naemon running with loaded Broker Modules inside a dev container](/.vscode/naemon_with_broker_via_devcontainer.png)


## Known issues
If you get an error message like `Configured debug type 'cppdbg' is not supported` please make
sure you have the [C/C++ Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) for
VS Code installed and enabled.

