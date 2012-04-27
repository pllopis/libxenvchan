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

CFLAGS += -I../include -I. -fPIC

.PHONY: all
all: libvchan.so vchan-node1 vchan-node2 libvchan.a

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

.PHONY: install
install: all
	$(INSTALL_DIR) $(DESTDIR)$(LIBDIR)
	$(INSTALL_DIR) $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL_PROG) libvchan.so.$(MAJOR).$(MINOR) $(DESTDIR)$(LIBDIR)
	ln -sf libvchan.so.$(MAJOR).$(MINOR) $(DESTDIR)$(LIBDIR)/libvchan.so.$(MAJOR)
	ln -sf libvchan.so.$(MAJOR) $(DESTDIR)$(LIBDIR)/libvchan.so
	$(INSTALL_DATA) libvchan.a $(DESTDIR)$(LIBDIR)

.PHONY: clean
clean:
	$(RM) -f *.o *.so* *.a vchan-node1 vchan-node2 $(DEPS)

distclean: clean

-include $(DEPS)
