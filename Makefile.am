SUBDIRS = po

ACLOCAL_AMFLAGS = -I m4
AM_CXXFLAGS = -std=c++20 @YAMLCPPFLAGS@

export LC_ALL=C

sbin_PROGRAMS=vera vera-init
noinst_SCRIPTS=vlad

pkgdata_SCRIPTS=rc.sysvinit.vera rc.local.vera rc.local_shutdown.vera \
	rc.controllers \
	vera-logrotate vera-logrotate-migrate vera-rcm vera-rck \
	update-logrotate

noinst_PROGRAMS=\
	testcontroller						\
	testcontroller2						\
	testcontroller2fdleak					\
	testcontroller3						\
	testcontroller4						\
	testdepcomputations 					\
	testhook						\
	testinittab						\
	testpoller						\
	testprocloader						\
	testswitchlog

EXTRA_DIST= 							\
	cgroup.controllers					\
	packaging/slackware/doinst.sh				\
	packaging/slackware/uninst.sh				\
	packaging/slackware/networkmanager-wait-online		\
	slack-desc						\
	testinittab.sh						\
	testinittab.txt						\
	testlogrotate.sh					\
	testprocloader.sh					\
	vera-logrotate-migrate					\
	vera-rck						\
	vera-rcm

BUILT_SOURCES=configdirs.h
CLEANFILES=configdirs.h

noinst_LIBRARIES=libvera.a

libvera_a_SOURCES=						\
	current_containers_infofwd.H				\
	current_containers_info.H				\
	external_filedesc.H					\
	external_filedesc.C					\
	external_filedesc_privcmdsocket.H			\
	external_filedesc_privcmdsocket.C			\
	hook.C							\
	hook.H							\
	inittab.C						\
	inittab.H						\
	log.C 							\
	log.H							\
	log_current_time.C					\
	messages.H 						\
	parsed_yaml.C						\
	parsed_yaml.H						\
	poller.C						\
	poller.H						\
	privrequest.C						\
	privrequest.H						\
	proc_container.C					\
	proc_container.H					\
	proc_container2.C					\
	proc_containerfwd.H					\
	proc_container_group.C					\
	proc_container_group.H					\
	proc_container_runner.C					\
	proc_container_runner.H					\
	proc_container_runnerfwd.H				\
	proc_container_run_info.H				\
	proc_container_state.C					\
	proc_container_state.H					\
	proc_container_statefwd.H				\
	proc_container_timer.C					\
	proc_container_timer.H					\
	proc_container_timerfwd.H				\
	proc_loader.C						\
	proc_loader.H						\
	proc_loaderfwd.H					\
	proc_loader2.C						\
	proc_loader3.C						\
	switchlog.C						\
	switchlog.H						\
	unit_test.H						\
	verac_connect_sun_socket.c				\
	verac_check_hook.c					\
	verac_parseinittab.c					\
	verac_runsysinit.c					\
	verac.h							\
	yaml_writer.C						\
	yaml_writer.H

configdirs.h: config.status Makefile
	echo '#define INSTALLCONFIGDIR "$(INSTALLCONFIGDIR)"' >configdirs.h.tmp
	echo '#define CONFIGDIR "$(CONFIGDIR)"' >>configdirs.h.tmp
	echo '#define RUNLEVELCONFIG "$(RUNLEVELCONFIG)"' >>configdirs.h.tmp
	echo '#define ENVIRONCONFIG "$(ENVIRONCONFIG)"' >>configdirs.h.tmp
	echo '#define LOCALCONFIGDIR "$(LOCALCONFIGDIR)"' >>configdirs.h.tmp
	echo '#define OVERRIDECONFIGDIR "$(OVERRIDECONFIGDIR)"' >>configdirs.h.tmp
	echo '#define LOCALSTATEDIR "$(localstatedir)"' >>configdirs.h.tmp
	echo '#define LOCALEDIR "$(localedir)"' >> configdirs.h.tmp
	echo '#define PKGDATADIR "$(pkgdatadir)"' >>configdirs.h.tmp
	echo '#define SBINDIR "$(sbindir)"' >>configdirs.h.tmp
	echo '#define SWITCHLOGDIR "$(SWITCHLOGDIR)"' >>configdirs.h.tmp
	mv configdirs.h.tmp configdirs.h

vera_SOURCES=vera.C
vera_LDADD=libvera.a @YAMLLIBS@

vera_init_SOURCES=vera-init.c
vera_init_LDADD=libvera.a

vlad: vera
	rm -f vlad; ln vera vlad
CLEANFILES += vlad

