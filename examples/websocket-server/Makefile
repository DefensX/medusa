
target-y = \
	medusa-websocket-server

medusa-websocket-server_includes-y = \
	../../dist/include

medusa-websocket-server_libraries-y = \
	../../dist/lib

medusa-websocket-server_files-y = \
	websocket-server.c

medusa-websocket-server_ldflags-y = \
	../../dist/lib/libmedusa.a

medusa-websocket-server_ldflags-y += \
	-lpthread \
	-lm

medusa-websocket-server_ldflags-${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} += \
	-lssl \
	-lcrypto

medusa-websocket-server_ldflags-${__WINDOWS__} += \
	-lws2_32 \
	-lcrypt32 \
	-static

medusa-websocket-server_ldflags-$(__LINUX__) += \
	-lrt

medusa-websocket-server_depends-y = \
	../../dist/lib/libmedusa.a

dist.dir = ../../dist

dist.bin-y = \
	medusa-websocket-server

include ../../3rdparty/libmakefile/Makefile.lib
