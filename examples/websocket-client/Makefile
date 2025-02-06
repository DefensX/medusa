
target-y = \
	medusa-websocket-client

medusa-websocket-client_includes-y = \
	../../dist/include

medusa-websocket-client_libraries-y = \
	../../dist/lib

medusa-websocket-client_files-y = \
	websocket-client.c

medusa-websocket-client_ldflags-y = \
	../../dist/lib/libmedusa.a

medusa-websocket-client_ldflags-y += \
	-lpthread \
	-lm

medusa-websocket-client_ldflags-${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} += \
	-lssl \
	-lcrypto

medusa-websocket-client_ldflags-${__WINDOWS__} += \
	-lws2_32 \
	-lcrypt32 \
	-static

medusa-websocket-client_ldflags-$(__LINUX__) += \
	-lrt

medusa-websocket-client_depends-y = \
	../../dist/lib/libmedusa.a

dist.dir = ../../dist

dist.bin-y = \
	medusa-websocket-client

include ../../3rdparty/libmakefile/Makefile.lib
