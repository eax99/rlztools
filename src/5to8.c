/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* 5to8:
 * turn a suffix array composed of 40-bit integers (little-endian byte order)
 * into one composed of 64-bit integers (again, little-endian byte order).
 * Usage:
 * 5to8 infile outfile
 *
 * If compiled with -D FIVETOFOUR this file will instead produce the program
 * 5to4, which turns 40-bit integers into 32-bit integers (little-endian).
 * Not all 40-bit integers fit into 32 bits, so this program will check that
 * every fifth byte is zero: if a fifth byte isn't, a conversion isn't possible
 * and the program will immediately exit with a nonzero exit status.
 *
 * (c) Eve Kivivuori 2022
 */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#define EXIT_CONVERSION_ERROR 1

/* A couple of string definitions for help printouts. */
#ifndef VERSION_STRING
#define VERSION_STRING "0.6"
#endif
#ifndef DATE_STRING
#define DATE_STRING "November 2022"
#endif
#ifdef FIVETOFOUR
#define PROGNAME "5to4"
#define BITS_S "32"
#else
#define PROGNAME "5to8"
#define BITS_S "64"
#endif

int work(FILE* infile, FILE* outfile);

int main(int argc, const char** argv) {
    if (argc != 3) {
        fputs(PROGNAME ": turn 40-bit suffix-arrays into " BITS_S "-bit ones.\n", stderr);
        fputs("Usage: " PROGNAME " infile outfile\n", stderr);
        fputs("Both infile and outfile have machine-native byte order.\n", stderr);
#ifdef FIVETOFOUR
        fputs("Numbers that don't fit in 32 bits cause an error and immediate exit;\nevery fifth byte must be zero, because they are what this program removes.\n", stderr);
#endif
        fputs("Input not evenly divisible into 5-byte chunks is padded with extra zeroes.\n", stderr);
        fputs("(" PROGNAME " version " VERSION_STRING ", " DATE_STRING ")\n", stderr);
        return 0;
    }

    FILE * infile, * outfile;
    errno = 0;
    infile = fopen(argv[1], "rb");
    if (infile == NULL) {
        fprintf(stderr, "error opening input file '%s': %s\n",
                argv[2], strerror(errno));
        return 2;
    }
    errno = 0;
    outfile = fopen(argv[2], "wb");
    if (outfile == NULL) {
        fprintf(stderr, "error opening output file '%s': %s\n",
                argv[3], strerror(errno));
        return 3;
    }

    int retval = work(infile, outfile);
    fclose(outfile);
    fclose(infile);

    return retval;
}

/* returns 0 if everything ok (no early exits if FIVETOFOUR), >0 otherwise */
int work(FILE* infile, FILE* outfile) {
    long long input_byte_number = 0;
    while (1) {
        for (int i = 0; i < 5; i++) {
            int c = fgetc(infile);
            input_byte_number++;
            if (ferror(infile)) {
                fprintf(stderr, "Read error (%d)\n", ferror(infile));
            } else if (i > 0 && c == EOF) {
#ifdef FIVETOFOUR
                fputs("warning: input file size wasn't divisible by 5\n", stderr);
                /* no need for an early exit: because the input wasn't divisible
                 * by five, it means that the final number definitely fits in
                 * 32 bits, so pad to 32 bits and then exit. */
                for ( ; i < 4; i++) fputc(0, outfile);
                return 0;
#else
                fputs("warning: input file size wasn't divisible by 5, padding with extra zeroes\n", stderr);
                for ( ; i < 5; i++) fputc(0, outfile);
                break; /* still need the extra three zero bytes */
#endif
            } else if (i == 0 && c == EOF) {
                return 0; /* natural end of file */
            }
#ifdef FIVETOFOUR
            if (i < 4) {
                fputc((uint8_t) c, outfile);
            } else {
                if (c != 0) {
                    fprintf(stderr, "error: nonzero byte at byte 0x%llx, exiting\n", input_byte_number);
                    return 1;
                }
            }
#else
            fputc((uint8_t) c, outfile);
#endif
        }
#ifndef FIVETOFOUR
        /* output null bytes of padding */
        fputc(0, outfile); fputc(0, outfile); fputc(0, outfile);
#endif
    }
    return 0;
}

