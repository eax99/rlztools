/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* divsuffix: used to create suffix arrays of multi-byte-wide inputs.
 *
 * usage: divsuffix [-W64] N infile outfile
 *
 * N is the width of each symbol.
 * -W64 can be used to process 64-bit suffix arrays; default is 32 bits.
 *
 * This works by looking at each index I, removing every index that isn't
 * divisible by N, and those that are left are divided by N.
 *
 * Sensible values of N are 2, 4 and 8, but this isn't picky.
 */

#include <iostream>
#include <string>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
//#include "filereader.h"

#define EXIT_BUG 33
#define EXIT_FREAD_ERROR 82
#define EXIT_FWRITE_ERROR 87
#define EXIT_USER_ERROR 63

#ifndef VERSION_STRING
#define VERSION_STRING "0.6"
#endif
#ifndef DATE_STRING
#define DATE_STRING "November 2022"
#endif

using std::cerr;
using std::string;

void print_help() {
    cerr << "divsuffix: Remove from a suffix array those indices indivisible by a given N,\n"
            "           then divide by N and output those that are left.\n"
            "Usage: divsuffix [-W64] N input_file output_file\n"
            "  -W64  Assume a 64-bit-per-index suffix array; the default is 32 bits.\n"
            "N can be any positive integer, but you probably want to use only 2, 4 or 8.\n"
            "(divsuffix version " VERSION_STRING ", " DATE_STRING ")\n";
}

long work_32(FILE* infile, FILE* outfile, unsigned int N) {
    uint32_t buf[1];
    //uint8_t bytebuf = reinterpret_cast<char *>(buf);
    long num_written = 0;

    while (true) {
        size_t fread_result = fread(reinterpret_cast<void *>(buf), 1, 4, infile);
        if (feof(infile))
            break;
        if (fread_result != 4) {
            cerr << "warning: input size not divisible by 4 or input error (was "<<fread_result<<"), exiting.\n";
            exit(EXIT_FREAD_ERROR);
        }
        uint32_t x = buf[0];
        if (x % N != 0)
            continue;
        x = x / N;
        buf[0] = x;
        size_t fwrite_result = fwrite(reinterpret_cast<void *>(buf), 1, 4, outfile);
        if (ferror(outfile) || fwrite_result != 4) {
            cerr << "warning: write error\n";
            exit(EXIT_FWRITE_ERROR);
        } else {
            num_written += 1;
        }
    }

    return num_written;
}

long work_64(FILE* infile, FILE* outfile, unsigned int N) {
    uint64_t buf[1];
    //uint8_t bytebuf = reinterpret_cast<char *>(buf);
    long num_written = 0;

    while (true) {
        size_t fread_result = fread(reinterpret_cast<void *>(buf), 1, 8, infile);
        if (feof(infile))
            break;
        if (fread_result != 8) {
            cerr << "warning: input size not divisible by 8 or input error (was "<<fread_result<<"), exiting.\n";
            exit(EXIT_FREAD_ERROR);
        }
        uint64_t x = buf[0];
        if (x % N != 0)
            continue;
        x = x / N;
        buf[0] = x;
        size_t fwrite_result = fwrite(reinterpret_cast<void *>(buf), 1, 8, outfile);
        if (ferror(outfile) || fwrite_result != 8) {
            cerr << "warning: write error\n";
            exit(EXIT_FWRITE_ERROR);
        } else {
            num_written += 1;
        }
    }

    return num_written;
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        print_help();
        exit(EXIT_USER_ERROR);
    }

    string input_file_name = "";
    string output_file_name = "";
    int sa_width = 32;
    int N = 0;

    /* Argument parsing *****/
    int i = 1;
    while (i < argc) {
        string arg_i = string(argv[i]);
        if (arg_i.compare("--help") == 0) {
            print_help(); exit(0);
        } else if (arg_i.compare("-W64") == 0) {
            sa_width = 64;
        } else if (arg_i.compare("-W") == 0) {
            if (argc < i + 2) {
                cerr << "error: no width after -W\n";
                exit(EXIT_USER_ERROR);
            }
            string next_arg = string(argv[++i]);
            if (next_arg.compare("64") == 0) {
                sa_width = 64;
            } else if (next_arg.compare("32") == 0) {
                sa_width = 32;
            } else {
                cerr << "error: improper length '" << next_arg << "' after -W\n";
                exit(EXIT_USER_ERROR);
            }
            i++;
        } else {
            if (N == 0) {
                N = atoi(arg_i.c_str());
                if (N <= 0) {
                    cerr << "error: improper N given ('" << arg_i << "'); suggesting 2, 4, 8.\n";
                    exit(EXIT_USER_ERROR);
                }
            } else if (input_file_name.length() == 0) {
                input_file_name = arg_i;
            } else if (output_file_name.length() == 0) {
                output_file_name = arg_i;
            } else {
                cerr << "warning: ignoring extra argument '" << arg_i << "'\n";
            }
        }
        i++;
    }

    if (N == 0) {
        cerr << "Bad arguments: N not specified\n";
        exit(EXIT_USER_ERROR);
    }

    if (input_file_name.length() == 0) {
        cerr << "Bad arguments: input file name not specified\n";
        exit(EXIT_USER_ERROR);
    }

    if (output_file_name.length() == 0) {
        cerr << "Bad arguments: output file name not specified\n";
        exit(EXIT_USER_ERROR);
    }
    /* end argument parsing *****/

    cerr << "Dividing '" << input_file_name << "' by " << N << ", writing to '" << output_file_name << "'\n";

    FILE* infile;
    FILE* outfile;
    errno = 0;
    infile = fopen(argv[2], "rb");
    if (infile == NULL) {
        fprintf(stderr, "error opening input file '%s': %s\n",
                argv[2], strerror(errno));
        return 2;
    }
    errno = 0;
    outfile = fopen(argv[3], "wbx");
    if (outfile == NULL) {
        fprintf(stderr, "error opening output file '%s': %s\n",
                argv[3], strerror(errno));
        return 3;
    }

    long n_written = 0;
    if (sa_width == 32) {
        n_written = work_32(infile, outfile, (unsigned int) N);
    } else if (sa_width == 64) {
        n_written = work_64(infile, outfile, (unsigned int) N);
    }
    fclose(outfile);
    fclose(infile);
    cerr << n_written << " symbols written out, done.\n";

    return 0;
}

