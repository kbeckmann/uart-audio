# Audio out with an FTDI UART cable

This encodes audio as either PDM (using a first order sigma-delta stage), 32-bits PWM or 64-bits PWM and sends it as binary data to a UART including standard start and stopbits.

This probably works with many UART cables, not just FTDI.

The program requires raw audio samples in a suitable samplerate to work.

The sample rate is calculated like this:

`baudrate / 10 * 8 / output_bits_per_sample`

For PDM and 64-bits PWM, this gives:

`baudrate / 10 * 8 / 64`

Example: 3MBaud = 3000000 / 10 * 8 / 64 = 37500

For 32-bits PWM:

`baudrate / 10 * 8 / 32`

The following prepares a .wav file into a suitable raw file to be used at 3MBaud in PDM mode.

```
sox input.wav --bits 16 --channels 1 --encoding signed-integer --rate 37500 output.raw
make uart-sound && ./uart-sound output.raw /dev/ttyUSB0 3000000 0
```
