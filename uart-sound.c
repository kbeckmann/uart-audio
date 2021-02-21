#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

char *tty = "/dev/ttyUSB0";
int baudrate = 3000000;

typedef enum {
    MODE_PDM   = 0,
    MODE_PWM32 = 1,
    MODE_PWM64 = 2,
} encoding_mode_t;

encoding_mode_t mode;

int main(int argc, char *argv[])
{
    if (argc < 5) {
        printf("Usage: %s <audio.raw> </dev/ttyUART0> <baud rate> <mode>\n"
               "Audio should use an appropriate sample rate, 16 bit mono signed\n",
               "mode: 0 = MODE_PDM,   sample rate = baud rate / 10 * 8 / 64\n"
               "      1 = MODE_PWM32, sample rate = baud rate / 10 * 8 / 32\n"
               "      2 = MODE_PWM64, sample rate = baud rate / 10 * 8 / 64\n",
               argv[0]);
        return -1;
    }

    FILE *fp = fopen(argv[1], "rb");
    tty = argv[2];
    baudrate = atol(argv[3]);
    mode = atol(argv[4]);

    fseek(fp, 0, SEEK_END);

    int audio_buffer_bytes = ftell(fp);
    int samples = audio_buffer_bytes / sizeof(int16_t);

    fseek(fp, 0, SEEK_SET);

    int16_t *audio_buffer = malloc(audio_buffer_bytes);
    fread(audio_buffer, samples, sizeof(int16_t), fp);

    printf("Read %d bytes from %s. Using %s @%d bps\n", samples, argv[1], tty, baudrate);

    // Ugly, please don't judge.
    char cmd[1024];
    snprintf(cmd, 1023, "stty -F %s %d raw -clocal -echo icrnl", tty, baudrate);
    puts(cmd);
    system(cmd);

    int usbdev = open(tty, O_RDWR);

    int bits_per_transfer = 10; // 
    int buflen = 1024;
    char buf[buflen];
    uint32_t *buf32 = (uint32_t *) buf;
    uint64_t *buf64 = (uint64_t *) buf;

    /*

    UART with 1 start bit and 1 stop bit sends data one byte at a time like this:

    0 D0 D1 D2 D3 D4 D5 D6 D7 1
    \                          \__ Stop bit
     \____________________________ Start bit

    Data bitrate = (baudrate // 10) * 8

    Baudrate:      2 000 000
    Data bitrate:  1 600 000
    Samplerate:       25 000
    Bits per sample:      64 (32 + 8 1/0 transitions)
    Bytes per sample:      8

    */

    int i = 0;
    int j = 0;
    int k = 0;
    uint32_t pdm = 0;
    int samples_out = 0;
    for (i = 0; i < samples; i++) {
        int16_t sample = audio_buffer[i];

        if (mode == MODE_PDM) {
            // Emulate PDM
            int k_iter = 8;
            for (k = 0; k < k_iter; k++) {
                uint8_t out = 0;

                // int limit = 1 << 8;
                int limit = 1 << 16;
                // int limit = 1 << 31;

                for (j = 0; j < 8; j++) {
                    if (pdm >= limit) {
                        pdm -= limit;
                    } else if (pdm <= -limit) {
                        pdm += limit;
                    }

                    // 9 bit
                    // pdm = (pdm & 0xff) + (((uint16_t)(sample + (1 << 15))) >> 8);

                    // 17 bit
                    pdm = (pdm & (limit - 1)) + ((uint16_t)(sample + (1 << 15)));

                    // 32 bit
                    // pdm = (pdm & (limit - 1)) + (((uint16_t)(sample + (1 << 15))) << 15);

                    int pdm_on = !!(pdm & limit);

                    // Store MSB of accumulator in LSB
                    out = (out >> 1) | (pdm_on << 7);

                    // Store MSB of accumulator in MSB
                    // out = (out << 1) | (pdm_on & 1);
                }

                // Byte ordering
                // int buf_idx = samples_out + k_iter - k - 1;
                int buf_idx = samples_out + k;
                buf[buf_idx] = out;

                if (buf_idx == buflen) {
                    write(usbdev, buf, buflen);
                    samples_out = 0;
                }
            }

            samples_out += k_iter;
        } else if (mode == MODE_PWM32) {
            // Emulate 32 bit PWM - works ok at 1 000 000 baud
            // 1000000 // 10 * 8 // 32 = 25000 Hz
            // 32 bits = 4 bytes

            uint8_t pwm = 16 + (sample >> 11); // [-32768, 32767] => [0, 31]
            uint32_t out = (1ULL << pwm) - 1;

            buf32[samples_out++] = out;

            if (samples_out == buflen / sizeof(uint32_t)) {
                write(usbdev, buf, buflen);
                samples_out = 0;
            }
        } else if (mode == MODE_PWM64) {
            // Emulate 64 bit PWM - works ok at 2 000 000 baud
            // 2000000 // 10 * 8 // 64 = 25000 Hz
            // 64 bits = 8 bytes

            uint8_t pwm = 32 + (sample >> 10); // [-32768, 32767] => [0, 63]
            uint64_t out = (1ULL << pwm) - 1;

            buf64[samples_out++] = out;

            if (samples_out == buflen / sizeof(uint64_t)) {
                write(usbdev, buf, buflen);
                samples_out = 0;
            }
        }
    }

    free(audio_buffer);
    fclose(fp);
 
    return 0;
}
