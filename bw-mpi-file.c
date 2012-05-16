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
#include <mpi.h>

#define DEBUG       0
#define Printf(fmt, ...)   if(DEBUG) printf(fmt, ##__VA_ARGS__)

char *buf;
char *path;
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
       fprintf(stderr, "usage: %s path blocksize transfer_size\n", argv[0]);
       exit(1);
}

void bw()
{
       unsigned long long read_size = 0, wr_size = 0;
       int size, sz;
       struct timeval tv1, tv2;
       long t = 0, t1, t2;
       int f = open(path, O_WRONLY | O_CREAT, 0);
       while (read_size < total_size) {
               size = read_size + blocksize > total_size ? total_size - read_size : blocksize;

               gettimeofday(&tv1, NULL);
               size = write(f, buf, size);
               gettimeofday(&tv2, NULL);
               t1 = tv1.tv_sec*1000000 + tv1.tv_usec;
               t2 = tv2.tv_sec*1000000 + tv2.tv_usec;
               t += (t2 - t1);

               if (size < 0) {
                       perror("write");
                       exit(1);
               }
               read_size += size;
               //printf("%lld/%lld\n", read_size, total_size);
       }
       gettimeofday(&tv1, NULL);
       fsync(f);
       close(f);
       gettimeofday(&tv2, NULL);
       t1 = tv1.tv_sec*1000000 + tv1.tv_usec;
       t2 = tv2.tv_sec*1000000 + tv2.tv_usec;
       t += (t2 - t1);
       printf("BW: %.3f MB/s (%llu bytes in %ld usec), Size: %.2fMB, time: %.3fsec\n", BW(read_size,t), read_size, t, ((double)read_size/(1024*1024)), ((double)t/1000000));
}

/**
       Simple libvchan application, both client and server.
       One side does writing, the other side does reading.
*/
int main(int argc, char **argv)
{
       int rank;
       
       if (argc < 4)
               usage(argv);

       MPI_Init(&argc, &argv);
       MPI_Comm_rank(MPI_COMM_WORLD, &rank);

       path = (char*)malloc(strlen(argv[1])+4);
       snprintf(path, strlen(argv[1])+4, "%s-%02d", argv[1], rank); 
       blocksize = atoi(argv[2]);
       total_size = atoll(argv[3]);
       buf = (char*) malloc(blocksize);
       if (buf == NULL) {
            perror("malloc");
            exit(1);
       }

       printf("[%02d] Running bandwidth test writing %s, blocksize %d transfer_size %llu\n",
              rank, path, blocksize, total_size);

       unlink(path);
       bw();
       unlink(path);
       free(buf);
       free(path);
       MPI_Finalize();
       return 0;
}
