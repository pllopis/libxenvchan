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
 *  This file contains the setup code used to establish the ring buffer.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/user.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <xs.h>
#include <xen/sys/evtchn.h>
#include <xen/sys/gntalloc.h>
#include <xen/sys/gntdev.h>
#include "libvchan.h"

#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define max(a,b) ((a > b) ? a : b)

static int init_gnt_srv(struct libvchan *ctrl)
{
   int pages_left = ctrl->read.order >= PAGE_SHIFT ? 1 << (ctrl->read.order - PAGE_SHIFT) : 0;
   int pages_right = ctrl->write.order >= PAGE_SHIFT ? 1 << (ctrl->write.order - PAGE_SHIFT) : 0;
   struct ioctl_gntalloc_alloc_gref *gref_info = NULL;
   int ring_fd = open("/dev/xen/gntalloc", O_RDWR);
   int ring_ref = -1;
   int err;
   void *ring, *area;

   if (ring_fd < 0)
       return -1;

   gref_info = malloc(sizeof(*gref_info) + max(pages_left, pages_right)*sizeof(uint32_t));

   gref_info->domid = ctrl->other_domain_id;
   gref_info->flags = GNTALLOC_FLAG_WRITABLE;
   gref_info->count = 1;

   err = ioctl(ring_fd, IOCTL_GNTALLOC_ALLOC_GREF, gref_info);
   if (err)
       goto out;

   ring = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ring_fd, gref_info->index);

   if (ring == MAP_FAILED)
       goto out;

   ctrl->ring = ring;
   ring_ref = gref_info->gref_ids[0];

   memset(ring, 0, PAGE_SIZE);

   ctrl->read.shr = &ctrl->ring->left;
   ctrl->write.shr = &ctrl->ring->right;
   ctrl->ring->left_order = ctrl->read.order;
   ctrl->ring->right_order = ctrl->write.order;
   ctrl->ring->cli_live = 2;
   ctrl->ring->srv_live = 1;
   ctrl->ring->debug = 0xabcd;

#ifdef IOCTL_GNTALLOC_SET_UNMAP_NOTIFY
   {
       struct ioctl_gntalloc_unmap_notify arg;
       arg.index = gref_info->index + offsetof(struct vchan_interface, srv_live);
       arg.action = UNMAP_NOTIFY_CLEAR_BYTE | UNMAP_NOTIFY_SEND_EVENT;
       arg.event_channel_port = ctrl->event_port;
       ioctl(ring_fd, IOCTL_GNTALLOC_SET_UNMAP_NOTIFY, &arg);
   }
#endif

   if (ctrl->read.order == 10) {
       ctrl->read.buffer = ((void*)ctrl->ring) + 1024;
   } else if (ctrl->read.order == 11) {
       ctrl->read.buffer = ((void*)ctrl->ring) + 2048;
   } else {
       gref_info->count = pages_left;
       err = ioctl(ring_fd, IOCTL_GNTALLOC_ALLOC_GREF, gref_info);
       if (err)
           goto out_ring;
       area = mmap(NULL, pages_left * PAGE_SIZE, PROT_READ | PROT_WRITE,
           MAP_SHARED, ring_fd, gref_info->index);
       if (area == MAP_FAILED)
           goto out_ring;
       ctrl->read.buffer = area;
       memcpy(ctrl->ring->grants, gref_info->gref_ids, pages_left * sizeof(uint32_t));
   }

   if (ctrl->write.order == 10) {
       ctrl->write.buffer = ((void*)ctrl->ring) + 1024;
   } else if (ctrl->write.order == 11) {
       ctrl->write.buffer = ((void*)ctrl->ring) + 2048;
   } else {
       gref_info->count = pages_right;
       err = ioctl(ring_fd, IOCTL_GNTALLOC_ALLOC_GREF, gref_info);
       if (err)
           goto out_unmap_left;
       area = mmap(NULL, pages_right * PAGE_SIZE, PROT_READ | PROT_WRITE,
           MAP_SHARED, ring_fd, gref_info->index);
       if (area == MAP_FAILED)
           goto out_unmap_left;
       ctrl->write.buffer = area;
       memcpy(ctrl->ring->grants + pages_left,
              gref_info->gref_ids, pages_right * sizeof(uint32_t));
   }

