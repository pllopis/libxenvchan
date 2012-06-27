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
char *buf1, *buf2, *buf3, *buf4;
unsigned long long total_size;
int blocksize;

inline double BW(unsigned long long bytes, long usec) {
    double bw;
    // uncomment below to measure in Mbit/s
    bw = (double) ((((double)bytes/**8*/)/(1024*1024)) / (((double)usec)/1000000.0));
    return bw;
}

void hex_dump(char *data, int size, char *caption)
{
    int i; // index in data...
    int j; // index in line...
    char temp[8];
    char buffer[128];
    char *ascii;

    memset(buffer, 0, 128);

    printf("---------> %s <--------- (%d bytes from %p)\n", caption, size, data);

    // Printing the ruler...
    printf("        +0          +4          +8          +c            0   4   8   c   \n");

    // Hex portion of the line is 8 (the padding) + 3 * 16 = 52 chars long
    // We add another four bytes padding and place the ASCII version...
    ascii = buffer + 58;
    memset(buffer, ' ', 58 + 16);
    buffer[58 + 16] = '\n';
    buffer[58 + 17] = '\0';
    buffer[0] = '+';
    buffer[1] = '0';
    buffer[2] = '0';
    buffer[3] = '0';
    buffer[4] = '0';
    for (i = 0, j = 0; i < size; i++, j++)
    {
        if (j == 16)
        {
            printf("%s", buffer);
            memset(buffer, ' ', 58 + 16);

            sprintf(temp, "+%04x", i);
            memcpy(buffer, temp, 5);

            j = 0;
        }

        sprintf(temp, "%02x", 0xff & data[i]);
        memcpy(buffer + 8 + (j * 3), temp, 2);
        if ((data[i] > 31) && (data[i] < 127))
            ascii[j] = data[i];
        else
            ascii[j] = '.';
    }

    if (j != 0)
        printf("%s", buffer);
}

void usage(char** argv)
{
       fprintf(stderr, "usage:\n"
               "%s client [read|write] domid evt-port blocksize transfer_size\n"
               "%s server [read|write] domid evt-port blocksize transfer_size read_buffer_size write_buffer_size\n", argv[0], argv[0]);
       exit(1);
}

inline int send_blocking(struct libvchan *ctrl, void *msg, size_t size)
{
    size_t sz = 0;
    int ret;

    while (sz < size) {
        ret = libvchan_write(ctrl, msg + sz, size - sz);
        //ret = libvchan_send(ctrl, msg, size - sz);
        sz += ret;
    }

    return sz;
}

inline int recv_blocking(struct libvchan *ctrl, void *msg, size_t size)
{
    size_t sz = 0;
    int ret;

    while (sz < size) {
        ret = libvchan_read(ctrl, msg + sz, size - sz);
        //ret = libvchan_recv(ctrl, msg, size - sz);
        sz += ret;
    }

    return sz;
}

void reader(struct libvchan *ctrl)
{
       unsigned long long read_size = 0, wr_size = 0;
       int size, sz;
       struct timeval tv1, tv2;
       long t = 0, t1, t2;
       int i;

       //int f = open("b", O_WRONLY | O_CREAT, 0);
       while (read_size < total_size) {
               size = read_size + blocksize > total_size ? total_size - read_size : blocksize;

               gettimeofday(&tv1, NULL);
               /*recv_blocking(ctrl, buf, 8);
               recv_blocking(ctrl, buf, 6);
               recv_blocking(ctrl, buf1, 4);*/
               //recv_blocking(ctrl, buf2, 43);
               recv_blocking(ctrl, buf2, 8);
               recv_blocking(ctrl, buf2, 6);
               recv_blocking(ctrl, buf2, 8);
               //libvchan_read(ctrl, buf1, 4);
               //libvchan_read(ctrl, buf2, 8);
               size = recv_blocking(ctrl, buf, size+21);
               //size = libvchan_read(ctrl, buf, size);
               //libvchan_read(ctrl, buf3, 8);
               send_blocking(ctrl, buf3, 12);
               //send_blocking(ctrl, buf4, 8);
               //send_blocking(ctrl, buf1, 8);
               //libvchan_send(ctrl, buf4, 16);
               //libvchan_send(ctrl, buf1, 8);
               if (size < 0) {
                       perror("read vchan");
                       libvchan_close(ctrl);
                       exit(1);
               }
               /*if (size > 0) {
                  sz = write(f, buf, size);
                  wr_size += sz;
                  if (sz != size)
                    printf("write fail: requested %d wrote %d\n", size, sz);
               }*/
               gettimeofday(&tv2, NULL);
               t1 = tv1.tv_sec*1000000 + tv1.tv_usec;
               t2 = tv2.tv_sec*1000000 + tv2.tv_usec;
               t += (t2 - t1);

               read_size += size;
               //printf("%lld/%lld\n", read_size, total_size);
       }
       /*fsync(f);
       close(f);
       memcpy(buf, "0123456789\0", 11);
       libvchan_send(ctrl, buf, 11);*/
       printf("BW: %.3f MB/s (%llu bytes in %ld usec), Size: %.2fMB, time: %.3fsec\n", BW(read_size,t), read_size, t, ((double)read_size/(1024*1024)), ((double)t/1000000));
}

