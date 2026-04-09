# medusa #

1. <a href="#1-overview">overview</a>
2. <a href="#2-download">download</a>
3. <a href="#3-build">build</a>
3. <a href="#4-benchmark">benchmark</a>

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
    make
    make tests

### 3.2. mingw ###

    MEDUSA_BUILD_EXAMPLES=y \
    CROSS_COMPILE_PREFIX=x86_64-w64-mingw32- \
    CFLAGS="-DWINVER=_WIN32_WINNT_WIN10 -D_WIN32_WINNT=_WIN32_WINNT_WIN10" \
    MEDUSA_TCPSOCKET_OPENSSL_ENABLE=n \
    make

## 4. benchmark

C connections to URL, each connection sends N requests with interval I
milliseconds between requests using keep-alive K feature.

    medusa-server-benchmark -c C -n N -i I -k K -v 0 URL
