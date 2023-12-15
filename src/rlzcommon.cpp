/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
// Implementing the classes in rlzcommon.h.
#include "rlzcommon.h"
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using std::string;
using std::ifstream;
using std::ostringstream;
using std::cerr;

bool is_end_sentinel(RLZToken* token)
{
    return token->start_pos == end_sentinel.start_pos
           && token->length == end_sentinel.length;
}


long file_size(std::ifstream* ifs) {
    ifs->seekg(0, ifs->end);
    long size = ifs->tellg();
    ifs->seekg(0, ifs->beg);
    return size;
}

/***** FileReader *****/

template <typename T>
FileReader<T>::FileReader(string filename, bool verbose) {
    infile = ifstream(filename, ifstream::binary);
    if (!infile) {
        std::cerr << "Error: can't open input file " << filename << std::endl;
        exit(1);
    }

    // Get file size
    file_size_bytes = file_size(&infile);
    file_size_symbols = file_size_bytes / sizeof(T);

    if (verbose) {
        cerr << "Reading \"" << filename << "\"...";
        cerr.flush(); // reads might take a long time
    }

    data_array = new T[file_size_symbols];
    infile.read(reinterpret_cast<char *>(data_array), file_size_bytes);
    infile.close();
    if (verbose) {
        cerr << " read " << file_size_symbols << " symbols.\n";
        cerr.flush();
    }
}

template <typename T>
long long FileReader<T>::size() { return file_size_symbols; }

template <typename T>
T FileReader<T>::operator[](long long i) { return data_array[i]; }

template <typename T>
std::string FileReader<T>::as_string(long long i) {
    T sym = data_array[i];
    ostringstream os;
    if (sizeof(T) == 1) {
        if (sym >= ' ' && sym <= '~') {
            os << (char) sym;
        } else {
            if (sym == '\\') { os << "\\"; }
            else if (sym == '\n') { os << "\\n"; }
            else if (sym == '\t') { os << "\\t"; }
            else if (sym == '\r') { os << "\\r"; }
            else { os << '\\' << std::setw(3) << std::setfill('0') << std::oct << (int) sym << std::setw(0); }
        }
    } else {

#if WIDE_CHARS_PRINT_ASCII
        // If all sub-bytes of a multi-byte symbol are printable, print the ASCII representation. Like magic numbers.
        // If UTF-16 but in the ASCII range, print out the ASCII.
        if (sizeof(T) == 2 && (((sym & 0xFF00) == 00) || ((sym & 0x00FF) == 0))) {
            // little-endian system, so the byte order for 0xbbaa is aa bb
            uint8_t a = sym & 255;
            uint8_t b = (sym >> 8) & 255;
            if (a == 0 && b >= ' ' && b <= '~') {
                os << "0'" << b << "'";
            } else if (b == 0 && a >= ' ' && a <= '~') {
                os << "'" << a << "'0";
            } else {
                os << std::setw(sizeof(T)*2) << std::setfill('0') << std::hex << std::uppercase << sym << std::setw(0);
            }
        } else {
            bool all_ascii = true;
            for (int i = 0; i < sizeof(T); i++) {
                int b = (sym >> (i*8)) & 255;
                //std::cout << std::hex << b << std::dec << "\n";
                if (b < ' ' || b > '~') all_ascii = false;
            }
            if (all_ascii) {
                os << '\'';
                for (int i = 0; i < sizeof(T); i++) {
                    int b = (sym >> (i*8)) & 255;
                    os << (unsigned char) b;
                }
                os << '\'';
            } else {
                os << std::setw(sizeof(T)*2) << std::setfill('0') << std::hex << std::uppercase << sym << std::setw(0);
            }
        }
#else

        // Two nybbles per byte
        os << std::setw(sizeof(T)*2) << std::setfill('0') << std::hex << std::uppercase << sym << std::setw(0);

#endif
    }
    return os.str();
}


/***** RLZInputReader *****/

// private:
RLZToken RLZInputReader::next_token_32x2() {
    uint32_t buf[2];
    char* bytebuf = reinterpret_cast<char*>(buf);
    infile.read(bytebuf, 2*sizeof(uint32_t));
    if (infile.fail()) // didn't read all 8 bytes
        return end_sentinel;
    RLZToken token;
    token.start_pos = buf[0];
    token.length = buf[1];
    return token;
}

