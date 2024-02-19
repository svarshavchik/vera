vera
====

vera is an alternative init daemon supervisor (PID 1) that uses containers.
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

* Includes a script for wrapping /etc/inittab and /etc/rc.d/rc?.d
entries in vera's container units

* A fully fleshed out mechanism for defining dependencies between containers,
which can be started at the same time, dependencies permitting

* A basic management interface: a status command that lists all containers and
all processes in each container, start and stop individual containers

* Provides commands for switching to vera, and switching back to sysvinit,
every attempt is made to make this as foolproof as possible

* A manual page with complete documentation
