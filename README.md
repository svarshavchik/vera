vera
====

vera is an init daemon supervisor (PID 1) that uses Linux kernel control
groups (cgroup v2, or lightweight containers). It is a replacement for
/sbin/init.

Containers allow reliable, foolproof shutdown and termination of started
services.

vera was developed on Slackware 15.0. It is capable of booting and
shutting down a stock installation of Slackware 15.

Features
--------

* Uses cgroup version 2

* It's just a PID 1 supervisor, nothing more, nothing less (except for
containers)

* Container units' specification files use a basic, simple, documented
YAML syntax

* Includes a script for migrating /etc/inittab and /etc/rc.d/rc?.d
entries to vera's container units

* Optional cgroup v2 controllers: if enabled it's possible to enforce limits
on resources used by the container (memory limits, number of processes,
and other resources)

* A mechanism for defining dependencies between containers,
which can be started at the same time, dependencies permitting

* A basic management interface with a status command to list all containers and
their processes, and commands to start and stop individual containers

* Provides commands for switching to vera and switching back to sysvinit.
Every attempt is made to make this as foolproof as possible

* Manual pages with full documentation

* [Screen capture of booting and shutdown under vera](https://www.youtube.com/watch?v=ds0oprq-dEE)

Download
--------

- [Download the latest release tarball](https://github.com/svarshavchik/vera/releases).
See the INSTALL file in the tarball for build instructions

- Instead of compiling, a package for Slackware 15 is also
available, but see the INSTALL file as well for additional information

- PGP verification keys: https://www.courier-mta.org/KEYS.bin
