#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

inline double BW(unsigned long long bytes, long usec) {
    double bw;
    // uncomment below to measure in Mbit/s
    bw = (double) ((((double)bytes/**8*/)/(1024*1024)) / (((double)usec)/1000000.0));
    return bw;
}

void usage(void)
{
    printf("./memcpy <blocksize> <total_size> <dst_buffer_size>\n");
    exit(1);
}

int main(int argc, char **argv)
{
    char *buf1, *buf2;
    
    if (argc != 4) {
        usage();
    }

    int blocksize = atoi(argv[1]);
    unsigned long long total_size = atoll(argv[2]);
    unsigned long long buffer_size = atoll(argv[3]);
    unsigned long long count = 0;
    long t1, t2, t;
    struct timeval tv1, tv2;

    if (buffer_size < blocksize) {
        printf("buffer_size < blocksize\n");
        usage();
    }

    buf1 = (char*) malloc(buffer_size);
    buf2 = (char*) malloc(total_size);
    int i;
    //for (i=0; i<total_size; i++) buf2[i] = i % 10;

    for(i=0; i<1; i++) {
        count = 0;
        gettimeofday(&tv1, NULL);
        while (count < total_size) {
            int size = ((total_size - count) < blocksize) ? (total_size - count) : blocksize;
            int dst_offset = (count % buffer_size) + size > buffer_size ? 0 : count % buffer_size;
            memcpy(buf1+dst_offset, buf2+count, size);
            count += size;
        }
        gettimeofday(&tv2, NULL);
        t1 = tv1.tv_sec*1000000 + tv1.tv_usec;
        t2 = tv2.tv_sec*1000000 + tv2.tv_usec;
        t = (t2 - t1);
        printf("BW: %.3f MB/s (%llu bytes in %ld usec), Size: %.2fMB, time: %.3fsec\n", BW(count,t), count, t, ((double)count/(1024*1024)), ((double)t/1000000));
        //if(i==1)printf("%.3f\n", BW(count, t));
    }
    free(buf1);
    free(buf2);
    return 0;
}