out:
   close(ring_fd);
   free(gref_info);
   return ring_ref;
out_unmap_left:
   if (ctrl->read.order > 11)
       munmap(ctrl->read.buffer, pages_left * PAGE_SIZE);
out_ring:
   munmap(ring, PAGE_SIZE);
   ring_ref = -1;
   ctrl->ring = NULL;
   ctrl->write.order = ctrl->read.order = 0;
   goto out;
}

static void* do_gnt_map(int fd, int domid, uint32_t* pages, size_t npages, uint64_t *index)
{
   int i, rv;
   void* area = NULL;
   struct ioctl_gntdev_map_grant_ref *gref_info;
   gref_info = malloc(sizeof(*gref_info) + npages*sizeof(gref_info->refs[0]));
   gref_info->count = npages;
   for(i=0; i < npages; i++) {
       gref_info->refs[i].domid = domid;
       gref_info->refs[i].ref = pages[i];
   }

   rv = ioctl(fd, IOCTL_GNTDEV_MAP_GRANT_REF, gref_info);
   if (rv)
       goto out;
   if (index)
       *index = gref_info->index;
   area = mmap(NULL, PAGE_SIZE * npages, PROT_READ | PROT_WRITE, MAP_SHARED, fd, gref_info->index);
   if (area == MAP_FAILED) {
       struct ioctl_gntdev_unmap_grant_ref undo = {
           .index = gref_info->index,
           .count = gref_info->count
       };
       ioctl(fd, IOCTL_GNTDEV_UNMAP_GRANT_REF, &undo);
       area = NULL;
   }
 out:
   free(gref_info);
   return area;
}

static int init_gnt_cli(struct libvchan *ctrl, uint32_t ring_ref)
{
   int ring_fd = open("/dev/xen/gntdev", O_RDWR);
   int rv = -1;
   uint64_t ring_index;
   uint32_t *grants;
   if (ring_fd < 0)
       return -1;

   ctrl->ring = do_gnt_map(ring_fd, ctrl->other_domain_id, &ring_ref, 1, &ring_index);

   if (!ctrl->ring)
       goto out;

   ctrl->write.order = ctrl->ring->left_order;
   ctrl->read.order = ctrl->ring->right_order;
   ctrl->write.shr = &ctrl->ring->left;
   ctrl->read.shr = &ctrl->ring->right;
   if (ctrl->write.order < 10 || ctrl->write.order > 24)
       goto out_unmap_ring;
   if (ctrl->read.order < 10 || ctrl->read.order > 24)
       goto out_unmap_ring;
   if (ctrl->read.order == ctrl->write.order && ctrl->read.order < 12)
       goto out_unmap_ring;

   grants = ctrl->ring->grants;

   if (ctrl->write.order == 10) {
       ctrl->write.buffer = ((void*)ctrl->ring) + 1024;
   } else if (ctrl->write.order == 11) {
       ctrl->write.buffer = ((void*)ctrl->ring) + 2048;
   } else {
       int pages_left = 1 << (ctrl->write.order - PAGE_SHIFT);
       ctrl->write.buffer = do_gnt_map(ring_fd, ctrl->other_domain_id, grants, pages_left, NULL);
       if (!ctrl->write.buffer)
           goto out_unmap_ring;
       grants += pages_left;
   }

   if (ctrl->read.order == 10) {
       ctrl->read.buffer = ((void*)ctrl->ring) + 1024;
   } else if (ctrl->read.order == 11) {
       ctrl->read.buffer = ((void*)ctrl->ring) + 2048;
   } else {
       int pages_right = 1 << (ctrl->read.order - PAGE_SHIFT);
       ctrl->read.buffer = do_gnt_map(ring_fd, ctrl->other_domain_id, grants, pages_right, NULL);
       if (!ctrl->read.buffer)
           goto out_unmap_left;
   }

#ifdef IOCTL_GNTDEV_SET_UNMAP_NOTIFY
   {
       struct ioctl_gntdev_unmap_notify arg;
       arg.index = ring_index + offsetof(struct vchan_interface, cli_live);
       arg.action = UNMAP_NOTIFY_CLEAR_BYTE | UNMAP_NOTIFY_SEND_EVENT;
       arg.event_channel_port = ctrl->event_port;
       ioctl(ring_fd, IOCTL_GNTDEV_SET_UNMAP_NOTIFY, &arg);
   }
#endif

   rv = 0;
 out:
   close(ring_fd);
   return rv;
 out_unmap_left:
   if (ctrl->write.order >= PAGE_SHIFT)
       munmap(ctrl->write.buffer, 1 << ctrl->write.order);
 out_unmap_ring:
   munmap(ctrl->ring, PAGE_SIZE);
   ctrl->ring = 0;
   ctrl->write.order = ctrl->read.order = 0;
   rv = -1;
   goto out;
}

