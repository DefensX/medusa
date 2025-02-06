
target-y = \
	medusa-http-request

medusa-http-request_includes-y = \
	../../3rdparty/http-parser \
	../../dist/include

medusa-http-request_libraries-y = \
	../../dist/lib

medusa-http-request_files-y = \
	../../3rdparty/http-parser/http_parser.c \
	http-request.c

medusa-http-request_ldflags-y = \
	../../dist/lib/libmedusa.a

medusa-http-request_ldflags-y += \
	-lpthread \
	-lm

medusa-http-request_ldflags-${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} += \
	-lssl \
	-lcrypto

medusa-http-request_ldflags-${__WINDOWS__} += \
	-lws2_32 \
	-lcrypt32 \
	-static

medusa-http-request_ldflags-$(__LINUX__) += \
	-lrt

medusa-http-request_depends-y = \
	../../dist/lib/libmedusa.a

dist.dir = ../../dist

dist.bin-y = \
	medusa-http-request

include ../../3rdparty/libmakefile/Makefile.lib
