#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <fluidsynth.h>

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

typedef struct {
    uint8_t* data;
    uint32_t length;
    int index;
} midi_track_t;

uint32_t swap32(uint32_t val) {
    return ((val >> 24) & 0xff) | ((val >> 8) & 0xff00) |
           ((val << 8) & 0xff0000) | ((val << 24) & 0xff000000);
}

uint16_t swap16(uint16_t val) {
    return (val >> 8) | (val << 8);
}

void play_track(midi_track_t track, fluid_synth_t* synth, uint16_t division) {
    uint32_t tempo = 500000;
    uint32_t pos = 0;
    uint8_t last_event = 0;

    while (pos < track.length) {
        uint32_t delta = 0;
        uint8_t b;
        do {
            b = track.data[pos++];
            delta = (delta << 7) | (b & 0x7F);
        } while (b & 0x80 && pos < track.length);

        usleep((delta * tempo) / division);

        uint8_t status = track.data[pos++];
        if (status == 0xFF) {
            uint8_t type = track.data[pos++];
            uint32_t len = 0;
            do {
                b = track.data[pos++];
                len = (len << 7) | (b & 0x7F);
            } while (b & 0x80);

            if (type == 0x51 && len == 3) {
                tempo = (track.data[pos] << 16) | (track.data[pos + 1] << 8) | track.data[pos + 2];
            }
            pos += len;
        } else {
            if ((status & 0x80) == 0) {
                pos--;
                status = last_event;
            } else {
                last_event = status;
            }

            uint8_t type = status & 0xF0;
            uint8_t chan = status & 0x0F;
            uint8_t p1 = track.data[pos++];
            uint8_t p2 = (type != 0xC0 && type != 0xD0) ? track.data[pos++] : 0;

            switch (type) {
                case 0x90:
                    if (p2 > 0)
                        fluid_synth_noteon(synth, chan, p1, p2);
                    else
                        fluid_synth_noteoff(synth, chan, p1);
                    break;
                case 0x80:
                    fluid_synth_noteoff(synth, chan, p1);
                    break;
                case 0xC0:
                    fluid_synth_program_change(synth, chan, p1);
                    break;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <midi_file.mid> <soundfont.sf2>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct midi_header_t head;
    read(fd, &head, sizeof(head));
    if (memcmp(head.chunk_type, "MThd", 4) != 0) {
        fprintf(stderr, "Invalid MIDI header\n");
        return 1;
    }

    uint16_t format = swap16(head.format);
    uint16_t num_tracks = swap16(head.num_tracks);
    uint16_t division = swap16(head.division);

    midi_track_t* tracks = calloc(num_tracks, sizeof(midi_track_t));

    for (int i = 0; i < num_tracks; ++i) {
        struct midi_track_header_t track_head;
        read(fd, &track_head, sizeof(track_head));

        if (memcmp(track_head.chunk_type, "MTrk", 4) != 0) {
            fprintf(stderr, "Invalid track header at track %d\n", i);
            return 1;
        }

        uint32_t length = swap32(track_head.length);
        uint8_t* data = malloc(length);
        read(fd, data, length);

        tracks[i].data = data;
        tracks[i].length = length;
        tracks[i].index = i;
    }

    close(fd);

    fluid_settings_t* settings = new_fluid_settings();
    fluid_synth_t* synth = new_fluid_synth(settings);
    fluid_audio_driver_t* adriver = new_fluid_audio_driver(settings, synth);

    if (fluid_synth_sfload(synth, argv[2], 1) == FLUID_FAILED) {
        fprintf(stderr, "Failed to load soundfont\n");
        return 1;
    }

    for (int i = 0; i < num_tracks; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            play_track(tracks[i], synth, division);
            exit(0);
        }
    }

    for (int i = 0; i < num_tracks; ++i) {
        wait(NULL);
    }

    delete_fluid_audio_driver(adriver);
    delete_fluid_synth(synth);
    delete_fluid_settings(settings);

    for (int i = 0; i < num_tracks; ++i) {
        free(tracks[i].data);
    }
    free(tracks);

    return 0;
}
