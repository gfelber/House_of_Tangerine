# House_of_Tangerine
The House of Tangerine is a heap exploitation technique that doesn't require free() to achieve arbitrary reads and writes on heap and is confirmed to work on glibc:

* 2.27 (Ubuntu Bionic)
* 2.31 (Ubuntu Focal)
* 2.34 (Ubuntu Jammy)
* 2.39 (Ubuntu Noble & Arch Linux 6.8.1)

House of Tangerine is a modernized version of the House of Orange technique that worked for glibc < 2.26 (therefore the similar name). That targets tcache instead of unsorted-bin to achieve arbitrary reads and writes. It also includes and easy way to leak heap and libc ASLR offsets.

This technique was developed while solving the challenge "High Frequency Troubles" from Pico CTF 2024 by the author pepsipu, so specially thanks to him.
