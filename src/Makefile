
ifneq ($(__LINUX__), y)
override MEDUSA_EXEC_ENABLE		= n
override MEDUSA_POLL_EPOLL_ENABLE   	= n
override MEDUSA_SIGNAL_SIGNALFD_ENABLE	= n
override MEDUSA_TIMER_TIMERFD_ENABLE  	= n
endif

ifneq ($(__DARWIN__), y)
override MEDUSA_POLL_KQUEUE_ENABLE    	= n
endif

ifeq ($(__WINDOWS__), y)
override MEDUSA_EXEC_ENABLE		= n
override MEDUSA_POLL_POLL_ENABLE   	= n
override MEDUSA_SIGNAL_SIGACTION_ENABLE	= n
override MEDUSA_SIGNAL_SIGNALFD_ENABLE	= n
override MEDUSA_LIBMEDUSA_TARGET_O	= n
override MEDUSA_LIBMEDUSA_TARGET_SO	= n
endif

target.a-${MEDUSA_LIBMEDUSA_TARGET_A} = \
	libmedusa.a \

target.o-${MEDUSA_LIBMEDUSA_TARGET_O} = \
	libmedusa.o \

target.so-${MEDUSA_LIBMEDUSA_TARGET_SO} = \
	libmedusa.so.${MEDUSA_SONAME} \

libmedusa.a_cflags-y = \
	-fvisibility=hidden

libmedusa.a_files-y = \
	strndup.c \
	debug.c \
	pipe.c \
	clock.c \
	base64.c \
	sha1.c \
	pool.c \
	buffer.c \
	buffer-simple.c \
	buffer-ring.c \
	pqueue.c \
	condition.c \
	io.c \
	signal.c \
	timer.c \
	url.c \
	monitor.c \
	version.c

libmedusa.a_cflags-${MEDUSA_EXEC_ENABLE} += \
	-DMEDUSA_EXEC_ENABLE=1
libmedusa.a_files-${MEDUSA_EXEC_ENABLE} += \
	exec.c

libmedusa.a_cflags-${MEDUSA_POLL_EPOLL_ENABLE} += \
	-DMEDUSA_POLL_EPOLL_ENABLE=1
libmedusa.a_files-${MEDUSA_POLL_EPOLL_ENABLE} += \
	poll-epoll.c

libmedusa.a_cflags-${MEDUSA_POLL_KQUEUE_ENABLE} += \
	-DMEDUSA_POLL_KQUEUE_ENABLE=1
libmedusa.a_files-${MEDUSA_POLL_KQUEUE_ENABLE} += \
	poll-kqueue.c

libmedusa.a_cflags-${MEDUSA_POLL_POLL_ENABLE} += \
	-DMEDUSA_POLL_POLL_ENABLE=1
libmedusa.a_files-${MEDUSA_POLL_POLL_ENABLE} += \
	poll-poll.c

libmedusa.a_cflags-${MEDUSA_POLL_SELECT_ENABLE} += \
	-DMEDUSA_POLL_SELECT_ENABLE=1
libmedusa.a_files-${MEDUSA_POLL_SELECT_ENABLE} += \
	poll-select.c

libmedusa.a_cflags-${MEDUSA_TIMER_TIMERFD_ENABLE} += \
	-DMEDUSA_TIMER_TIMERFD_ENABLE=1
libmedusa.a_files-${MEDUSA_TIMER_TIMERFD_ENABLE} += \
	timer-timerfd.c

libmedusa.a_cflags-${MEDUSA_TIMER_MONOTONIC_ENABLE} += \
	-DMEDUSA_TIMER_MONOTONIC_ENABLE=1
libmedusa.a_files-${MEDUSA_TIMER_MONOTONIC_ENABLE} += \
	timer-monotonic.c

libmedusa.a_cflags-${MEDUSA_SIGNAL_SIGACTION_ENABLE} += \
	-DMEDUSA_SIGNAL_SIGACTION_ENABLE=1
libmedusa.a_files-${MEDUSA_SIGNAL_SIGACTION_ENABLE} += \
	signal-sigaction.c

libmedusa.a_cflags-${MEDUSA_SIGNAL_SIGNALFD_ENABLE} += \
	-DMEDUSA_SIGNAL_SIGNALFD_ENABLE=1
