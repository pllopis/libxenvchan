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
 * This is a test program for libvchan.  Communications are bidirectional,
 * with either server (grant offeror) or client able to read and write.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "libvchan.h"

void usage(char** argv)
{
   fprintf(stderr, "usage:\n"
       "\t%s [client|server] domainid nodeid [rbufsiz wbufsiz]\n",
       argv[0]);
   exit(1);
}

#define BUFSIZE 5000
char inbuf[BUFSIZE];
char outbuf[BUFSIZE];
int insiz = 0;
int outsiz = 0;
struct libvchan *ctrl = 0;

void vchan_wr() {
   if (!insiz)
       return;
   int ret = libvchan_write(ctrl, inbuf, insiz);
   if (ret < 0) {
       fprintf(stderr, "vchan write failed\n");
       exit(1);
   }
   if (ret > 0) {
       insiz -= ret;
       memmove(inbuf, inbuf + ret, insiz);
   }
}

void stdout_wr() {
   if (!outsiz)
       return;
   int ret = write(1, outbuf, outsiz);
   if (ret < 0 && errno != EAGAIN)
       exit(1);
   if (ret > 0) {
       outsiz -= ret;
       memmove(outbuf, outbuf + ret, outsiz);
   }
}

/**
    Simple libvchan application, both client and server.
   Both sides may write and read, both from the libvchan and from 
   stdin/stdout (just like netcat).
*/

int main(int argc, char **argv)
{
   int ret;
   int libvchan_fd;
   if (argc < 4)
       usage(argv);
   if (!strcmp(argv[1], "server")) {
       int rsiz = argc > 4 ? atoi(argv[4]) : 0;
       int wsiz = argc > 5 ? atoi(argv[5]) : 0;
       ctrl = libvchan_server_init(atoi(argv[2]), atoi(argv[3]), rsiz, wsiz);
   } else if (!strcmp(argv[1], "client"))
       ctrl = libvchan_client_init(atoi(argv[2]), atoi(argv[3]));
   else
       usage(argv);
   if (!ctrl) {
       perror("libvchan_*_init");
       exit(1);
   }

   fcntl(0, F_SETFL, O_NONBLOCK);
   fcntl(1, F_SETFL, O_NONBLOCK);

   libvchan_fd = libvchan_fd_for_select(ctrl);
   for (;;) {
       fd_set rfds;
       fd_set wfds;
       FD_ZERO(&rfds);
       FD_ZERO(&wfds);
       if (insiz != BUFSIZE)
           FD_SET(0, &rfds);
       if (outsiz)
           FD_SET(1, &wfds);
       FD_SET(libvchan_fd, &rfds);
       ret = select(libvchan_fd + 1, &rfds, &wfds, NULL, NULL);
       if (ret < 0) {
           perror("select");
           exit(1);
       }
       if (FD_ISSET(0, &rfds)) {
           ret = read(0, inbuf + insiz, BUFSIZE - insiz);
           if (ret < 0 && errno != EAGAIN)
               exit(1);
           if (ret == 0) {
               while (insiz) {
                   vchan_wr();
                   libvchan_wait(ctrl);
               }
               return 0;
           }
           if (ret)
               insiz += ret;
           vchan_wr();
       }
       if (FD_ISSET(libvchan_fd, &rfds)) {
           libvchan_wait(ctrl);
           vchan_wr();
       }
       if (FD_ISSET(1, &wfds))
           stdout_wr();
       while (libvchan_data_ready(ctrl) && outsiz < BUFSIZE) {
           ret = libvchan_read(ctrl, outbuf + outsiz, BUFSIZE - outsiz);
           if (ret < 0)
               exit(1);
           outsiz += ret;
           stdout_wr();
       }
       if (!libvchan_is_open(ctrl)) {
           fcntl(1, F_SETFL, 0);
           while (outsiz)
               stdout_wr();
           return 0;
       }
   }
}
