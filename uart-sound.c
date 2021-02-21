#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

char *tty = "/dev/ttyUSB0";
int baudrate = 1000000;

typedef enum {
    MODE_PDM   = 0,
    MODE_PWM32 = 1,
    MODE_PWM64 = 2,
} encoding_mode_t;

encoding_mode_t mode;

int main(int argc, char *argv[])
{
    if (argc < 5) {
        printf("Usage: %s <audio.raw> </dev/ttyUART0> <baudrate> <mode>\n"
               "Audio should use 62500 Hz sample rate, 8 bit mono signed\n",
               "mode: 0 = MODE_PDM,\n"
               "      1 = MODE_PWM32,\n"
               "      2 = MODE_PWM64,\n",
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

    // Ugly, don't judge.
    char cmd[1024];
    // sprintf(cmd, "stty -F %s %d cs8 -cstopb -parity -icanon min 1 time 1", tty, baudrate);
    sprintf(cmd, "stty -F %s %d raw -clocal -echo icrnl", tty, baudrate);
    puts(cmd);
    system(cmd);

    int usbdev = open(tty, O_RDWR);

    int bits_per_transfer = 10; // 1 start bit (0) + 8 data bits + 1 stop bit (1)
    int buflen = baudrate / bits_per_transfer;
    char buf[buflen];
    uint32_t *buf32 = (uint32_t *) buf;
    uint64_t *buf64 = (uint64_t *) buf;


    /*

    Actual baudrate = (baudrate // 10) * 8

    Baudrate:   2 000 000
    Actual BR:  1 600 000
    Samplerate:    25 000
    Bits per sample:   64 (32 + 8 1/0 transitions)
    Bytes per sample:   8

    */

    int i = 0;
    int j = 0;
    int k = 0;
    uint32_t pdm = 0;
    int bytes_out = 0;
    for (i = 0; i < samples; i++) {
        int16_t sample = audio_buffer[i];

        if (mode == MODE_PDM) {
            // Emulate a 17 bit PDM buffer
            int k_iter = 8;
            for (k = 0; k < k_iter; k++) {
                uint8_t out = 0;
                // int limit = 1 << 7;
                int limit = 1 << 16;
                for (j = 0; j < 8; j++) {
                    if (pdm > (limit - 1)) {
                        pdm -= limit;
                        // pdm = -limit - pdm;
                    } else if (pdm < -(limit - 1)) {
                        pdm += limit;
                        // pdm = limit + pdm;
                    }
                    pdm = (pdm & 0xffff) + ((uint16_t)(sample + (1 << 15)));

                    // Store LSB
                    int pdm_on = !!(pdm & (1 << 16));
                    out = (out >> 1) | (pdm_on << 7);
                    // out = (out << 1) | (pdm_on & 1);
                }

                // Byte ordering
                // int buf_idx = bytes_out + k_iter - k - 1;
                int buf_idx = bytes_out + k;
                buf[buf_idx] = out;

                if (buf_idx == buflen) {
                    write(usbdev, buf, buflen);
                    bytes_out = 0;
                }
            }

            bytes_out += k_iter;
        } else if (mode == MODE_PWM32) {
            // Emulate 32 bit PWM - works ok at 1 000 000 baud
            // (1000000 // 10 * 8) // 32 = 25000 Hz
            // 32 bits = 4 bytes

            uint8_t pwm = 16 + (sample >> 11); // [-32768, 32767] => [0, 31]
            uint32_t out = (1ULL << pwm) - 1;

            buf32[bytes_out++] = out;

            if (bytes_out == buflen / sizeof(uint32_t)) {
                write(usbdev, buf, buflen);
                bytes_out = 0;
            }
        } else if (mode == MODE_PWM64) {
            // Emulate 64 bit PWM - works ok at 2 000 000 baud
            // (2000000 // 10 * 8) // 64 = 25000 Hz
            // 64 bits = 8 bytes

            // uint8_t pwm = 32 + (sample >> 2); // [-128, 127] => [0, 63]
            uint8_t pwm = 32 + (sample >> 10); // [-32768, 32767] => [0, 63]
            uint64_t out = (1ULL << pwm) - 1;

            buf64[bytes_out++] = out;

            if (bytes_out == buflen / sizeof(uint64_t)) {
                write(usbdev, buf, buflen);
                bytes_out = 0;
            }
        }
    }

    free(audio_buffer);
    fclose(fp);
 
    return 0;
}
