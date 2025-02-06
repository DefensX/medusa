
MEDUSA_VERSION			?= 1.1.0
MEDUSA_SONAME			?= 1

MEDUSA_BUILD_TEST     		?= n
MEDUSA_BUILD_EXAMPLES 		?= n

MEDUSA_EXEC_ENABLE		?= y

MEDUSA_POLL_EPOLL_ENABLE     	?= y
MEDUSA_POLL_KQUEUE_ENABLE    	?= n
MEDUSA_POLL_POLL_ENABLE      	?= y
MEDUSA_POLL_SELECT_ENABLE    	?= y

MEDUSA_SIGNAL_SIGACTION_ENABLE	?= y
MEDUSA_SIGNAL_SIGNALFD_ENABLE	?= y
MEDUSA_SIGNAL_NULL_ENABLE	?= y

MEDUSA_TCPSOCKET_OPENSSL_ENABLE ?= y

MEDUSA_TIMER_TIMERFD_ENABLE  	?= y
MEDUSA_TIMER_MONOTONIC_ENABLE	?= y

MEDUSA_LIBMEDUSA_TARGET_A	?= y
MEDUSA_LIBMEDUSA_TARGET_O	?= y
MEDUSA_LIBMEDUSA_TARGET_SO	?= y

prefix	?= /usr/local

subdir-y = \
	src

subdir-${MEDUSA_BUILD_TEST} += \
	test

subdir-${MEDUSA_BUILD_EXAMPLES} += \
	examples

src_makeflags-y = \
	MEDUSA_VERSION=${MEDUSA_VERSION} \
	MEDUSA_SONAME=${MEDUSA_SONAME} \
	MEDUSA_LIBMEDUSA_TARGET_A=${MEDUSA_LIBMEDUSA_TARGET_A} \
	MEDUSA_LIBMEDUSA_TARGET_O=${MEDUSA_LIBMEDUSA_TARGET_O} \
	MEDUSA_LIBMEDUSA_TARGET_SO=${MEDUSA_LIBMEDUSA_TARGET_SO} \
	MEDUSA_EXEC_ENABLE=${MEDUSA_EXEC_ENABLE} \
	MEDUSA_POLL_EPOLL_ENABLE=${MEDUSA_POLL_EPOLL_ENABLE} \
	MEDUSA_POLL_KQUEUE_ENABLE=${MEDUSA_POLL_KQUEUE_ENABLE} \
	MEDUSA_POLL_POLL_ENABLE=${MEDUSA_POLL_POLL_ENABLE} \
	MEDUSA_POLL_SELECT_ENABLE=${MEDUSA_POLL_SELECT_ENABLE} \
	MEDUSA_SIGNAL_SIGACTION_ENABLE=${MEDUSA_SIGNAL_SIGACTION_ENABLE} \
	MEDUSA_SIGNAL_SIGNALFD_ENABLE=${MEDUSA_SIGNAL_SIGNALFD_ENABLE} \
	MEDUSA_SIGNAL_NULL_ENABLE=${MEDUSA_SIGNAL_NULL_ENABLE} \
	MEDUSA_TCPSOCKET_OPENSSL_ENABLE=${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} \
	MEDUSA_TIMER_TIMERFD_ENABLE=${MEDUSA_TIMER_TIMERFD_ENABLE} \
	MEDUSA_TIMER_MONOTONIC_ENABLE=${MEDUSA_TIMER_MONOTONIC_ENABLE}

test_depends-y = \
	src