RLZToken RLZInputReader::next_token_64x2() {
    uint64_t buf[2];
    char* bytebuf = reinterpret_cast<char*>(buf);
    infile.read(bytebuf, 2*sizeof(uint64_t));
    if (infile.fail()) // didn't read all 16 bytes
        return end_sentinel;
    RLZToken token;
    token.start_pos = buf[0];
    token.length = buf[1];
    return token;
}

RLZToken RLZInputReader::next_token_ascii() {
    std::string position_s, length_s;
    infile >> position_s;
    if (infile.eof()) return end_sentinel;
    infile >> length_s;
    uint64_t position = std::stoul(position_s, nullptr, 0);
    int64_t length = std::stol(length_s, nullptr, 0); // 0 for automatic base
    RLZToken token = {position, length};
    return token;
}

// Basically LEB128 coding: a high bit of 1 indicates there are more bytes to
// follow in this code. Numbers are written little end first, so successive
// bytes are shifted more and more. The maximum length for 64-bit integers
// is 10 bytes, but only those that have the highest bit set use the 10th byte.
// Sequences longer than this will return an error in the form of a position
// of zero and a length of -1.
RLZToken RLZInputReader::next_token_vbyte() {
    uint64_t pos = 0; int64_t len = 0;
    RLZToken token;
    int shiftwidth = 0;
    // read position
    while (shiftwidth < 64) {
        int c = infile.get();
        if (infile.fail()) return end_sentinel;
        // need it wide so that shifting works correctly
        uint64_t c64 = c < 0 ? ULLONG_MAX : (uint64_t) c;
        if (c64 & 0x80) { // continuation bit set
            pos += (c64 & 0x7F) << shiftwidth;
            shiftwidth += 7;
        } else { // last byte of number
            pos += c64 << shiftwidth;
            break;
        }
    }
    if (shiftwidth > 64) {
        // read 10 bytes without seeing a continuation byte
        token.start_pos = 0;
        token.length = -1LL;
        return token;
    }
    // read length.
    // 'while' condition is lower now because all positive signed
    // ints (63 bits or shorter) fit in 9 varbyte bytes, so shiftwidth needs
    // to be *at most* 56 -- even that is unlikely for real data.
    shiftwidth = 0;
    while (shiftwidth < 63) {
        int c = infile.get();
        if (infile.fail()) return end_sentinel;
        uint64_t c64 = c < 0 ? ULLONG_MAX : (uint64_t) c;
        if (c64 & 0x80) {
            len += (c64 & 0x7F) << shiftwidth;
            shiftwidth += 7;
        } else {
            len += c64 << shiftwidth;
            break;
        }
    }
    if (shiftwidth >= 63) {
        token.start_pos = 0;
        token.length = -1LL;
        return token;
    }
    token.start_pos = pos;
    token.length = len;
    return token;
}

// public:
RLZInputReader::RLZInputReader(std::string filename, int input_mode) {
    infile = std::ifstream(filename, std::ifstream::binary);
    if (!infile) {
        std::cerr << "Error: can't open input file " << filename << std::endl;
        exit(1);
    }
    mode = input_mode;
}

RLZToken RLZInputReader::next_token() {
    if (infile.eof() || infile.fail()) {
        if (infile.fail()) {
            std::cerr << "Warning: input reading failure\n";
        }
        return end_sentinel;
    }

    switch (mode) {
        case FMT_32X2: return this->next_token_32x2();
        case FMT_64X2: return this->next_token_64x2();
        case FMT_ASCII: return this->next_token_ascii();
        case FMT_VBYTE: {
            RLZToken tok = this->next_token_vbyte();
            if (tok.start_pos == 0 && tok.length == -1LL) {
                cerr << "error: vbyte decoder read a sequence that doesn't fit into 64 bits.\n";
                exit(EXIT_INVALID_INPUT);
            }
            return tok;
            break;
        }
        default:
            cerr << "bug in next_token(), mode code 0x" << std::hex << mode << "\n";
            exit(EXIT_BUG);
    }
}

bool RLZInputReader::keep_going() {
    return !(infile.eof() || infile.fail());
}



