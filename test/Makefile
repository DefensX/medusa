
$(eval tests = $(sort $(subst .c,,$(wildcard *-??.c))))

target-y = \
	${tests}

$(eval tests_event = $(sort $(subst .c,,$(wildcard *-??-event.c))))

target-n += \
	${tests_event}

all:

define test-defaults
	$1_files-y = \
		$1.c

	$1_includes-y = \
		../dist/include

	$1_libraries-y = \
		../dist/lib

	$1_cflags-${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} += \
		-DMEDUSA_TCPSOCKET_OPENSSL_ENABLE=1

	$1_ldflags-y = \
		../dist/lib/libmedusa.a \
		-lpthread \
		-lm

	$1_ldflags-${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} += \
		-lssl \
		-lcrypto

	$1_ldflags-$(__LINUX__) += \
		-lrt

	$1_ldflags-${__WINDOWS__} += \
		-lws2_32 \
		-lcrypt32 \
		-static

	$1_depends-y = \
		../dist/lib/libmedusa.a
endef

define test_event-defaults
	$1_files-y = \
		$1.c

	$1_ldflags-y = \
		-levent \
		-levent_pthreads \
		-lpthread

	$1_ldflags-$(__LINUX__) += \
		-lrt

endef

$(eval $(foreach T,${tests},$(eval $(call test-defaults,$T))))
$(eval $(foreach T,${tests_event},$(eval $(call test_event-defaults,$T))))

include ../3rdparty/libmakefile/Makefile.lib

tests: all
	${Q}echo "running tests";
	${Q}for T in ${tests} ${tests_event}; do \
		printf "  $${T} ..."; \
		./$${T} 2>$${T}.log 1>$${T}.log; \
		retval=$$?; \
		if [ $$retval != 0 ]; then \
			printf " fail (rc: $$retval)"; \
		else \
			printf " success"; \
		fi; \
		printf "\n"; \
	done

clean:
	${Q}${RM} *.out
	${Q}${RM} *.png
	${Q}${RM} *.log
