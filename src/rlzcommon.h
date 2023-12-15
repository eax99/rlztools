/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* Common structs and file reading and writing classes,
 * used by rlzparse.cpp, rlzunparse.cpp, (rlzexplain.cpp).
 */
#ifndef RLZ_COMMON_H_INCLUDED
#define RLZ_COMMON_H_INCLUDED

#include <climits>
#include <cstdint>
#include <fstream>

/* Common data type for representing RLZ tokens across rlzparse & friends.
 * RLZ parsing output will be a stream of these in some binary output format.
 * The un-parsing program will do the reverse: from binary input it will
 * form a sequence of these, and then these tokens will be passed around
 * as the text they refer to is searched for. */
struct RLZToken {
    uint64_t start_pos;
    int64_t length;
};

/* In-band signalling, used by the parser and de-parser to indicate the end
 * of a stream of RLZ tokens. Its implementation is all-ones, referring to
 * a token starting at position 2^64-1 and continuing for -1 bytes;
 * the 2^64-1 start position is mathematically sensible but computationally
 * unrealistic, and the -1 will cause "while (n < token->length)" loops
 * to be skipped. Not impossible for this to appear in a targeted attack as
 * part of an input file to be un-parsed, with the effect being to cause an
 * immediate end to un-parsing. */
const RLZToken end_sentinel = {
    ULLONG_MAX /* start_pos */,
    -1LL /* length */
};

// Compares to the end_sentinel.
bool is_end_sentinel(RLZToken* token);

/* A couple of magic constants, used to toggle between different input
 * and output binary formats. */
#define FMT_32X2  (0x32783233)  /* '32x2', little-endian */
#define FMT_64X2  (0x32783436)  /* '64x2' */
#define FMT_ASCII (0x74786574)  /* 'text' */
#define FMT_VBYTE (0x74796276)  /* 'vbyt' */

// Common definitions for exit codes: was the problem caused by us or not?
#define EXIT_BUG 33          /* '!' */
#define EXIT_USER_ERROR 63   /* '?' */
#define EXIT_INVALID_INPUT 1

// Seeks (ifstream.seekg()) to the end of a file to find out how big it is.
long file_size(std::ifstream* ifs);

// Read byte-mode input but interpret it as different-width unsigned ints.
// The file is read into memory as soon as an instance is constructed.
template <typename T> class FileReader {
private:
    std::ifstream infile;
    long long file_size_bytes;
    long long file_size_symbols; // in units of T, = file_size_bytes/sizeof(T)
    T* data_array;

public:
    /* verbose = true turns on statements like "error: can't open file"
     * and "reading <filename>" and "read <n> symbols". */
    FileReader(std::string filename, bool verbose = false);

    long long size(); // size in units of T
    T operator[](long long i); // main mechanism of access to data

    /* access a single index, return a textual representation of it; used in
     * early debug days for e.g. providing hex representation of uint32 */
    std::string as_string(long long i);
};

template class FileReader<uint8_t>;
template class FileReader<uint16_t>;
template class FileReader<uint32_t>;
template class FileReader<uint64_t>;


// Reads bytes from a file as input and produces RLZTokens.
class RLZInputReader {
private:
    std::ifstream infile;
    int mode;

    RLZToken next_token_32x2();
    RLZToken next_token_64x2();
    RLZToken next_token_ascii();
    RLZToken next_token_vbyte();

public:
    /* Unlike FileReader, doesn't immediately read in the file when
     * constructed; instead, reading happens with next_token calls, which
     * read enough data to produce the next token (which may be the end
     * sentinel). */
    RLZInputReader(std::string filename, int input_mode);
    RLZToken next_token();

    /* Read loops in rlzunparse don't rely entirely on next_token():
     * this function basically just does (infile.eof() || infile.fail()).
     * It fixed some off-by-one bug where we did one loop too many,
     * and the end sentinel wasn't quite enough to fix the problem,
     * or it would've been inelegant. */
    bool keep_going();
};


#endif // include guard, RLZ_COMMON_H_INCLUDED
