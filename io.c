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
 *  This file contains the communications interface built on the ring buffer.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <xenctrl.h>
#include <xen/io/libvchan.h>

#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

// allow vchan data to be easily observed in strace by doing a
// writev() to FD -1 with the data being read/written.
#ifndef VCHAN_DEBUG
#define VCHAN_DEBUG 0
#endif

#define barrier() asm volatile("" ::: "memory")

static uint32_t rd_prod(struct libvchan *ctrl)
{
   return ctrl->read.shr->prod;
}

static uint32_t* _rd_cons(struct libvchan *ctrl)
{
   return &ctrl->read.shr->cons;
}
#define rd_cons(x) (*_rd_cons(x))

static uint32_t* _wr_prod(struct libvchan *ctrl)
{
   return &ctrl->write.shr->prod;
}
#define wr_prod(x) (*_wr_prod(x))

static uint32_t wr_cons(struct libvchan *ctrl)
{
   return ctrl->write.shr->cons;
}

static const void* rd_ring(struct libvchan *ctrl)
{
   return ctrl->read.buffer;
}

static void* wr_ring(struct libvchan *ctrl)
{
   return ctrl->write.buffer;
}

static uint32_t wr_ring_size(struct libvchan *ctrl)
{
   return (1 << ctrl->write.order);
}

static uint32_t rd_ring_size(struct libvchan *ctrl)
{
   return (1 << ctrl->read.order);
}

int libvchan_data_ready(struct libvchan *ctrl)
{
   return rd_prod(ctrl) - rd_cons(ctrl);
}

int libvchan_buffer_space(struct libvchan *ctrl)
{
   return wr_ring_size(ctrl) - (wr_prod(ctrl) - wr_cons(ctrl));
}

static int do_notify(struct libvchan *ctrl)
{
   struct ioctl_evtchn_notify notify;
   notify.port = ctrl->event_port;
   return ioctl(ctrl->event_fd, IOCTL_EVTCHN_NOTIFY, &notify);
}

int libvchan_wait(struct libvchan *ctrl)
{
   int ret;
   uint32_t dummy;
   ret = read(ctrl->event_fd, &dummy, sizeof(dummy));
   if (ret == -1)
       return -1;
   write(ctrl->event_fd, &dummy, sizeof(dummy));
   return 0;
}

/**
 * returns -1 on error, or size on success
 */
static int do_send(struct libvchan *ctrl, const void *data, size_t size)
{
   int real_idx = wr_prod(ctrl) & (wr_ring_size(ctrl) - 1);
   int avail_contig = wr_ring_size(ctrl) - real_idx;
   if (VCHAN_DEBUG) {
       char metainfo[32];
       struct iovec iov[2];
       iov[0].iov_base = metainfo;
       iov[0].iov_len = snprintf(metainfo, 32, "vchan wr %d/%d", ctrl->other_domain_id, ctrl->device_number);
       iov[1].iov_base = (void *)data;
       iov[1].iov_len = size;
       writev(-1, iov, 2);
   }
   if (avail_contig > size)
       avail_contig = size;
   memcpy(wr_ring(ctrl) + real_idx, data, avail_contig);
   if (avail_contig < size)
   {
       // we rolled across the end of the ring
       memcpy(wr_ring(ctrl), data + avail_contig, size - avail_contig);
   }
   barrier(); // data must be in the ring prior to increment
   wr_prod(ctrl) += size;
   barrier(); // increment must happen prior to notify
   if (do_notify(ctrl) < 0)
       return -1;
   return size;
}

/**
 * returns 0 if no buffer space is available, -1 on error, or size on success
 */
int libvchan_send(struct libvchan *ctrl, const void *data, size_t size)
{
   int avail;
   while (1) {
       if (!libvchan_is_open(ctrl))
           return -1;
       avail = libvchan_buffer_space(ctrl);
       if (size <= avail)
           return do_send(ctrl, data, size);
       if (!ctrl->blocking)
           return 0;
       if (size > wr_ring_size(ctrl))
           return -1;
       if (libvchan_wait(ctrl))
           return -1;
   }
}

