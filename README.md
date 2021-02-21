# Audio out with an FTDI UART cable

```

sox badapple.wav --bits 16 --channels 1 --encoding signed-integer --rate 25000 out.raw
make uart-sound && ./uart-sound out.raw /dev/ttyUSB0 2000000

play -t raw -r 25000 -e signed -b 16 -c 1 out.raw

```