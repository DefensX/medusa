
target-y = \
	medusa-http-server

medusa-http-server_includes-y = \
	../../dist/include

medusa-http-server_libraries-y = \
	../../dist/lib

medusa-http-server_files-y = \
	http-server.c

medusa-http-server_ldflags-y = \
	../../dist/lib/libmedusa.a

medusa-http-server_ldflags-y += \
	-lpthread \
	-lm

medusa-http-server_ldflags-${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} += \
	-lssl \
	-lcrypto

medusa-http-server_ldflags-${__WINDOWS__} += \
	-lws2_32 \
	-lcrypt32 \
	-static

medusa-http-server_ldflags-$(__LINUX__) += \
	-lrt

medusa-http-server_depends-y = \
	../../dist/lib/libmedusa.a

dist.dir = ../../dist

dist.bin-y = \
	medusa-http-server

include ../../3rdparty/libmakefile/Makefile.lib