static int init_evt_srv(struct libvchan *ctrl)
{
   struct ioctl_evtchn_bind_unbound_port bind;
   ctrl->event_fd = open("/dev/xen/evtchn", O_RDWR);
   if (ctrl->event_fd < 0)
       return -1;
   bind.remote_domain = ctrl->other_domain_id;
   ctrl->event_port = ioctl(ctrl->event_fd, IOCTL_EVTCHN_BIND_UNBOUND_PORT, &bind);
   if (ctrl->event_port < 0)
       return -1;
   write(ctrl->event_fd, &ctrl->event_port, sizeof(ctrl->event_port));
   return 0;
}

static int init_xs_srv(struct libvchan *ctrl, int ring_ref)
{
   int ret = -1;
   struct xs_handle *xs;
   struct xs_permissions perms[2];
   char buf[64];
   char ref[16];
   char* domid_str = NULL;
   xs = xs_domain_open();
   if (!xs)
       goto fail;
   domid_str = xs_read(xs, 0, "domid", NULL);
   if (!domid_str)
       goto fail_xs_open;

   // owner domain is us
   perms[0].id = atoi(domid_str);
   // permissions for domains not listed = none
   perms[0].perms = XS_PERM_NONE;
   // other domains
   perms[1].id = ctrl->other_domain_id;
   perms[1].perms = XS_PERM_READ;

   snprintf(ref, sizeof ref, "%d", ring_ref);
   snprintf(buf, sizeof buf, "data/vchan/%d/ring-ref", ctrl->device_number);
   if (!xs_write(xs, 0, buf, ref, strlen(ref)))
       goto fail_xs_open;
   if (!xs_set_permissions(xs, 0, buf, perms, 2))
       goto fail_xs_open;

   snprintf(ref, sizeof ref, "%d", ctrl->event_port);
   snprintf(buf, sizeof buf, "data/vchan/%d/event-channel", ctrl->device_number);
   if (!xs_write(xs, 0, buf, ref, strlen(ref)))
       goto fail_xs_open;
   if (!xs_set_permissions(xs, 0, buf, perms, 2))
       goto fail_xs_open;

   ret = 0;
 fail_xs_open:
   free(domid_str);
   xs_daemon_close(xs);
 fail:
   return ret;
}

static int min_order(size_t siz)
{
   int rv = PAGE_SHIFT;
   while (siz > (1 << rv))
       rv++;
   return rv;
}

struct libvchan *libvchan_server_init(int domain, int devno, size_t left_min, size_t right_min)
{
   // if you go over this size, you'll have too many grants to fit in the shared page.
   size_t MAX_RING_SIZE = 256 * PAGE_SIZE;
   struct libvchan *ctrl;
   int ring_ref;
   if (left_min > MAX_RING_SIZE || right_min > MAX_RING_SIZE)
       return 0;