void writer(struct libvchan *ctrl)
{
       int size, sz;
       unsigned long long write_size = 0, wr_size = 0;
       struct timeval tv1, tv2;
       long t = 0, t1, t2;
       int i;
       //int f = open("a", O_RDONLY);
       //lseek(f, 0, SEEK_SET);
       while (write_size < total_size) {
               size = write_size + blocksize > total_size ? total_size - write_size : blocksize;
               /*sz = read(f, buf, size);
               wr_size += sz;
               if (sz != size) {
                   perror("file read");
                   printf("read fail: requested %d read %d at offset %ld\n", size, sz, lseek(f, 0, SEEK_CUR));
                   exit(1);
               }*/
               gettimeofday(&tv1, NULL);
               /*send_blocking(ctrl, buf, 8);
               send_blocking(ctrl, buf, 6);
               send_blocking(ctrl, buf1, 4);*/
               send_blocking(ctrl, buf2, 43);
               //libvchan_write(ctrl, buf1, 4);
               //libvchan_write(ctrl, buf2, 8);
               size = send_blocking(ctrl, buf, size);
               //size = libvchan_write(ctrl, buf, size);
               //libvchan_write(ctrl, buf3, 8);
               //send_blocking(ctrl, buf3, 8);
               recv_blocking(ctrl, buf4, 12);
               //libvchan_recv(ctrl, buf4, 16);
               //libvchan_recv(ctrl, buf1, 8);
               gettimeofday(&tv2, NULL);
               t1 = tv1.tv_sec*1000000 + tv1.tv_usec;
               t2 = tv2.tv_sec*1000000 + tv2.tv_usec;
               t += (t2 - t1);

               if (size < 0) {
                       perror("vchan write");
                       exit(1);
               } else if (size == 0) {
                   printf("send size 0, %d\n", libvchan_data_ready(ctrl));
               }
               write_size += size;
               //printf("wr_size %llu write_size %llu total_size %llu\n", wr_size, write_size, total_size);
       }
       
       /*gettimeofday(&tv1, NULL);
       size = recv_blocking(ctrl, buf, 11);
       gettimeofday(&tv2, NULL);
       t1 = tv1.tv_sec*1000000 + tv1.tv_usec;
       t2 = tv2.tv_sec*1000000 + tv2.tv_usec;
       t += (t2 - t1);
       close(f);*/
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
       buf = (char*) malloc(blocksize+100);
       if (buf == NULL) {
            perror("malloc");
            exit(1);
       }
       buf1 = (char*) malloc(blocksize);
       buf2 = (char*) malloc(blocksize);
       buf3 = (char*) malloc(blocksize);
       buf4 = (char*) malloc(blocksize);

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

       int i;
       for (i=0; i<1; i++) {
       if (wr)
               writer(ctrl);
       else
               reader(ctrl);
       }
       libvchan_close(ctrl);
       free(buf);
       free(buf1);
       free(buf2);
       free(buf3);
       free(buf4);
       return 0;
}
