target-y = \
	medusa-echo-client

medusa-echo-client_includes-y = \
	../../dist/include

medusa-echo-client_libraries-y = \
	../../dist/lib

medusa-echo-client_files-y = \
	echo-client.c

medusa-echo-client_ldflags-y = \
	../../dist/lib/libmedusa.a

medusa-echo-client_ldflags-y += \
	-lpthread \
	-lm

medusa-echo-client_ldflags-${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} += \
	-lssl \
	-lcrypto

medusa-echo-client_ldflags-${__WINDOWS__} += \
	-lws2_32 \
	-lcrypt32 \
	-static

medusa-echo-client_ldflags-$(__LINUX__) += \
	-lrt

medusa-echo-client_depends-y = \
	../../dist/lib/libmedusa.a

dist.dir = ../../dist

dist.bin-y = \
	medusa-echo-client

include ../../3rdparty/libmakefile/Makefile.lib