   ctrl = malloc(sizeof(*ctrl));
   if (!ctrl)
       return 0;

   ctrl->other_domain_id = domain;
   ctrl->device_number = devno;
   ctrl->ring = NULL;
   ctrl->event_fd = -1;
   ctrl->is_server = 1;
   ctrl->server_persist = 0;

   ctrl->read.order = min_order(left_min);
   ctrl->write.order = min_order(right_min);

   // if we can avoid allocating extra pages by using in-page rings, do so
#define MAX_SMALL_RING 1024
#define MAX_LARGE_RING 2048
   if (left_min <= MAX_SMALL_RING && right_min <= MAX_LARGE_RING) {
       ctrl->read.order = 10;
       ctrl->write.order = 11;
   } else if (left_min <= MAX_LARGE_RING && right_min <= MAX_SMALL_RING) {
       ctrl->read.order = 11;
       ctrl->write.order = 10;
   } else if (left_min <= MAX_LARGE_RING) {
       ctrl->read.order = 11;
   } else if (right_min <= MAX_LARGE_RING) {
       ctrl->write.order = 11;
   }
   if (init_evt_srv(ctrl))
       goto out;
   ring_ref = init_gnt_srv(ctrl);
   if (ring_ref < 0)
       goto out;
   if (init_xs_srv(ctrl, ring_ref))
       goto out;
   return ctrl;
out:
   libvchan_close(ctrl);
   return 0;
}

static int init_evt_cli(struct libvchan *ctrl)
{
   struct ioctl_evtchn_bind_interdomain bind;
   ctrl->event_fd = open("/dev/xen/evtchn", O_RDWR);
   if (ctrl->event_fd < 0)
       return -1;

   bind.remote_domain = ctrl->other_domain_id;
   bind.remote_port = ctrl->event_port;
   ctrl->event_port = ioctl(ctrl->event_fd, IOCTL_EVTCHN_BIND_INTERDOMAIN, &bind);
   if (ctrl->event_port < 0)
       return -1;
   return 0;
}


struct libvchan *libvchan_client_init(int domain, int devno)
{
   struct libvchan *ctrl = malloc(sizeof(struct libvchan));
   struct xs_handle *xs = NULL;
   char buf[64];
   char *ref;
   int ring_ref;
   unsigned int len;
   if (!ctrl)
       return 0;
   ctrl->other_domain_id = domain;
   ctrl->device_number = devno;
   ctrl->ring = NULL;
   ctrl->event_fd = -1;
   ctrl->write.order = ctrl->read.order = 0;
   ctrl->is_server = 0;

   xs = xs_daemon_open();
   if (!xs)
       xs = xs_domain_open();
   if (!xs)
       goto fail;

// find xenstore entry
   snprintf(buf, sizeof buf, "/local/domain/%d/data/vchan/%d/ring-ref",
       ctrl->other_domain_id, ctrl->device_number);
   ref = xs_read(xs, 0, buf, &len);
   if (!ref)
       goto fail;
   ring_ref = atoi(ref);
   free(ref);
   if (!ring_ref)
       goto fail;
   snprintf(buf, sizeof buf, "/local/domain/%d/data/vchan/%d/event-channel",
       ctrl->other_domain_id, ctrl->device_number);
   ref = xs_read(xs, 0, buf, &len);
   if (!ref)
       goto fail;
   ctrl->event_port = atoi(ref);
   free(ref);
   if (!ctrl->event_port)
       goto fail;

// set up event channel
   if (init_evt_cli(ctrl))
       goto fail;

// set up shared page(s)
   if (init_gnt_cli(ctrl, ring_ref))
       goto fail;

   ctrl->ring->cli_live = 1;
   ctrl->ring->debug = 0xabce;

 out:
   if (xs)
       xs_daemon_close(xs);
   return ctrl;
 fail:
   libvchan_close(ctrl);
   ctrl = NULL;
   goto out;
}
