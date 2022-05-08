SUBDIRS = po

ACLOCAL_AMFLAGS = -I m4
AM_CXXFLAGS = -std=c++20

noinst_PROGRAMS=testcontroller testdepcomputations

noinst_LIBRARIES=libvera.a

libvera_a_SOURCES=						\
	log.C 							\
	log.H							\
	log_current_time.C					\
	messages.H 						\
	proc_container.C					\
	proc_container.H					\
	proc_containerfwd.H					\
	proc_container_runner.C					\
	proc_container_runner.H					\
	proc_container_runnerfwd.H				\
	proc_container_timer.C					\
	proc_container_timer.H					\
	proc_container_timerfwd.H				\
	unit_test.H

testcontroller_SOURCES=testcontroller.C
testcontroller_LDADD=libvera.a

testdepcomputations_SOURCES=testdepcomputations.C
testdepcomputations_LDADD=libvera.a

check:
	$(VALGRIND) ./testdepcomputations
	$(VALGRIND) ./testcontroller
