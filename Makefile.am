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
	log.C 							\
	log.H							\
	log_current_time.C					\
	messages.H 						\
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
	proc_container_timer.C					\
	proc_container_timer.H					\
	proc_container_timerfwd.H				\
	proc_loader.C						\
	proc_loader.H						\
	poller.C						\
	poller.H						\
	unit_test.H

configdirs.h: config.status
	echo '#define INSTALLCONFIGDIR "$(INSTALLCONFIGDIR)"' >configdirs.h.tmp
	echo '#define CONFIGDIR "$(CONFIGDIR)"' >>configdirs.h.tmp
	echo '#define RUNLEVELCONFIG "$(RUNLEVELCONFIG)"' >>configdirs.h.tmp
	echo '#define LOCALCONFIGDIR "$(LOCALCONFIGDIR)"' >>configdirs.h.tmp
	echo '#define OVERRIDECONFIGDIR "$(OVERRIDECONFIGDIR)"' >>configdirs.h.tmp
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
	$(MKDIR_P) $(DESTDIR)$(INSTALLCONFIGDIR)
	$(MKDIR_P) $(DESTDIR)$(CONFIGDIR)
	$(MKDIR_P) $(DESTDIR)$(LOCALCONFIGDIR)
	$(MKDIR_P) $(DESTDIR)$(OVERRIDECONFIGDIR)
	chmod 700 $(DESTDIR)$(INSTALLCONFIGDIR)
	chmod 700 $(DESTDIR)$(CONFIGDIR)
	chmod 700 $(DESTDIR)$(LOCALCONFIGDIR)
	chmod 700 $(DESTDIR)$(OVERRIDECONFIGDIR)
	umask 022; ./testprocloader genrunlevels $(DESTDIR)$(RUNLEVELCONFIG)

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
