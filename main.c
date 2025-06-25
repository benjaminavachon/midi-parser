#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
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

// Helper: swap big endian 32-bit
uint32_t swap32(uint32_t val) {
    return ((val >> 24) & 0xff) | ((val >> 8) & 0xff00) |
           ((val << 8) & 0xff0000) | ((val << 24) & 0xff000000);
}

// Helper: swap big endian 16-bit
uint16_t swap16(uint16_t val) {
    return (val >> 8) | (val << 8);
}

int read_vlq(int fd, uint32_t* out) {
    *out = 0;
    uint8_t b = 0;
    do {
        if (read(fd, &b, 1) != 1) return -1;
        *out = (*out << 7) | (b & 0x7F);
    } while (b & 0x80);
    return 0;
}

void readTrack(int fd, uint32_t offset, uint32_t track_end,uint32_t tempo,uint16_t division, const char* midi_path, fluid_synth_t* synth){
    //uint32_t end_threshold = start_threshold + track_length;
    lseek(fd, offset, SEEK_SET);

    //NEED TO GET THE START THRESHOLD SO I KNOW WHERE TO START LOOKING
    //uint32_t start_threshold = 0;

    /* if (fluid_synth_sfload(synth, sf_path, 1) == FLUID_FAILED) {
        //fprintf(stderr, "Failed to load SoundFont: %s\n", sf_path);
        close(fd);
        return;
    } */

    uint8_t last_event_type = 0;

    while (lseek(fd, 0, SEEK_CUR) < track_end) {
        // Read delta time
        uint32_t delta_ticks = 0;
        if (read_vlq(fd, &delta_ticks) < 0) {
            //fprintf(stderr, "Failed reading delta time\n");
            return;
        }

        // Sleep for delta time converted to microseconds
        uint32_t delay_us = (uint64_t)delta_ticks * tempo / division;
        if (delay_us > 0) {
            usleep(delay_us);
        }

        // Read event type or running status
        uint8_t event_type = 0;
        if (read(fd, &event_type, 1) != 1) {
            //fprintf(stderr, "Failed reading event type\n");
            return;
        }

        if (event_type == 0xFF) {
            // Meta event
            uint8_t meta_type = 0;
            if (read(fd, &meta_type, 1) != 1) {
                //fprintf(stderr, "Failed reading meta type\n");
                return;
            }

            uint32_t meta_length = 0;
            if (read_vlq(fd, &meta_length) < 0) {
                //fprintf(stderr, "Failed reading meta length\n");
                return;
            }

            uint8_t meta_data[256] = {0};
            if (meta_length > 255) meta_length = 255;
            if (read(fd, meta_data, meta_length) != meta_length) {
                //fprintf(stderr, "Failed reading meta data\n");
                return;
            }

            if (meta_type == 0x2F) {
                // End of track
                //printf("End of Track\n");
                break;
            } else if (meta_type == 0x51 && meta_length == 3) {
                // Set tempo
                tempo = (meta_data[0] << 16) | (meta_data[1] << 8) | meta_data[2];
                //printf("Set Tempo: %u microseconds per quarter note (%.2f BPM)\n", tempo, 60000000.0 / tempo);
            } else if (meta_type == 0x03) {
                //printf("Track Name: %.*s\n", meta_length, meta_data);
            }
            // Ignore other meta events
        } else if (event_type == 0xF0 || event_type == 0xF7) {
            // SysEx event - skip
            uint32_t sysex_length = 0;
            if (read_vlq(fd, &sysex_length) < 0) {
                //fprintf(stderr, "Failed reading SysEx length\n");
                return;
            }
            lseek(fd, sysex_length, SEEK_CUR);
            //printf("Skipped SysEx event (length %u)\n", sysex_length);
        } else {
            // Running status
            if ((event_type & 0x80) == 0) {
                lseek(fd, -1, SEEK_CUR);
                event_type = last_event_type;
            } else {
                last_event_type = event_type;
            }

            uint8_t status = event_type & 0xF0;
            uint8_t channel = event_type & 0x0F;

            uint8_t param1 = 0, param2 = 0;
            if (status == 0xC0 || status == 0xD0) {
                if (read(fd, &param1, 1) != 1) return;
            } else {
                if (read(fd, &param1, 1) != 1) return;
                if (read(fd, &param2, 1) != 1) return;
            }

            switch (status) {
                case 0x80: // Note Off
                    fluid_synth_noteoff(synth, channel, param1);
                    printf("Note Off: ch %d, note %d\n", channel, param1);
                    break;
                case 0x90: // Note On
                    if (param2 > 0) {
                        fluid_synth_noteon(synth, channel, param1, param2);
                        printf("Note On: ch %d, note %d, vel %d\n", channel, param1, param2);
                    } else {
                        fluid_synth_noteoff(synth, channel, param1);
                        printf("Note Off (via Note On vel=0): ch %d, note %d\n", channel, param1);
                    }
                    break;
                case 0xA0: // Polyphonic Aftertouch
                    // ignore or implement
                    break;
                case 0xB0: // Control Change
                    // ignore or implement
                    break;
                case 0xC0: // Program Change
                    fluid_synth_program_change(synth, channel, param1);
                    //printf("Program Change: ch %d, program %d\n", channel, param1);
                    break;
                case 0xD0: // Channel Pressure
                    // ignore or implement
                    break;
                case 0xE0: { // Pitch Bend
                    int pitch = ((param2 << 7) | param1) - 8192;
                    // You can implement pitch bend if desired
                    //printf("Pitch Bend: ch %d, value %d\n", channel, pitch);
                    break;
                }
                default:
                    //printf("Unknown MIDI event: 0x%X\n", status);
                    break;
            }
        }
    }

    
    
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <midi_file.mid> <soundfont.sf2>\n", argv[0]);
        return 1;
    }

    const char* midi_path = argv[1];
    const char* sf_path = argv[2];

    int fd = open(midi_path, O_RDONLY);
    if (fd < 0) {
        perror("open midi");
        return 1;
    }

    struct midi_header_t head = {0};
    if (read(fd, &head, sizeof(head)) != sizeof(head)) {
        perror("read header");
        close(fd);
        return 1;
    }

    // Swap endian for multi-byte fields
    uint32_t header_length = swap32(head.header_length);
    uint16_t format = swap16(head.format);
    uint16_t num_tracks = swap16(head.num_tracks);
    uint16_t division = swap16(head.division);

    if (memcmp(head.chunk_type, "MThd", 4) != 0) {
        fprintf(stderr, "Not a valid MIDI file (missing MThd)\n");
        close(fd);
        return 1;
    }

    printf("MIDI header:\n");
    printf(" Format: %d\n", format);
    printf(" Tracks: %d\n", num_tracks);
    printf(" Division: %d ticks per quarter note\n", division);

    // Initialize FluidSynth
    fluid_settings_t* settings = new_fluid_settings();
    fluid_synth_t* synth = new_fluid_synth(settings);
    fluid_audio_driver_t* adriver = new_fluid_audio_driver(settings, synth);

    if (fluid_synth_sfload(synth, sf_path, 1) == FLUID_FAILED) {
        fprintf(stderr, "Failed to load SoundFont: %s\n", sf_path);
        return 1;
    }

    for (int ch = 0; ch < 16; ++ch) {
        fluid_synth_program_change(synth, ch, 0); // Acoustic Grand Piano
    }

    // Tempo in microseconds per quarter note
    uint32_t tempo = 500000; // default 120 BPM

    uint32_t offset = sizeof(head);

    // Process each track
    for (int track_idx = 0; track_idx < num_tracks; ++track_idx) {
        //lseek(fd, 0, SEEK_CUR);
        //lseek(fd,offset,SEEK_SET);
        lseek(fd, offset, SEEK_SET);
        struct midi_track_header_t track_head = {0};
        if (read(fd, &track_head, sizeof(track_head)) != sizeof(track_head)) {
            perror("read track header");
            break;
        }

        if (memcmp(track_head.chunk_type, "MTrk", 4) != 0) {
            fprintf(stderr, "Track %d does not start with MTrk\n", track_idx);
            break;
        }

        uint32_t track_length = swap32(track_head.length);
        off_t track_end = lseek(fd, 0, SEEK_CUR) + track_length;

        printf("\n=== Track %d: length %u bytes ===\n", track_idx + 1, track_length);

        //THIS IS WHERE I DO THE THING
        readTrack(fd,offset,track_end,tempo,division,midi_path,synth);

        offset = track_end; 
        //lseek(fd,offset,SEEK_SET);
        printf("%d bytes from the start\n",offset);
    }

cleanup:
    delete_fluid_audio_driver(adriver);
    delete_fluid_synth(synth);
    delete_fluid_settings(settings);

    close(fd);
    return 0;
}

