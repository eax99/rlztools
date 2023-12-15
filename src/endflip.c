/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* endflip:
 * Flip byte order of a file, from little-endian to big-endian and back.
 *
 * Usage:
 * endflip N infile outfile
 * N - width of each symbol, in bytes.
 * Supports symbols of any width, from 2-, 4- and 8-byte long words
 * (i.e. 16, 32 and 64-bit words) to weird stuff like 5-byte or 13-byte
 * symbols, because why not. (There is a limit of 99, though.)
 * If the file 'infile' has a filesize not divisible by N
 * (like an odd number of bytes for N=2),
 * the extra bytes will NOT be output (no padding is done),
 * a warning will be printed, and the output will become divisible by N.
 */

#define MIN_WORD_WIDTH 2
#define MAX_WORD_WIDTH 99

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#ifndef VERSION_STRING
#define VERSION_STRING "0.6"
#endif
#ifndef DATE_STRING
#define DATE_STRING "November 2022"
#endif

void print_help();
void work(int N, FILE* infile, FILE* outfile);

int main(int argc, const char** argv) {
    if (argc != 4) {
        print_help();
        return 0;
    }

    int N;
    N = atoi(argv[1]);

    if (N < MIN_WORD_WIDTH || N > MAX_WORD_WIDTH) {
        print_help();
        return 1;
    }

    FILE * infile, * outfile;
    errno = 0;
    infile = fopen(argv[2], "rb");
    if (infile == NULL) {
        fprintf(stderr, "error opening input file '%s': %s\n",
                argv[2], strerror(errno));
        return 2;
    }
    errno = 0;
    outfile = fopen(argv[3], "wb");
    if (outfile == NULL) {
        fprintf(stderr, "error opening output file '%s': %s\n",
                argv[3], strerror(errno));
        return 3;
    }

    work(N, infile, outfile);
    fclose(outfile);
    fclose(infile);

    return 0;
}

void work(int N, FILE* infile, FILE* outfile) {
    uint8_t buffer[MAX_WORD_WIDTH];
    int c;
    while (1) {
        for (int n = 0; n < N; n++) {
            c = fgetc(infile);
            if (ferror(infile)) {
                fprintf(stderr, "Read error (%d)\n", ferror(infile));
            } else if (n > 0 && c == EOF) {
                fprintf(stderr, "warning: input file size wasn't divisible by %d, ignoring last %d bytes\n", N, n);
                return;
            } else if (n == 0 && c == EOF) {
                return;
            }
            buffer[n] = (uint8_t) c;
        }
        /* output in reverse order */
        for (int m = N-1; m >= 0; m--) {
            fputc(buffer[m], outfile);
            if (ferror(outfile)) {
                fprintf(stderr, "Write error (%d)\n", ferror(outfile));
            }
        }
    }
}

void print_help() {
    fputs("endflip: Flip endianness of multi-byte words.\n\n", stderr);
    fputs("Usage: endflip N inputfile outputfile\n", stderr);
    fputs(" N = width of symbols (minimum 2, maximum 99)\n", stderr);
    fputs("(endfip version " VERSION_STRING ", " DATE_STRING ")\n", stderr);
}


