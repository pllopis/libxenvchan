/**
 * This is a program designed to test communication bandwidth between two Xen domains. 
 * It is based off the example test programs that accompany libxenvchan.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libvchan.h"

#define DEBUG       0
#define Printf(fmt, ...)   if(DEBUG) printf(fmt, ##__VA_ARGS__)

char *buf;
unsigned long long total_size;
int blocksize;

inline double BW(unsigned long long bytes, long usec) {
    double bw;
    // uncomment below to measure in Mbit/s
    bw = (double) ((((double)bytes/**8*/)/(1024*1024)) / (((double)usec)/1000000.0));
    return bw;
}

void usage(char** argv)
{
       fprintf(stderr, "usage:\n"
               "%s client [read|write] domid nodeid blocksize transfer_size\n"
               "%s server [read|write] domid nodeid blocksize transfer_size read_buffer_size write_buffer_size\n", argv[0], argv[0]);
       exit(1);
}

void reader(struct libvchan *ctrl)
{
       unsigned long long read_size = 0;
       int size;
       struct timeval tv1, tv2;
       long t = 0, t1, t2;
       //int f = open("b", O_WRONLY | O_CREAT, 0);
       while (read_size < total_size) {
               size = read_size + blocksize > total_size ? total_size - read_size : blocksize;

               gettimeofday(&tv1, NULL);
               size = libvchan_read(ctrl, buf, size);
               gettimeofday(&tv2, NULL);
               t1 = tv1.tv_sec*1000000 + tv1.tv_usec;
               t2 = tv2.tv_sec*1000000 + tv2.tv_usec;
               t += (t2 - t1);

               if (size < 0) {
                       perror("read vchan");
                       libvchan_close(ctrl);
                       exit(1);
               }
               /*if (size > 0)
                   write(f, buf, size);*/
               read_size += size;
       }
       //close(f);
       printf("BW: %.3f MB/s (%llu bytes in %ld usec), Size: %.2fMB, time: %.3fsec\n", BW(read_size,t), read_size, t, ((double)read_size/(1024*1024)), ((double)t/1000000));
}

void writer(struct libvchan *ctrl)
{
       int size;
       unsigned long long write_size = 0;
       struct timeval tv1, tv2;
       long t = 0, t1, t2;
       //int f = open("a", O_RDONLY);
       while (write_size < total_size) {
               size = write_size + blocksize > total_size ? total_size - write_size : blocksize;
               //read(f, buf, size);
               gettimeofday(&tv1, NULL);
               size = libvchan_write(ctrl, buf, size);
               gettimeofday(&tv2, NULL);
               t1 = tv1.tv_sec*1000000 + tv1.tv_usec;
               t2 = tv2.tv_sec*1000000 + tv2.tv_usec;
               t += (t2 - t1);

               if (size < 0) {
                       perror("vchan write");
                       exit(1);
               }
               write_size += size;
       }
       //close(f);
       printf("BW: %.3f MB/s (%llu bytes in %ld usec), Size: %.2fMB, time: %.3fsec\n", BW(write_size,t), write_size, t, ((double)write_size/(1024*1024)), ((double)t/1000000));
}


/**
       Simple libvchan application, both client and server.
       One side does writing, the other side does reading.
*/
int main(int argc, char **argv)
{
       struct libvchan *ctrl = 0;
       int wr;
       if (argc < 6)
               usage(argv);
       if (!strcmp(argv[2], "read"))
               wr = 0;
       else if (!strcmp(argv[2], "write"))
               wr = 1;
       else
               usage(argv);

       blocksize = atoi(argv[5]);
       total_size = atoll(argv[6]);
       buf = (char*) malloc(blocksize);
       if (buf == NULL) {
            perror("malloc");
            exit(1);
       }

       printf("Running bandwidth test with domain %d on port %d, blocksize %d transfer_size %llu\n",
              atoi(argv[3]), atoi(argv[4]), blocksize, total_size);

       if (!strcmp(argv[1], "server")) {
               if (argc < 8)
                    usage(argv);
               ctrl = libvchan_server_init(atoi(argv[3]), atoi(argv[4]), atoi(argv[7]), atoi(argv[8]));
       } else if (!strcmp(argv[1], "client"))
               ctrl = libvchan_client_init(atoi(argv[3]), atoi(argv[4]));
       else
               usage(argv);
       if (!ctrl) {
               perror("libvchan_*_init");
               exit(1);
       }

       if (wr)
               writer(ctrl);
       else
               reader(ctrl);
       libvchan_close(ctrl);
       return 0;
}
