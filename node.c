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
 * This is a test program for libvchan.  Communications are in one direction,
 * either server (grant offeror) to client or vice versa.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "libvchan.h"

int libvchan_write_all(struct libvchan *ctrl, char *buf, int size)
{
   int written = 0;
   int ret;
   while (written < size) {
       ret = libvchan_write(ctrl, buf + written, size - written);
       if (ret <= 0) {
           perror("write");
           exit(1);
       }
       written += ret;
   }
   return size;
}

int write_all(int fd, char *buf, int size)
{
   int written = 0;
   int ret;
   while (written < size) {
       ret = write(fd, buf + written, size - written);
       if (ret <= 0) {
           perror("write");
           exit(1);
       }
       written += ret;
   }
   return size;
}

void usage(char** argv)
{
   fprintf(stderr, "usage:\n"
       "%s [client|server] [read|write] domid nodeid\n", argv[0]);
   exit(1);
}

#define BUFSIZE 5000
char buf[BUFSIZE];
void reader(struct libvchan *ctrl)
{
   int size;
   for (;;) {
       size = rand() % (BUFSIZE - 1) + 1;
       size = libvchan_read(ctrl, buf, size);
       fprintf(stderr, "#");
       if (size < 0) {
           perror("read vchan");
           libvchan_close(ctrl);
           exit(1);
       }
       size = write_all(1, buf, size);
       if (size < 0) {
           perror("stdout write");
           exit(1);
       }
       if (size == 0) {
           perror("write size=0?\n");
           exit(1);
       }
   }
}

void writer(struct libvchan *ctrl)
{
   int size;
   for (;;) {
       size = rand() % (BUFSIZE - 1) + 1;
       size = read(0, buf, size);
       if (size < 0) {
           perror("read stdin");
           libvchan_close(ctrl);
           exit(1);
       }
       if (size == 0)
           break;
       size = libvchan_write_all(ctrl, buf, size);
       fprintf(stderr, "#");
       if (size < 0) {
           perror("vchan write");
           exit(1);
       }
       if (size == 0) {
           perror("write size=0?\n");
           exit(1);
       }
   }
}


/**
   Simple libvchan application, both client and server.
   One side does writing, the other side does reading; both from
   standard input/output fds.
*/
int main(int argc, char **argv)
{
   int seed = time(0);
   struct libvchan *ctrl = 0;
   int wr = 0;
   if (argc < 4)
       usage(argv);
   if (!strcmp(argv[2], "read"))
       wr = 0;
   else if (!strcmp(argv[2], "write"))
       wr = 1;
   else
       usage(argv);
   if (!strcmp(argv[1], "server"))
       ctrl = libvchan_server_init(atoi(argv[3]), atoi(argv[4]), 0, 0);
   else if (!strcmp(argv[1], "client"))
       ctrl = libvchan_client_init(atoi(argv[3]), atoi(argv[4]));
   else
       usage(argv);
   if (!ctrl) {
       perror("libvchan_*_init");
       exit(1);
   }
   ctrl->blocking = 1;

   srand(seed);
   fprintf(stderr, "seed=%d\n", seed);
   if (wr)
       writer(ctrl);
   else
       reader(ctrl);
   libvchan_close(ctrl);
   return 0;
}