int libvchan_write(struct libvchan *ctrl, const void *data, size_t size)
{
   int avail;
   if (!libvchan_is_open(ctrl))
       return -1;
   if (ctrl->blocking) {
       size_t pos = 0;
       while (1) {
           avail = libvchan_buffer_space(ctrl);
           if (pos + avail > size)
               avail = size - pos;
           if (avail)
               pos += do_send(ctrl, data + pos, avail);
           if (pos == size)
               return pos;
           if (libvchan_wait(ctrl))
               return -1;
           if (!libvchan_is_open(ctrl))
               return -1;
       }
   } else {
       avail = libvchan_buffer_space(ctrl);
       if (size > avail)
           size = avail;
       if (size == 0)
           return 0;
       return do_send(ctrl, data, size);
   }
}

static int do_recv(struct libvchan *ctrl, void *data, size_t size)
{
   int real_idx = rd_cons(ctrl) & (rd_ring_size(ctrl) - 1);
   int avail_contig = rd_ring_size(ctrl) - real_idx;
   if (avail_contig > size)
       avail_contig = size;
   barrier(); // data read must happen after rd_cons read
   memcpy(data, rd_ring(ctrl) + real_idx, avail_contig);
   if (avail_contig < size)
   {
       // we rolled across the end of the ring
       memcpy(data + avail_contig, rd_ring(ctrl), size - avail_contig);
   }
   rd_cons(ctrl) += size;
   if (VCHAN_DEBUG) {
       char metainfo[32];
       struct iovec iov[2];
       iov[0].iov_base = metainfo;
       iov[0].iov_len = snprintf(metainfo, 32, "vchan rd %d/%d", ctrl->other_domain_id, ctrl->device_number);
       iov[1].iov_base = data;
       iov[1].iov_len = size;
       writev(-1, iov, 2);
   }
   barrier(); // consumption must happen prior to notify of newly freed space
   if (do_notify(ctrl) < 0)
       return -1;
   return size;
}

/**
 * reads exactly size bytes from the vchan.
 * returns 0 if insufficient data is available, -1 on error, or size on success
 */
int libvchan_recv(struct libvchan *ctrl, void *data, size_t size)
{
   while (1) {
       int avail = libvchan_data_ready(ctrl);
       if (size <= avail)
           return do_recv(ctrl, data, size);
       if (!libvchan_is_open(ctrl))
           return -1;
       if (!ctrl->blocking)
           return 0;
       if (size > rd_ring_size(ctrl))
           return -1;
       if (libvchan_wait(ctrl))
           return -1;
   }
}

int libvchan_read(struct libvchan *ctrl, void *data, size_t size)
{
   while (1) {
       int avail = libvchan_data_ready(ctrl);
       if (avail && size > avail)
           size = avail;
       if (avail)
           return do_recv(ctrl, data, size);
       if (!libvchan_is_open(ctrl))
           return -1;
       if (!ctrl->blocking)
           return 0;
       if (libvchan_wait(ctrl))
           return -1;
   }
}

int libvchan_is_open(struct libvchan* ctrl)
{
   if (ctrl->is_server)
       return ctrl->server_persist || ctrl->ring->cli_live;
   else
       return ctrl->ring->srv_live;
}

/// The fd to use for select() set
int libvchan_fd_for_select(struct libvchan *ctrl)
{
   return ctrl->event_fd;
}

void libvchan_close(struct libvchan *ctrl)
{
   if (!ctrl)
       return;
   if (ctrl->ring) {
       if (ctrl->is_server)
           ctrl->ring->srv_live = 0;
       else
           ctrl->ring->cli_live = 0;
       munmap(ctrl->ring, PAGE_SIZE);
   }
   if (ctrl->event_fd != -1) {
       if (ctrl->event_port > 0 && ctrl->ring)
           do_notify(ctrl);
       close(ctrl->event_fd);
   }
   if (ctrl->read.order >= PAGE_SHIFT)
       munmap(ctrl->read.buffer, 1 << ctrl->read.order);
   if (ctrl->write.order >= PAGE_SHIFT)
       munmap(ctrl->write.buffer, 1 << ctrl->write.order);
   free(ctrl);
}
