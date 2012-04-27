/**
 * @file
 * @section AUTHORS
 *
 * Copyright (C) 2010  Rafal Wojtczuk  <raf...@invisiblethingslab.com>
 *
 *  Authors:
 *       Rafal Wojtczuk  <raf...@invisiblethingslab.com>
 *       Daniel De Graaf <dgde...@tycho.nsa.gov>
 *
 * @section LICENSE
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * @section DESCRIPTION
 *
 *  Originally borrowed from the Qubes OS Project, http://www.qubes-os.org,
 *  this code has been substantially rewritten to use the gntdev and gntalloc
 *  devices instead of raw MFNs and map_foreign_range.
 *
 *  This is a library for inter-domain communication.  A standard Xen ring
 *  buffer is used, with a datagram-based interface built on top.  The grant
 *  reference and event channels are shared in XenStore under the path
 *  /local/domain/<domid>/data/vchan/<port>/{ring-ref,event-channel}
 *
 *  The ring.h macros define an asymmetric interface to a shared data structure
 *  that assumes all rings reside in a single contiguous memory space. This is
 *  not suitable for vchan because the interface to the ring is symmetric except
 *  for the setup. Unlike the producer-consumer rings defined in ring.h, the
 *  size of the rings used in vchan are determined at execution time instead of
 *  compile time, so the macros in ring.h cannot be used to access the rings.
 */

#include <stdint.h>
#include <sys/types.h>
#include <xen/sys/evtchn.h>

struct ring_shared {
   uint32_t cons, prod;
};

/**
 * vchan_interface: primary shared data structure
 */
struct vchan_interface {
   /**
    * Standard consumer/producer interface, one pair per buffer
    * left is client write, server read
    * right is client read, server write
    */
   struct ring_shared left, right;
   /**
    * size of the rings, which determines their location
    * 10   - at offset 1024 in ring's page
    * 11   - at offset 2048 in ring's page
    * 12+  - uses 2^(N-12) grants to describe the multi-page ring
    * These should remain constant once the page is shared.
    * Only one of the two orders can be 10 (or 11).
    */
   uint16_t left_order, right_order;
   /**
    * Shutdown detection:
    *  0: client (or server) has exited
    *  1: client (or server) is connected
    *  2: client has not yet connected
    */
   uint8_t cli_live, srv_live;
   /**
    * structure padding; magic values depending on setup stage
    */
   uint16_t debug;
   /**
    * Grant list: ordering is left, right. Must not extend into actual ring
    * or grow beyond the end of the initial shared page.
    * These should remain constant once the page is shared, to allow
    * for possible remapping by a client that restarts.
    */
   uint32_t grants[0];
};

struct libvchan_ring {
   /* Pointer into the shared page. Offsets into buffer. */
   struct ring_shared* shr;
   /* ring data; may be its own shared page(s) depending on order */
   void* buffer;
   /**
    * The size of the ring is (1 << order); offsets wrap around when they
    * exceed this. This copy is required because we can't trust the order
    * in the shared page to remain constant.
    */
   int order;
};

/**
 * struct libvchan: control structure passed to all library calls
 */
struct libvchan {
   /* person we communicate with */
   int other_domain_id;
   /* "port" we communicate on (allows multiple vchans to exist in xenstore) */
   int device_number;
   /* Shared ring page, mapped using gntdev or gntalloc */
   /* Note that the FD for gntdev or gntalloc has already been closed. */
   struct vchan_interface *ring;
   /* event channel interface (needs port for API) */
   int event_fd;
   uint32_t event_port;
   /* informative flags: are we acting as server? */
   int is_server:1;
   /* true if server remains active when client closes (allows reconnection) */
   int server_persist:1;
   /* true if operations should block instead of returning 0 */
   int blocking:1;
   /* communication rings */
   struct libvchan_ring read, write;
};

/**
 * Set up a vchan, including granting pages
 * @param domain The peer domain that will be connecting
 * @param devno A device number, used to identify this vchan in xenstore
 * @param send_min The minimum size (in bytes) of the send ring (left)
 * @param recv_min The minimum size (in bytes) of the receive ring (right)
 * @return The structure, or NULL in case of an error
 */
struct libvchan *libvchan_server_init(int domain, int devno, size_t read_min, size_t write_min);
/**
 * Connect to an existing vchan. Note: you can reconnect to an existing vchan
 * safely, however no locking is performed, so you must prevent multiple clients
 * from connecting to a single server.
 *
 * @param domain The peer domain to connect to
 * @param devno A device number, used to identify this vchan in xenstore
 * @return The structure, or NULL in case of an error
 */
struct libvchan *libvchan_client_init(int domain, int devno);
/**
 * Close a vchan. This deallocates the vchan and attempts to free its
 * resources. The other side is notified of the close, but can still read any
 * data pending prior to the close.
 */
void libvchan_close(struct libvchan *ctrl);

/**
 * Packet-based receive: always reads exactly $size bytes.
 * @param ctrl The vchan control structure
 * @param data Buffer for data that was read
 * @param size Size of the buffer and amount of data to read
 * @return -1 on error, 0 if nonblocking and insufficient data is available, or $size
 */
int libvchan_recv(struct libvchan *ctrl, void *data, size_t size);
/**
 * Stream-based receive: reads as much data as possible.
 * @param ctrl The vchan control structure
 * @param data Buffer for data that was read
 * @param size Size of the buffer
 * @return -1 on error, otherwise the amount of data read (which may be zero if
 *         the vchan is nonblocking)
 */
int libvchan_read(struct libvchan *ctrl, void *data, size_t size);
/**
 * Packet-based send: send entire buffer if possible
 * @param ctrl The vchan control structure
 * @param data Buffer for data to send
 * @param size Size of the buffer and amount of data to send
 * @return -1 on error, 0 if nonblocking and insufficient space is available, or $size
 */
int libvchan_send(struct libvchan *ctrl, const void *data, size_t size);
/**
 * Stream-based send: send as much data as possible.
 * @param ctrl The vchan control structure
 * @param data Buffer for data to send
 * @param size Size of the buffer
 * @return -1 on error, otherwise the amount of data sent (which may be zero if
 *         the vchan is nonblocking)
 */
int libvchan_write(struct libvchan *ctrl, const void *data, size_t size);
/**
 * Waits for reads or writes to unblock, or for a close
 */
int libvchan_wait(struct libvchan *ctrl);
/**
 * Returns the event file descriptor for this vchan. When this FD is readable,
 * libvchan_wait() will not block, and the state of the vchan has changed since
 * the last invocation of libvchan_wait().
 */
int libvchan_fd_for_select(struct libvchan *ctrl);
/**
 * Query the state of the vchan shared page:
 *  return 0 when one side has called libvchan_close() or crashed
 *  return 1 when both sides are open
 *  return 2 [server only] when no client has yet connected
 */
int libvchan_is_open(struct libvchan* ctrl);
/** Amount of data ready to read, in bytes */
int libvchan_data_ready(struct libvchan *ctrl);
/** Amount of data it is possible to send without blocking */
int libvchan_buffer_space(struct libvchan *ctrl);
