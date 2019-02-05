lib_LTLIBRARIES = libgx.la

GX_SOURCES =      	\
    object.cpp          \
    singleton.cpp       \
    bitorder.cpp        \
    page.cpp            \
    printf.cpp          \
    path.cpp            \
    stream.cpp          \
    log.cpp             \
    obstack.cpp         \
    pool.cpp            \
    allocator.cpp       \
    getopt.cpp          \
    io.cpp              \
    fileloader.cpp      \
    csvloader.cpp       \
    charset.cpp         \
    rbtree.cpp          \
    timeval.cpp         \
    timermanager.cpp    \
    script.cpp          \
    utils.cpp           \
    servlet.cpp         \
    network.cpp         \
    socket.cpp          \
    reactor.cpp         \
    peer.cpp            \
    protocol.cpp        \
    coroutine.cpp       \
    context.cpp         \
    filesystem.cpp      \
    filemonitor.cpp     \
    application.cpp     \
    mysql.cpp           \
    prob.cpp            \
    cron.cpp

libgx_la_SOURCES = $(GX_SOURCES)
libgx_la_CXXFLAGS = -std=c++11 -O2 -Wall -Wl,-E -I/usr/include/lua5.1
libgx_la_LDFLAGS = -llua5.1 -lmysqlclient

