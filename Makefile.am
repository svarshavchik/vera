SUBDIRS = po

ACLOCAL_AMFLAGS = -I m4
AM_CXXFLAGS = -std=c++20 @YAMLCPPFLAGS@

export LC_ALL=C

sbin_PROGRAMS=vera
noinst_SCRIPTS=vlad

noinst_PROGRAMS=\
	testcontroller						\
	testcontroller2						\
	testcontroller2fdleak					\
	testcontroller3						\
	testdepcomputations 					\
	testpoller						\
	testprocloader
EXTRA_DIST=testprocloader.sh

BUILT_SOURCES=configdirs.h
CLEANFILES=configdirs.h vlad

noinst_LIBRARIES=libvera.a

libvera_a_SOURCES=						\
	current_containers_infofwd.H				\
	current_containers_info.H				\
	external_filedesc.H					\
	external_filedesc.C					\
	external_filedesc_privcmdsocket.H			\
	external_filedesc_privcmdsocket.C			\
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
	proc_containerfwd.H					\
	proc_container_group.C					\
	proc_container_group.H					\
	proc_container_runner.C					\
	proc_container_runner.H					\
	proc_container_runnerfwd.H				\
	proc_container_run_info.H				\
	proc_container_state.H					\
	proc_container_statefwd.H				\
	proc_container_timer.C					\
	proc_container_timer.H					\
	proc_container_timerfwd.H				\
	proc_loader.C						\
	proc_loader.H						\
	unit_test.H						\
	yaml_writer.C						\
	yaml-writer.H

configdirs.h: config.status
	echo '#define INSTALLCONFIGDIR "$(INSTALLCONFIGDIR)"' >configdirs.h.tmp
	echo '#define CONFIGDIR "$(CONFIGDIR)"' >>configdirs.h.tmp
	echo '#define RUNLEVELCONFIG "$(RUNLEVELCONFIG)"' >>configdirs.h.tmp
	echo '#define LOCALCONFIGDIR "$(LOCALCONFIGDIR)"' >>configdirs.h.tmp
	echo '#define OVERRIDECONFIGDIR "$(OVERRIDECONFIGDIR)"' >>configdirs.h.tmp
	echo '#define LOCALSTATEDIR "$(localstatedir)"' >>configdirs.h.tmp
	echo '#define LOCALEDIR "$(localedir)"' >> configdirs.h.tmp
	mv configdirs.h.tmp configdirs.h

vera_SOURCES=vera.C
vera_LDADD=libvera.a @YAMLLIBS@

vlad: vera
	rm -f vlad; ln vera vlad

testcontroller_SOURCES=testcontroller.C unit_test.C
testcontroller_LDADD=libvera.a @YAMLLIBS@

testcontroller2_SOURCES=testcontroller2.C unit_test.C
testcontroller2_LDADD=libvera.a @YAMLLIBS@

testcontroller2fdleak_SOURCES=testcontroller2fdleak.C

testcontroller3_SOURCES=testcontroller3.C unit_test.C
testcontroller3_LDADD=libvera.a @YAMLLIBS@

testdepcomputations_SOURCES=testdepcomputations.C unit_test.C
testdepcomputations_LDADD=libvera.a @YAMLLIBS@

testprocloader_SOURCES=testprocloader.C unit_test.C
testprocloader_LDADD=libvera.a @YAMLLIBS@

testpoller_SOURCES=testpoller.C unit_test.C
testpoller_LDADD=libvera.a @YAMLLIBS@

install-exec-hook:
	rm -f $(DESTDIR)$(sbindir)/vlad
	ln $(DESTDIR)$(sbindir)/vera $(DESTDIR)$(sbindir)/vlad

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(INSTALLCONFIGDIR)/system
	$(MKDIR_P) $(DESTDIR)$(CONFIGDIR)
	$(MKDIR_P) $(DESTDIR)$(localstatedir)
	$(MKDIR_P) $(DESTDIR)$(LOCALCONFIGDIR)
	$(MKDIR_P) $(DESTDIR)$(OVERRIDECONFIGDIR)
	chmod 755 $(DESTDIR)$(INSTALLCONFIGDIR)
	chmod 755 $(DESTDIR)$(CONFIGDIR)
	chmod 755 $(DESTDIR)$(LOCALCONFIGDIR)
	chmod 755 $(DESTDIR)$(OVERRIDECONFIGDIR)
	umask 022; ./testprocloader genrunlevels $(DESTDIR)$(RUNLEVELCONFIG) $(DESTDIR)$(INSTALLCONFIGDIR)/system

uninstall-local:
	rm -f  $(DESTDIR)$(RUNLEVELCONFIG)
	rm -f  $(DESTDIR)$(sbindir)/vlad

check-am:
	$(VALGRIND) ./testdepcomputations
	$(VALGRIND) ./testcontroller
	export VALGRIND='$(VALGRIND)'; sh $(srcdir)/testprocloader.sh
	$(VALGRIND) ./testpoller
	./testcontroller2
	$(VALGRIND) ./testcontroller2 testreexec_nofork
	./testcontroller2 testreexec_do '$(VALGRIND) ./testcontroller2'
	./testcontroller3 respawn1 "$(VALGRIND)"
	./testcontroller3 respawn2
	./testcontroller3 respawn3 "$(VALGRIND)"
	./testcontroller3 respawn4
	$(VALGRIND) ./testcontroller3 testnoreexec

man8_MANS=vera.8 vlad.8

EXTRA_DIST += init.8 vlad.8 vera.8.in

vera.8: vera.8.in Makefile
	set -vx; sed 's~@''INSTALLCONFIGDIR@''~$(INSTALLCONFIGDIR)~;   # \
	     s~@''LOCALCONFIGDIR@''~$(LOCALCONFIGDIR)~;                # \
	     s~@''OVERRIDECONFIGDIR@''~$(OVERRIDECONFIGDIR)~;          # \
	     s~@''RUNLEVELCONFIG@''~$(RUNLEVELCONFIG)~'                  \
					<$(srcdir)/vera.8.in >vera.8.tmp
	mv -f vera.8.tmp vera.8

CLEANFILES += vera.8

EXTRA_DIST += INSTALL
BUILT_SOURCES += INSTALL

if HAVE_DOCS
init.8: vera.man.stamp
	touch init.8

vera.8.in: vera.man.stamp
	touch vera.8.in

vlad.8: vera.man.stamp
	touch vlad.8

vera.man.stamp: vera.xml
	rm -rf veraman.tmp
	mkdir -p veraman.tmp
	xsltproc  --nonet -o veraman.tmp/ http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $(srcdir)/vera.xml
	mv veraman.tmp/vera.8 veraman.tmp/vera.8.in
	mv veraman.tmp/* .
	rm -rf veraman.tmp
	touch vera.man.stamp

CLEANFILES += init.8 vera.8.in vlad.8 vera.man.stamp

INSTALL: vera.html.stamp
	touch INSTALL

vera.html.stamp: vera.xml
	rm -rf html.tmp
	xsltproc --nonet -o html.tmp/ \
	   --stringparam chunk.first.sections 1 \
	   --stringparam use.id.as.filename 1 \
           http://docbook.sourceforge.net/release/xsl/current/xhtml/chunk.xsl \
           vera.xml
	elinks -dump -no-references <html.tmp/INSTALL.html >INSTALL.tmp
	mv INSTALL.tmp INSTALL
	rm -rf html.tmp
	touch vera.html.stamp

CLEANFILES += INSTALL
endif
