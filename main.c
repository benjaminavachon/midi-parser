#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

struct __attribute__((packed)) midi_header_t {
    char chunk_type[4];
    uint32_t header_length;
    uint16_t format;
    uint16_t num_tracks;
    uint16_t division;
};

struct __attribute__((packed)) midi_track_header_t {
    char chunk_type[4];
    uint32_t length;
};

int main(int argc, char *argv[]){
    struct midi_header_t head = {0};

    int fd = open("./MIDI_sample.mid", O_RDONLY);

    read(fd, &head, sizeof(head));

    uint32_t swapped = ((head.header_length>>24)&0xff) | // move byte 3 to byte 0
                    ((head.header_length<<8)&0xff0000) | // move byte 1 to byte 2
                    ((head.header_length>>8)&0xff00) | // move byte 2 to byte 1
                    ((head.header_length<<24)&0xff000000);
    
    uint16_t format = (head.format>>8) | (head.format<<8);
    uint16_t num_tracks = (head.num_tracks>>8) | (head.num_tracks<<8);
    uint16_t division = ((head.division & 0x00FF) << 8) | ((head.division & 0xFF00) >> 8);

    printf("MThd: %s\n",head.chunk_type);
    printf("header_length: %d\n",swapped);
    printf("format: %d\n",format);
    printf("num_tracks: %d\n",num_tracks);
    printf("division: %i\n",division);

    //start reading the body
    
    struct midi_track_header_t track_head = {0};
    read(fd,&track_head,sizeof(track_head));
    uint32_t track_length = ((track_head.length>>24)&0xff) |
                        ((track_head.length<<8)&0xff0000) |
                        ((track_head.length>>8)&0xff00) |
                        ((track_head.length<<24)&0xff000000);
    printf("%i\n",track_length);

    uint8_t buffer[8];
    read(fd, buffer, 8);
    printf("Raw event bytes: ");
    for (int i = 0; i < 8; ++i)
        printf("0x%02X ", buffer[i]);
    printf("\n");
    lseek(fd, -8, SEEK_CUR);

    uint32_t nValue = 0;
    uint8_t nByte = 0;

    do {
        if (read(fd, &nByte, 1) != 1) {
            perror("read");
            break;
        }
        nValue = (nValue << 7) | (nByte & 0x7F);
    } while (nByte & 0x80);
    printf("%i\n",nValue);

    uint8_t event_type;
    read(fd, &event_type, 1);

    if (event_type == 0xFF) {
        uint8_t meta_type;
        read(fd, &meta_type, 1);
        printf("Meta event: 0x%02X\n", meta_type);

        // Get meta data length (also VLQ)
        uint32_t meta_length = 0;
        uint8_t b;
        do {
            read(fd, &b, 1);
            meta_length = (meta_length << 7) | (b & 0x7F);
        } while (b & 0x80);

        // Read meta data
        char data[256] = {0};
        if (meta_length > 255) meta_length = 255;
        read(fd, data, meta_length);
        printf("Meta data: %.*s\n", meta_length, data);
    }


    return 0;
}