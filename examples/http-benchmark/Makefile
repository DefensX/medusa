
target-y = \
	medusa-http-benchmark

medusa-http-benchmark_includes-y = \
	../../3rdparty/http-parser \
	../../dist/include

medusa-http-benchmark_libraries-y = \
	../../dist/lib

medusa-http-benchmark_files-y = \
	../../3rdparty/http-parser/http_parser.c \
	http-benchmark.c

medusa-http-benchmark_ldflags-y = \
	../../dist/lib/libmedusa.a

medusa-http-benchmark_ldflags-y += \
	-lpthread \
	-lm

medusa-http-benchmark_ldflags-${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} += \
	-lssl \
	-lcrypto

medusa-http-benchmark_ldflags-${__WINDOWS__} += \
	-lws2_32 \
	-lcrypt32 \
	-static

medusa-http-benchmark_ldflags-$(__LINUX__) += \
	-lrt

medusa-http-benchmark_depends-y = \
	../../dist/lib/libmedusa.a

dist.dir = ../../dist

dist.bin-y = \
	medusa-http-benchmark

include ../../3rdparty/libmakefile/Makefile.lib
