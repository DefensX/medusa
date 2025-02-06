
target-y = \
	medusa-dns-lookup

medusa-dns-lookup_includes-y = \
	../../dist/include

medusa-dns-lookup_libraries-y = \
	../../dist/lib

medusa-dns-lookup_files-y = \
	dns-lookup.c

medusa-dns-lookup_ldflags-y = \
	../../dist/lib/libmedusa.a

medusa-dns-lookup_ldflags-y += \
	-lpthread \
	-lm

medusa-dns-lookup_ldflags-${MEDUSA_TCPSOCKET_OPENSSL_ENABLE} += \
	-lssl \
	-lcrypto

medusa-dns-lookup_ldflags-${__WINDOWS__} += \
	-lws2_32 \
	-lcrypt32 \
	-static

medusa-dns-lookup_ldflags-$(__LINUX__) += \
	-lrt

medusa-dns-lookup_depends-y = \
	../../dist/lib/libmedusa.a

dist.dir = ../../dist

dist.bin-y = \
	medusa-dns-lookup

include ../../3rdparty/libmakefile/Makefile.lib