testcontroller_SOURCES=testcontroller.C unit_test.C
testcontroller_LDADD=libvera.a @YAMLLIBS@

testcontroller2_SOURCES=testcontroller2.C unit_test.C
testcontroller2_LDADD=libvera.a @YAMLLIBS@

testcontroller2fdleak_SOURCES=testcontroller2fdleak.C

testcontroller3_SOURCES=testcontroller3.C unit_test.C
testcontroller3_LDADD=libvera.a @YAMLLIBS@

testcontroller4_SOURCES=testcontroller4.C unit_test.C
testcontroller4_LDADD=libvera.a @YAMLLIBS@ -lpthread

testdepcomputations_SOURCES=testdepcomputations.C unit_test.C
testdepcomputations_LDADD=libvera.a @YAMLLIBS@

testprocloader_SOURCES=testprocloader.C unit_test.C
testprocloader_LDADD=libvera.a @YAMLLIBS@

testpoller_SOURCES=testpoller.C unit_test.C
testpoller_LDADD=libvera.a @YAMLLIBS@

testinittab_SOURCES=testinittab.C parsed_yaml.C inittab.C yaml_writer.C \
	proc_loader.C proc_container2.C
testinittab_LDADD=libvera.a @YAMLLIBS@

testhook_SOURCES=testhook.C
testhook_LDADD=libvera.a

testswitchlog_SOURCES=testswitchlog.C
testswitchlog_LDADD=libvera.a

UNITFILES=update-logrotate v2-controllers
CLEANFILES += $(UNITFILES)

EXTRA_DIST += $(patsubst %,%.in,$(UNITFILES))

all: $(UNITFILES)

define unitfile_dependency

$(1): $(1).in
	sed 's@~PKGDATADIR~@'"`echo $(pkgdatadir)`"'@' <$(srcdir)/$$@.in >$$@
	sed 's@~INSTALLCONFIGDIR~@'"`echo $(INSTALLCONFIGDIR)`"'@' <$(srcdir)/$$@.in >$$@

endef

$(eval $(foreach F,$(UNITFILES),	 \
	$(call unitfile_dependency,$(F)) \
	)				 \
)

install-exec-hook:
	rm -f $(DESTDIR)$(sbindir)/vlad
	ln $(DESTDIR)$(sbindir)/vera $(DESTDIR)$(sbindir)/vlad

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(INSTALLCONFIGDIR)/system
	$(INSTALL_DATA) update-logrotate \
		$(DESTDIR)$(INSTALLCONFIGDIR)/system/update-logrotate
	$(INSTALL_DATA) v2-controllers \
		$(DESTDIR)$(INSTALLCONFIGDIR)/system/v2-controllers
	$(MKDIR_P) $(DESTDIR)$(CONFIGDIR)
	$(MKDIR_P) $(DESTDIR)$(localstatedir)
	$(MKDIR_P) $(DESTDIR)$(SWITCHLOGDIR)
	chmod 700 $(DESTDIR)$(SWITCHLOGDIR)
	$(MKDIR_P) $(DESTDIR)$(LOCALCONFIGDIR)
	$(MKDIR_P) $(DESTDIR)$(OVERRIDECONFIGDIR)
	chmod 755 $(DESTDIR)$(INSTALLCONFIGDIR)
	chmod 755 $(DESTDIR)$(CONFIGDIR)
	$(INSTALL_DATA) $(srcdir)/cgroup.controllers $(DESTDIR)$(CONFIGDIR)/cgroup.controllers
	chmod 644 $(DESTDIR)$(CONFIGDIR)/cgroup.controllers
	chmod 755 $(DESTDIR)$(LOCALCONFIGDIR)
	chmod 755 $(DESTDIR)$(OVERRIDECONFIGDIR)
	umask 022; ./testprocloader genrunlevels \
		$(DESTDIR)$(RUNLEVELCONFIG) \
		$(DESTDIR)$(INSTALLCONFIGDIR)/system \
		$(DESTDIR)$(ENVIRONCONFIG) && \
		$(INSTALL_DATA) $(srcdir)/packaging/slackware/networkmanager-wait-online \
			$(DESTDIR)$(INSTALLCONFIGDIR)/system && \
		./testinittab -c $(DESTDIR)$(INSTALLCONFIGDIR) \
			-l $(DESTDIR)$(LOCALCONFIGDIR) \
			-p $(DESTDIR)${pkgdatadir} \
			-o $(DESTDIR)$(OVERRIDECONFIGDIR) \
			-r $(DESTDIR)$(RUNLEVELCONFIG) >/dev/null
	test "$(TESTDIR)" = "" || exit 0; $(DESTDIR)$(sbindir)/vlad rehook || true