test_makeflags-y = \
	MEDUSA_EXEC_ENABLE=${MEDUSA_EXEC_ENABLE} \
	MEDUSA_POLL_EPOLL_ENABLE=${MEDUSA_POLL_EPOLL_ENABLE} \
	MEDUSA_POLL_KQUEUE_ENABLE=${MEDUSA_POLL_KQUEUE_ENABLE} \
	MEDUSA_POLL_POLL_ENABLE=${MEDUSA_POLL_POLL_ENABLE} \
	MEDUSA_POLL_SELECT_ENABLE=${MEDUSA_POLL_SELECT_ENABLE} \
	MEDUSA_SIGNAL_SIGACTION_ENABLE=${MEDUSA_SIGNAL_SIGACTION_ENABLE} \
	MEDUSA_SIGNAL_SIGNALFD_ENABLE=${MEDUSA_SIGNAL_SIGNALFD_ENABLE} \
	MEDUSA_SIGNAL_NULL_ENABLE=${MEDUSA_SIGNAL_NULL_ENABLE} \
	MEDUSA_TCPSOCKET_OPENSSL_ENABLE=${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} \
	MEDUSA_TIMER_TIMERFD_ENABLE=${MEDUSA_TIMER_TIMERFD_ENABLE} \
	MEDUSA_TIMER_MONOTONIC_ENABLE=${MEDUSA_TIMER_MONOTONIC_ENABLE}

examples_depends-y = \
	src

examples_makeflags-y = \
	MEDUSA_EXEC_ENABLE=${MEDUSA_EXEC_ENABLE} \
	MEDUSA_POLL_EPOLL_ENABLE=${MEDUSA_POLL_EPOLL_ENABLE} \
	MEDUSA_POLL_KQUEUE_ENABLE=${MEDUSA_POLL_KQUEUE_ENABLE} \
	MEDUSA_POLL_POLL_ENABLE=${MEDUSA_POLL_POLL_ENABLE} \
	MEDUSA_POLL_SELECT_ENABLE=${MEDUSA_POLL_SELECT_ENABLE} \
	MEDUSA_SIGNAL_SIGACTION_ENABLE=${MEDUSA_SIGNAL_SIGACTION_ENABLE} \
	MEDUSA_SIGNAL_SIGNALFD_ENABLE=${MEDUSA_SIGNAL_SIGNALFD_ENABLE} \
	MEDUSA_SIGNAL_NULL_ENABLE=${MEDUSA_SIGNAL_NULL_ENABLE} \
	MEDUSA_TCPSOCKET_OPENSSL_ENABLE=${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} \
	MEDUSA_TIMER_TIMERFD_ENABLE=${MEDUSA_TIMER_TIMERFD_ENABLE} \
	MEDUSA_TIMER_MONOTONIC_ENABLE=${MEDUSA_TIMER_MONOTONIC_ENABLE}

include 3rdparty/libmakefile/Makefile.lib

tests: all
	${Q}+${MAKE} ${test_makeflags-y} -C test tests

install: src test
	install -d ${DESTDIR}/${prefix}/include/medusa
	install -m 0644 dist/include/medusa/buffer.h ${DESTDIR}/${prefix}/include/medusa/buffer.h
	install -m 0644 dist/include/medusa/clock.h ${DESTDIR}/${prefix}/include/medusa/clock.h
	install -m 0644 dist/include/medusa/debug.h ${DESTDIR}/${prefix}/include/medusa/debug.h
	install -m 0644 dist/include/medusa/error.h ${DESTDIR}/${prefix}/include/medusa/error.h
	install -m 0644 dist/include/medusa/exec.h ${DESTDIR}/${prefix}/include/medusa/exec.h
	install -m 0644 dist/include/medusa/httprequest.h ${DESTDIR}/${prefix}/include/medusa/httprequest.h
	install -m 0644 dist/include/medusa/httpserver.h ${DESTDIR}/${prefix}/include/medusa/httpserver.h
	install -m 0644 dist/include/medusa/io.h ${DESTDIR}/${prefix}/include/medusa/io.h
	install -m 0644 dist/include/medusa/iovec.h ${DESTDIR}/${prefix}/include/medusa/iovec.h
	install -m 0644 dist/include/medusa/monitor.h ${DESTDIR}/${prefix}/include/medusa/monitor.h
	install -m 0644 dist/include/medusa/pool.h ${DESTDIR}/${prefix}/include/medusa/pool.h
	install -m 0644 dist/include/medusa/strndup.h ${DESTDIR}/${prefix}/include/medusa/strndup.h
	install -m 0644 dist/include/medusa/queue.h ${DESTDIR}/${prefix}/include/medusa/queue.h
	install -m 0644 dist/include/medusa/queue_sys.h ${DESTDIR}/${prefix}/include/medusa/queue_sys.h
	install -m 0644 dist/include/medusa/signal.h ${DESTDIR}/${prefix}/include/medusa/signal.h
	install -m 0644 dist/include/medusa/condition.h ${DESTDIR}/${prefix}/include/medusa/condition.h
	install -m 0644 dist/include/medusa/tcpsocket.h ${DESTDIR}/${prefix}/include/medusa/tcpsocket.h
	install -m 0644 dist/include/medusa/udpsocket.h ${DESTDIR}/${prefix}/include/medusa/udpsocket.h
	install -m 0644 dist/include/medusa/timer.h ${DESTDIR}/${prefix}/include/medusa/timer.h
	install -m 0644 dist/include/medusa/dnsrequest.h ${DESTDIR}/${prefix}/include/medusa/dnsrequest.h
	install -m 0644 dist/include/medusa/dnsresolver.h ${DESTDIR}/${prefix}/include/medusa/dnsresolver.h
	install -m 0644 dist/include/medusa/websocketclient.h ${DESTDIR}/${prefix}/include/medusa/websocketclient.h
	install -m 0644 dist/include/medusa/websocketserver.h ${DESTDIR}/${prefix}/include/medusa/websocketserver.h
	install -m 0644 dist/include/medusa/url.h ${DESTDIR}/${prefix}/include/medusa/url.h
	install -m 0644 dist/include/medusa/monitor-private.h ${DESTDIR}/${prefix}/include/medusa/monitor-private.h
	install -m 0644 dist/include/medusa/subject-struct.h ${DESTDIR}/${prefix}/include/medusa/subject-struct.h

