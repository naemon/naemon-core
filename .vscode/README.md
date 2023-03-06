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
The shown configuration is tested on Ubuntu 22.04 and Fedora 37.
This documentation will most likely also work flowlesly on new or
older versions of Ubuntu and Fedora.

In addition it is possible to develop on a Windows system using WSL2 ([Windows Subsystem for Linux](https://learn.microsoft.com/en-us/windows/wsl/install))


## Reqirements
- [Visual Studio Code](https://code.visualstudio.com/)
  - [C/C++ Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)


## Install build dependencies
Naemon itself depends on several libraries. Everything needed can be easily
installed via the package manager of your distribution.

### Fedora
```
sudo dnf group install "Development Tools"
sudo dnf install git glib2-devel help2man gperf gcc gcc-c++ gdb cmake3 pkgconfig automake autoconf nagios-plugins-all libtool perl-Test-Simple

sudo ln -s /usr/lib64/nagios /usr/lib/nagios
```

### Ubuntu
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

## Known issues
If you get an error message like `Configured debug type 'cppdbg' is not supported` please make
sure you have the [C/C++ Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) for
VS Code installed and enabled.

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
