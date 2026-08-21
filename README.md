# medusa #

1. <a href="#1-overview">overview</a>
2. <a href="#2-download">download</a>
3. <a href="#3-build">build</a>
    1. <a href="#31-debian">debian</a>
    2. <a href="#32-mingw">mingw</a>
        1. <a href="#321-openssl">openssl</a>
        2. <a href="#322-medusa">medusa</a>
    3. <a href="#33-darwin">darwin</a>
4. <a href="#4-benchmark">benchmark</a>

## 1. overview ##

Medusa is an asynchronous event notification library. It executes registered callback functions when specific events occur on file descriptors, after timeouts, or in response to signals.

Beyond basic I/O monitoring, Medusa provides a full async networking stack — TCP sockets, UDP sockets, and WebSockets, each supporting both server and client roles within the same event-driven model.

The API surface includes conditional signal events, timers, DNS request and resolver primitives, an executor, HTTP request and HTTP server implementations, and raw I/O operations. Internally, object state changes (modified, deleted, created) are tracked through priority queues and object trees, converging into a single-point execution within the event loop to keep dispatch fast and deterministic.

Multiple platform-native loop backends are supported — epoll, kqueue, poll, select — chosen at build time or runtime. A software signal mechanism handles internal async event propagation between components.

The result is a low-footprint, high-throughput event engine with tendrils reaching into every I/O path — like its namesake, with strands tied to everything, and equally charming.

## 2. download ##

    git clone --recursive https://github.com/SecureIndustries/medusa.git

or

    git clone https://github.com/SecureIndustries/medusa.git
    cd medusa
    git submodule update --init --recursive

## 3. build ##

### 3.1. debian ###

    apt install gcc
    apt install make
    apt install pkg-config

    cd medusa
    MEDUSA_BUILD_EXAMPLES=y \
    MEDUSA_BUILD_TESTS=y \
    make -j 8
    make -j 8 tests

### 3.2. mingw ###

#### 3.2.1. openssl

    OPENSSL=openssl-3.5.7

    mkdir -p 3rdparty/openssl-mingw64
    curl -L https://www.openssl.org/source/$OPENSSL.tar.gz | tar -xz -C 3rdparty/openssl-mingw64

    cd 3rdparty/openssl-mingw64/$OPENSSL
    CC= CROSS_COMPILE=x86_64-w64-mingw32- ./Configure \
        --prefix=/usr/local --libdir=lib \
        no-apps no-idea no-mdc2 no-rc5 no-shared no-tests \
        mingw64
    make -j 8 build_libs
    make -j 8 DESTDIR=../../install_mingw64 install_sw
    cd ../../..

#### 3.2.2. medusa

    CFLAGS="-DWINVER=_WIN32_WINNT_WIN10 -D_WIN32_WINNT=_WIN32_WINNT_WIN10 -I`pwd`/3rdparty/install_mingw64/usr/local/include" \
	LDFLAGS="-L`pwd`/3rdparty/install_mingw64/usr/local/lib" \
    MEDUSA_BUILD_EXAMPLES=y \
    MEDUSA_BUILD_TESTS=y \
    CROSS_COMPILE_PREFIX=x86_64-w64-mingw32- \
    make -j 8

### 3.3. darwin

    OPENSSL=openssl-3.5.7

    mkdir -p 3rdparty/openssl-darwin64
    curl -L https://www.openssl.org/source/$OPENSSL.tar.gz | tar -xz -C 3rdparty/openssl-darwin64

    cd 3rdparty/openssl-darwin64/$OPENSSL
    CC= ./Configure \
        --prefix=/usr/local --libdir=lib \
        no-apps no-idea no-mdc2 no-rc5 no-shared no-tests
    make -j 8 build_libs
    make -j 8 DESTDIR=../../install_darwin64 install_sw
    cd ../../..

    CFLAGS="-I`pwd`/3rdparty/install_darwin64/usr/local/include" \
	LDFLAGS="-L`pwd`/3rdparty/install_darwin64/usr/local/lib" \
    MEDUSA_BUILD_EXAMPLES=y \
    MEDUSA_BUILD_TESTS=y \
    MEDUSA_LIBMEDUSA_TARGET_SO=n \
    make -j 8

## 4. benchmark

C connections to URL, each connection sends N requests with interval I
milliseconds between requests using keep-alive K feature.

    medusa-server-benchmark -c C -n N -i I -k K -v 0 URL