ifeq (${MEDUSA_LIBMEDUSA_TARGET_SO}, y)
	install -d ${DESTDIR}/${prefix}/lib
	install -m 0755 dist/lib/libmedusa.so.${MEDUSA_SONAME} ${DESTDIR}/${prefix}/lib/
	ln -sf libmedusa.so.${MEDUSA_SONAME} ${DESTDIR}/${prefix}/lib/libmedusa.so.${MEDUSA_VERSION}
endif
ifeq (${MEDUSA_LIBMEDUSA_TARGET_A}, y)
	install -d ${DESTDIR}/${prefix}/lib
	install -m 0644 dist/lib/libmedusa.a ${DESTDIR}/${prefix}/lib/
endif

ifeq (${MEDUSA_BUILD_EXAMPLES}, y)
	install -d ${DESTDIR}/${prefix}/bin
	install -m 0755 dist/bin/medusa-dns-lookup ${DESTDIR}/${prefix}/bin/medusa-dns-lookup
	install -m 0755 dist/bin/medusa-echo-client ${DESTDIR}/${prefix}/bin/medusa-echo-client
	install -m 0755 dist/bin/medusa-echo-server ${DESTDIR}/${prefix}/bin/medusa-echo-server
	install -m 0755 dist/bin/medusa-http-benchmark ${DESTDIR}/${prefix}/bin/medusa-http-benchmark
	install -m 0755 dist/bin/medusa-http-request ${DESTDIR}/${prefix}/bin/medusa-http-request
	install -m 0755 dist/bin/medusa-http-server ${DESTDIR}/${prefix}/bin/medusa-http-server
	install -m 0755 dist/bin/medusa-websocket-client ${DESTDIR}/${prefix}/bin/medusa-websocket-client
	install -m 0755 dist/bin/medusa-websocket-server ${DESTDIR}/${prefix}/bin/medusa-websocket-server
endif

	install -d ${DESTDIR}/${prefix}/lib/pkgconfig
	sed 's?'prefix=/usr/local'?'prefix=${DESTDIR}/${prefix}'?g' libmedusa.pc > ${DESTDIR}/${prefix}/lib/pkgconfig/libmedusa.pc

uninstall:
	rm -rf ${DESTDIR}/${prefix}/bin/medusa-*

	rm -rf ${DESTDIR}/${prefix}/include/medusa

	rm -f ${DESTDIR}/${prefix}/lib/libmedusa.so*
	rm -f ${DESTDIR}/${prefix}/lib/libmedusa.a

	rm -f ${DESTDIR}/${prefix}/lib/pkgconfig/libmedusa.pc