uninstall-hook:
	rm -rf $(DESTDIR)$(INSTALLCONFIGDIR)/system
	rm -rf $(DESTDIR)$(localstatedir)/vera.priv
	rm -rf $(DESTDIR)$(localstatedir)/vera.pub
	rm -rf $(DESTDIR)$(SWITCHLOGDIR)
	rm -rf $(DESTDIR)$(sysconfdir)/vera

uninstall-local:
	rm -f  $(DESTDIR)$(RUNLEVELCONFIG)
	rm -f  $(DESTDIR)$(sbindir)/vlad

check-am:
	$(VALGRIND) ./testdepcomputations
	$(VALGRIND) ./testcontroller
	./testcontroller sysdown
	export VALGRIND='$(VALGRIND)'; sh $(srcdir)/testprocloader.sh
	$(VALGRIND) ./testpoller
	./testcontroller2
	$(VALGRIND) ./testcontroller2 testreexec_nofork
	./testcontroller2 testreexec_do '$(VALGRIND) ./testcontroller2'
	./testcontroller3 respawn1 "$(VALGRIND)"
	./testcontroller3 respawn2
	./testcontroller3 respawn3 "$(VALGRIND)"
	./testcontroller3 respawn4
	./testcontroller4
	$(VALGRIND) ./testcontroller3 testnoreexec
	export VALGRIND='$(VALGRIND)'; \
		export srcdir=$(srcdir); \
		sh $(srcdir)/testinittab.sh
	./testhook
	./testswitchlog
	srcdir="$(srcdir)"; export srcdir; $(SHELL) $(srcdir)/testlogrotate.sh

man8_MANS=vera.8 vlad.8 vera.extras.8 vera.logs.8 vera.runlevels.8 vera.units.8

EXTRA_DIST += init.8 vlad.8 vera.8.in \
	vera.extras.8.in \
	vera.logs.8.in \
	vera.runlevels.8.in \
	vera.units.8.in

MANEXPAND = \
	set -vx; sed 's~@''INSTALLCONFIGDIR@''~$(INSTALLCONFIGDIR)~g;   \
	     s~@''LOCALCONFIGDIR@''~$(LOCALCONFIGDIR)~g;                \
	     s~@''OVERRIDECONFIGDIR@''~$(OVERRIDECONFIGDIR)~g;          \
	     s~@''RUNLEVELCONFIG@''~$(RUNLEVELCONFIG)~g;                \
	     s~@''ENVIRONCONFIG@''~$(ENVIRONCONFIG)~g;                  \
	     s~@''CONFIGDIR@''~$(CONFIGDIR)~g'

vera.8: vera.8.in Makefile
	$(MANEXPAND) <$(srcdir)/vera.8.in >vera.8.tmp
	mv -f vera.8.tmp vera.8

vera.extras.8: vera.extras.8.in Makefile
	$(MANEXPAND) <$(srcdir)/vera.extras.8.in >vera.extras.8.tmp
	mv -f vera.extras.8.tmp vera.extras.8

vera.logs.8: vera.logs.8.in Makefile
	$(MANEXPAND) <$(srcdir)/vera.logs.8.in >vera.logs.8.tmp
	mv -f vera.logs.8.tmp vera.logs.8

vera.runlevels.8: vera.runlevels.8.in Makefile
	$(MANEXPAND) <$(srcdir)/vera.runlevels.8.in >vera.runlevels.8.tmp
	mv -f vera.runlevels.8.tmp vera.runlevels.8

vera.units.8: vera.units.8.in Makefile
	$(MANEXPAND) <$(srcdir)/vera.units.8.in >vera.units.8.tmp
	mv -f vera.units.8.tmp vera.units.8

CLEANFILES += vera.8 vera.extras.8 vera.logs.8 vera.runlevels.8 vera.units.8

EXTRA_DIST += INSTALL
BUILT_SOURCES += INSTALL

if HAVE_DOCS
init.8: vera.man.stamp
	touch init.8

vera.8.in: vera.man.stamp
	touch vera.8.in

vera.extras.8.in: vera.man.stamp
	touch vera.extras.8.in

vera.logs.8.in: vera.man.stamp
	touch vera.logs.8.in

vera.runlevels.8.in: vera.man.stamp
	touch vera.runlevels.8.in

vera.units.8.in: vera.man.stamp
	touch vera.units.8.in

vlad.8: vera.man.stamp
	touch vlad.8

