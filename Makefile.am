SUBDIRS = po

ACLOCAL_AMFLAGS = -I m4
AM_CXXFLAGS = -std=c++20 @YAMLCPPFLAGS@

export LC_ALL=C

noinst_PROGRAMS=\
	testcontroller						\
	testcontroller2						\
	testcontroller2fdleak					\
	testdepcomputations 					\
	testpoller						\
	testprocloader
EXTRA_DIST=testprocloader.sh

noinst_LIBRARIES=libvera.a

libvera_a_SOURCES=						\
	current_containers_infofwd.H				\
	current_containers_info.H				\
	external_filedesc.H					\
	log.C 							\
	log.H							\
	log_current_time.C					\
	messages.H 						\
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

check:
	$(VALGRIND) ./testdepcomputations
	$(VALGRIND) ./testcontroller
	export VALGRIND='$(VALGRIND)'; sh $(srcdir)/testprocloader.sh
	$(VALGRIND) ./testpoller
	./testcontroller2
	$(VALGRIND) ./testcontroller2 testreexec_nofork
	./testcontroller2 testreexec_do '$(VALGRIND) ./testcontroller2'
