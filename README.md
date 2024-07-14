vera
====

vera is an init daemon supervisor (PID 1) that uses Linux kernel control
groups (cgroupsv2, or lightweight containers). It is a replacement for
/sbin/init.

Containers allow reliable, foolproof shutdown and termination of started
services.

vera was developed on Slackware 15.0. It is capable of booting and
shutting down a stock installation of Slackware 15.

Features
--------

* Uses cgroups version 2

* It's just a PID 1 supervisor, nothing more, nothing less (except for
containers)

* Container units' specification files use a basic, simple, documented
YAML syntax

* Includes a script for migrating /etc/inittab and /etc/rc.d/rc?.d
entries to vera's container units

* A fully fleshed out mechanism for defining dependencies between containers,
which can be started at the same time, dependencies permitting

* A basic management interface: a status command that lists all containers and
all processes in each container, start and stop individual containers

* Provides commands for switching to vera, and switching back to sysvinit,
every attempt is made to make this as foolproof as possible

* A manual page with complete documentation

Download
--------

https://github.com/svarshavchik/vera/releases

- Download the latest release tarball, see the INSTALL file in the tarball
for build instructions

- Instead of compiling and building, a package for Slackware 15 is also
available, but see the INSTALL file as well for additional information

- PGP verification keys: https://www.courier-mta.org/KEYS.bin