vera.man.stamp: vera.xml
	rm -rf veraman.tmp
	mkdir -p veraman.tmp
	xsltproc  --nonet -o veraman.tmp/ http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $(srcdir)/vera.xml
	mv veraman.tmp/vera.8 veraman.tmp/vera.8.in
	mv veraman.tmp/vera.extras.8 veraman.tmp/vera.extras.8.in
	mv veraman.tmp/vera.logs.8 veraman.tmp/vera.logs.8.in
	mv veraman.tmp/vera.runlevels.8 veraman.tmp/vera.runlevels.8.in
	mv veraman.tmp/vera.units.8 veraman.tmp/vera.units.8.in
	mv veraman.tmp/* .
	rm -rf veraman.tmp
	touch vera.man.stamp

CLEANFILES += init.8 vera.8.in \
	vera.extras.8 vera.logs.8 vera.runlevels.8.in vera.units.8.in \
	vlad.8 vera.man.stamp

INSTALL: vera.html.stamp
	touch INSTALL

vera.html.stamp: vera.xml
	rm -rf html.tmp
	xsltproc --nonet -o html.tmp/ \
	   --stringparam chunk.first.sections 1 \
	   --stringparam use.id.as.filename 1 \
           http://docbook.sourceforge.net/release/xsl/current/xhtml/chunk.xsl \
           vera.xml
	$(HTML2TXT) <html.tmp/INSTALL.html >INSTALL.tmp
	mv INSTALL.tmp INSTALL
	rm -rf html.tmp
	touch vera.html.stamp

CLEANFILES += INSTALL
endif

# Make a Slackware package

NVR:=$(PACKAGE)-$(VERSION)-$(shell uname -m)-1

slackpkg: dist
	sudo rm -rf $(PACKAGE)-$(VERSION)
	tar xf $(PACKAGE)-$(VERSION).tar.xz
	cd $(PACKAGE)-$(VERSION) && \
		./configure --prefix=/usr --sysconfdir=/etc --sbindir=/sbin \
			--libdir=/lib --localstatedir=/run \
			--with-switchlogdir=/var/log/vera \
			--localedir=/usr/share/locale && \
		make -j `grep '^processor' /proc/cpuinfo | wc -l` && \
                strip vera && strip vera-init
	sudo $(MAKE) slackpkg-sudo
	cp $(NVR).txz $(NVR).txz.tmp
	mv -f $(NVR).txz.tmp $(NVR).txz

slackpkg-sudo:
	cd $(PACKAGE)-$(VERSION) && \
		$(MAKE) install DESTDIR=inst && \
		cd inst && \
                   find lib/vera/system/inittab \
                        lib/vera/system/rc \
			lib/vera/system/rc.M* -type f -print | xargs rm -f && \
                   mkdir install && \
			cp ../../packaging/slackware/doinst.sh \
						install/doinst.sh && \
			chmod +x install/doinst.sh && \
			cp ../../slack-desc install && \
                   gzip usr/share/man/*/* && \
                   mkdir -p var/lib/pkgtools/douninst.sh && \
		   cp ../../packaging/slackware/uninst.sh \
                        var/lib/pkgtools/douninst.sh/$(NVR) && \
                        chmod +x var/lib/pkgtools/douninst.sh/$(NVR) && \
                   /sbin/makepkg -c y -l y \
     ../../$(NVR).txz
	rm -rf $(PACKAGE)-$(VERSION)

RELEASETAG=$(VERSION)

release:
	scp slackware:src/vera/$(PACKAGE)-$(VERSION).tar.xz .
	scp slackware:src/vera/$(PACKAGE)-$(VERSION)-*.txz .
	gpg --detach-sign --default-key mrsam@courier-mta.com $(PACKAGE)-$(VERSION).tar.xz
	gpg --detach-sign --default-key mrsam@courier-mta.com $(PACKAGE)-$(VERSION)-*.txz
	-git tag $(RELEASETAG)
	git push --tags -f
	git push --tags -f github
	gh release create $(RELEASETAG) \
		-t "Release $(VERSION)" \
		-n '' \
		--latest \
		'$(PACKAGE)-$(VERSION).tar.xz#$(PACKAGE)-$(VERSION).tar.xz: source code' \
		'$(PACKAGE)-$(VERSION).tar.xz.sig#$(PACKAGE)-$(VERSION).tar.xz.sig: source code signature' \
		"`echo $(PACKAGE)-$(VERSION)-*.txz`"'#'"`echo $(PACKAGE)-$(VERSION)-*.txz`"': slackware64 package' \
		"`echo $(PACKAGE)-$(VERSION)-*.txz.sig`"'#'"`echo $(PACKAGE)-$(VERSION)-*.txz.sig`"': slackware64 package signature'