libmedusa.a_files-${MEDUSA_SIGNAL_SIGNALFD_ENABLE} += \
	signal-signalfd.c

libmedusa.a_cflags-${MEDUSA_SIGNAL_NULL_ENABLE} += \
	-DMEDUSA_SIGNAL_NULL_ENABLE=1
libmedusa.a_files-${MEDUSA_SIGNAL_NULL_ENABLE} += \
	signal-null.c

libmedusa.a_cflags-${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} += \
	-DMEDUSA_TCPSOCKET_OPENSSL_ENABLE=1

libmedusa.a_tcpsocket.c_cflags-${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} += \
	-Wno-error=deprecated-declarations

libmedusa.a_files-y += \
	tcpsocket.c \
	udpsocket.c

libmedusa.a_files-y += \
	httprequest.c \
	../3rdparty/http-parser/http_parser.c

libmedusa.a_files-y += \
	dnsresolver.c \
	dnsrequest.c \
	../3rdparty/SPCDNS/src/codec.c \
	../3rdparty/SPCDNS/src/mappings.c \
	../3rdparty/SPCDNS/src/output.c

libmedusa.a_../3rdparty/SPCDNS/src/codec.c_cflags-y += \
	-DNDEBUG

libmedusa.o_../3rdparty/SPCDNS/src/codec.c_cflags-y += \
	-DNDEBUG

libmedusa.so.${MEDUSA_SONAME}_../3rdparty/SPCDNS/src/codec.c_cflags-y += \
	-DNDEBUG

libmedusa.a_tcpsocket.c_cflags-$(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) += \
	-Wno-deprecated-declarations

libmedusa.o_tcpsocket.c_cflags-$(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) += \
	-Wno-deprecated-declarations

libmedusa.so.${MEDUSA_SONAME}_tcpsocket.c_cflags-$(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) += \
	-Wno-deprecated-declarations

libmedusa.a_files-y += \
	httpserver.c \
	../3rdparty/http-parser/http_parser.c

libmedusa.a_files-y += \
	websocketclient.c \
	websocketserver.c \
	../3rdparty/http-parser/http_parser.c

libmedusa.o_cflags-y = \
	${libmedusa.a_cflags-y}

libmedusa.o_tcpsocket.c_cflags-$(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) += \
	-Wno-deprecated-declarations

libmedusa.o_files-y = \
	${libmedusa.a_files-y}

libmedusa.so.${MEDUSA_SONAME}_cflags-y += \
	${libmedusa.a_cflags-y}

libmedusa.so.${MEDUSA_SONAME}_tcpsocket.c_cflags-$(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) += \
	-Wno-deprecated-declarations

libmedusa.so.${MEDUSA_SONAME}_files-y = \
	${libmedusa.a_files-y}

libmedusa.so.${MEDUSA_SONAME}_ldflags-y = \
	-lpthread \
	-lm

libmedusa.so.${MEDUSA_SONAME}_ldflags-${__WINDOWS__} += \
	-lws2_32

libmedusa.so.${MEDUSA_SONAME}_ldflags-$(__LINUX__) += \
	-lrt

libmedusa.so.${MEDUSA_SONAME}_ldflags-$(MEDUSA_TCPSOCKET_OPENSSL_ENABLE) += \
	-lssl

dist.dir = ../dist
dist.base = medusa

dist.include-y = \
	monitor-private.h \
	subject-struct.h \
	monitor.h \
	clock.h \
	debug.h \
	error.h \
	pool.h \
	iovec.h \
	buffer.h \
	condition.h \
	io.h \
	signal.h \
	timer.h \
	tcpsocket.h \
	udpsocket.h \
	httprequest.h \
	httpserver.h \
	dnsrequest.h \
	dnsresolver.h \
	websocketclient.h \
	websocketserver.h \
	exec.h \
	queue.h \
	queue_sys.h \
	url.h \
	sha1.h \
	base64.h \
	strndup.h \
	version.h

dist.obj-${MEDUSA_LIBMEDUSA_TARGET_O} += \
	libmedusa.o

dist.lib-${MEDUSA_LIBMEDUSA_TARGET_A} += \
	libmedusa.a

dist.lib-${MEDUSA_LIBMEDUSA_TARGET_SO} += \
	libmedusa.so.${MEDUSA_SONAME}

include ../3rdparty/libmakefile/Makefile.lib
