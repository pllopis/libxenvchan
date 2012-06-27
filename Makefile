#
# tools/libvchan/Makefile
#

XEN_ROOT = $(CURDIR)/../..
include $(XEN_ROOT)/tools/Rules.mk

LIBVCHAN_OBJS = init.o io.o
NODE_OBJS = node.o
NODE2_OBJS = node-select.o

LIBVCHAN_LIBS = $(LDLIBS_libxenstore)
$(LIBVCHAN_OBJS): CFLAGS += $(CFLAGS_libxenstore)

MAJOR = 1.0
MINOR = 0

PROFILING = -fno-omit-frame-pointer -pg

CFLAGS += -g -I../include -I. -fPIC $(PROFILING)

MPICC = mpicc

.PHONY: all
all: libvchan.so vchan-node1 vchan-node2 libvchan.a bw bw-file bw-gnt-mpi-file bw-rpc bw-mpi-file

libvchan.so: libvchan.so.$(MAJOR)
	ln -sf $< $@

libvchan.so.$(MAJOR): libvchan.so.$(MAJOR).$(MINOR)
	ln -sf $< $@

libvchan.so.$(MAJOR).$(MINOR): $(LIBVCHAN_OBJS)
	$(CC) $(LDFLAGS) -Wl,$(SONAME_LDFLAG) -Wl,libvchan.so.$(MAJOR) $(SHLIB_LDFLAGS) -o $@ $^ $(LIBVCHAN_LIBS)

libvchan.a: $(LIBVCHAN_OBJS)
	$(AR) rcs libvchan.a $^

vchan-node1: $(NODE_OBJS) libvchan.so
	$(CC) $(LDFLAGS) -o $@ $(NODE_OBJS) libvchan.so $(LDLIBS_libvchan)

vchan-node2: $(NODE2_OBJS) libvchan.so
	$(CC) $(LDFLAGS) -o $@ $(NODE2_OBJS) libvchan.so $(LDLIBS_libvchan)

#bw: bw.o libvchan.so
#	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS_libvchan)

bw: bw.o libvchan.a
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBVCHAN_LIBS)

bw-file: bw-file.o libvchan.a
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBVCHAN_LIBS) $(PROFILING)

bw-gnt-mpi-file: bw-gnt-mpi-file.c libvchan.a
	$(MPICC) $(LDFLAGS) -o $@ $^ $(LIBVCHAN_LIBS)

bw-mpi-file: bw-mpi-file.c
	$(MPICC) -o $@ $^

bw-rpc: bw-rpc.c libvchan.a
	$(MPICC) $(LDFLAGS) -o $@ $^ $(LIBVCHAN_LIBS) $(PROFILING)

.PHONY: install
install: all
	$(INSTALL_DIR) $(DESTDIR)$(LIBDIR)
	$(INSTALL_DIR) $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL_PROG) libvchan.so.$(MAJOR).$(MINOR) $(DESTDIR)$(LIBDIR)
	ln -sf libvchan.so.$(MAJOR).$(MINOR) $(DESTDIR)$(LIBDIR)/libvchan.so.$(MAJOR)
	ln -sf libvchan.so.$(MAJOR) $(DESTDIR)$(LIBDIR)/libvchan.so
	$(INSTALL_DATA) libvchan.a $(DESTDIR)$(LIBDIR)
	#$(INSTALL_PROG) libvchan.so.$(MAJOR).$(MINOR) /home/pllopis/lib
	#$(INSTALL_DATA) libvchan.a /home/pllopis/lib
	#$(INSTALL_DATA) libvchan.a /home/pllopis/src/gvirtus/util
	$(INSTALL_PROG) bw /home/pllopis/src/gnt
	$(INSTALL_PROG) bw-file /home/pllopis/src/gnt
	$(INSTALL_PROG) bw-gnt-mpi-file /home/pllopis/src/gnt
	$(INSTALL_PROG) bw-mpi-file /home/pllopis/src/gnt
	$(INSTALL_PROG) bw-rpc /home/pllopis/src/gnt

.PHONY: clean
clean:
	$(RM) -f *.o *.so* *.a vchan-node1 vchan-node2 $(DEPS)

distclean: clean

-include $(DEPS)
