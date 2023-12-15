/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* rlzexplain: print out the contents of an RLZ file in human-readable form,
 *             also optionally printing out the textual references in
 *             each token.
 *
 * Basic usage:
 * rlzexplain -i infile.rlz [-d dictionary.rlz] [-f 32x2|64x2|ascii]
 *            [-w 8|16|32|64] [-l line-width] [--hex-addresses]
 *            [--hex-output] [--raw-bytes] [--utf8]
 *
 * By default, doesn't print lines longer than 80 characters;
 * line width can be changed by e.g. -l 100, or -l 0 for unlimited width.
 * Doesn't print out the text that tokens refer to unless given a dictionary.
 * For 8-bit data, prints bytes in the 0x20--0x7E range as ASCII text,
 * with C-style escaping for other bytes; the --hex-output option turns this
 * off and all bytes are printed as they are, and --raw-bytes will print out
 * all bytes exactly as they are, while --utf8 will print out valid UTF-8
 * character sequences unescaped while escaping everything else.
 * For wider data, prints out a sequence of space-separated numbers,
 * in decimal unless --hex-output is given.
 */

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include "rlzcommon.h"

#define DEFAULT_LINE_WIDTH 80
// If a token is too long to fit on a line, we'll print its start,
// then a three-dot ellipsis, then the last END_WIDTH characters.
#define END_WIDTH 5

#ifndef VERSION_STRING
#define VERSION_STRING "0.7"
#endif
#ifndef DATE_STRING
#define DATE_STRING "February 2023"
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))

using std::hex;
using std::dec;
using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;
using std::ifstream;
using std::stringstream;

void print_help() {
    cerr << "rlzexplain: Print out RLZ files as human-readable text.\n"
            "            Given a dictionary, prints out the text that tokens reference.\n"
            "Usage: rlzexplain [options] -i INFILE.RLZ [-d DICTIONARY]\n"
            "Options:\n"
            "  -w, --width 8/16/32/64\tBit width of dictionary symbols, default=8.\n"
            "  -f, --input-fmt 32x2/64x2/ascii\tFormat of RLZ file, default=32x2.\n"
            "  -l N, --line-width N\tDefault 80, set to 0 for unlimited.\n"
            "  --hex-addresses\tPrint offset and length fields in hexadecimal.\n"
            "  --hex-output\tPrint referenced text as hex numbers, even for 8-bit data.\n"
            "  --raw-bytes\tFor 8-bit data, escape no non-ascii text.\n"
            "  --utf8\tFor 8-bit data, detect and don't escape valid UTF-8 sequences.\n"
            "(rlzexplain version" VERSION_STRING ", " DATE_STRING ")\n";
}


// returns width of the string that was used to represent the character
int stream_print_char_maybe_escape(stringstream* ss, char c, bool dont_escape = false)
{
    if (dont_escape || (c >= 32 && c <= 126)) {
        (*ss) << c;
        return 1;
    } else {
        if (c == 0) { (*ss) << "\\0"; return 2; }
        else if (c == 9) { (*ss) << "\\t"; return 2; }
        else if (c == 10) { (*ss) << "\\n"; return 2; }
        else if (c == 13) { (*ss) << "\\r"; return 2; }
        else {
            (*ss) << "\\x" << hex << std::setw(2) << std::setfill('0')
                  << (c & 0xFF) << std::setw(0) << dec; // fixme
            return 4;
        }
    }
}

void work_chars(RLZInputReader* inputreader, FileReader<uint8_t>* dict,
                unsigned int line_width, bool hex_addresses, bool raw_bytes)
{
    while (inputreader->keep_going()) {
        stringstream ss;
        unsigned int cur_len = 0;
        RLZToken tok = inputreader->next_token();
        if (is_end_sentinel(&tok))
            break;
        if (hex_addresses) ss << hex; else ss << dec;
        ss << tok.start_pos << "+" << tok.length << "\t";

        long max_possible_length = dict->size() - tok.start_pos;
        if (tok.length > max_possible_length) {
            ss << "[length too long for dictionary]";
            goto printout;
        } else if (tok.length == 0) {
            stream_print_char_maybe_escape(
                    &ss, (char) tok.start_pos, raw_bytes);
            goto printout;
        }
        // round cur_len to the next multiple of 8 -- a tab stop.
        cur_len = (1 + ((-1 + ss.tellp()) >> 3)) << 3;
        //ss << cur_len << " "; /* debug */
        // For very long tokens + finite line width, grab the end of the token.
        if (tok.length > line_width - cur_len - 3 && tok.length > END_WIDTH+4 && line_width > 0) {
            stringstream last_5;
            unsigned int i = tok.start_pos + tok.length - END_WIDTH;
            while (i < tok.start_pos + tok.length) {
                char c = (*dict)[i++];
                stream_print_char_maybe_escape(&last_5, c, raw_bytes);
            }
            // Then do the start of the token.
            i = 0;
            while (cur_len < line_width - 3 - last_5.tellp() && i < tok.length) {
                char c = (*dict)[tok.start_pos + i++];
                cur_len += stream_print_char_maybe_escape(&ss, c, raw_bytes);
            }
            ss << "..." << last_5.str();
        } else {
            int i = 0;
            while (i < tok.length && (cur_len < line_width || line_width == 0))
            {
                char c = (*dict)[tok.start_pos + i++];
                cur_len += stream_print_char_maybe_escape(&ss, c, raw_bytes);
            }
        }
        printout:
        ss << "\n";
        cout << ss.str();
    }

}




void work_utf8(RLZInputReader* inputreader, FileReader<uint8_t>* dict,
               unsigned int line_width, bool hex_addresses)
{
    while (inputreader->keep_going()) {
        stringstream ss;
        unsigned int cur_len = 0;
        RLZToken tok = inputreader->next_token();
        if (is_end_sentinel(&tok))
            break;
        if (hex_addresses) ss << hex; else ss << dec;
        ss << tok.start_pos << "+" << tok.length << "\t";

        long max_possible_length = dict->size() - tok.start_pos;
        if (tok.length > max_possible_length) {
            ss << "[length too long for dictionary]";
        } else if (tok.length == 0) {
            stream_print_char_maybe_escape(&ss, (char) tok.start_pos);
        } else {
            // round cur_len to the next multiple of 8 -- a tab stop.
            cur_len = (1 + ((-1 + ss.tellp()) >> 3)) << 3;

            /* No special end-of-token printing, because Unicode graphemes'
             * (monospace) width is variable & requires big tables to process.
             * Just assumes all characters are 1 wide, with two coarse
             * exceptions: characters in the range U+3000 to U+9FFF inclusive
             * are assumed to be 2 wide. This includes the big CJK Unified
             * Ideographs block, but also CJK Symbols and Punctuation and the
             * Japanese scripts, which, _generally_, are wide. (This range
             * is easy to detect: it's all those which have their first byte
             * be between 0xE3 and 0xE9, inclusive).
             * The second exception is all 4-byte sequences, referring to
             * characters above U+FFFF; not all blocks in that range are wide,
             * but most of the current use is for emoji, which are wide.
             *
             * Because the spec includes detecting invalid sequences, we
             * need a short buffer: if we find a valid initial UTF-8 byte,
             * we put it in the buffer, and the correct number of valid
             * continuation bytes are also placed in the buffer, after which
             * it is printed; in case of an invalid byte breaking the sequence,
             * we stop and just print the buffer with \x-escapes. */
            vector<uint8_t> bytebuf;
            unsigned int expecting = 0;
            int i = 0;
            while (i < tok.length && (cur_len < line_width || line_width == 0))
            {
                uint8_t c = (*dict)[tok.start_pos + i++];
                if (bytebuf.size() == 0) {
                    // No sequence in progress.
                    if (c <= 127) {
                        // And there will be no sequence: plain ASCII here.
                        cur_len += stream_print_char_maybe_escape(&ss, c);
                    } else if ((c >= 0x80 && c <= 0xBF) || (c >= 0xF5)) {
                        // No sequence: invalid UTF-8 byte, or continuation
                        // without an initial byte.
                        cur_len += stream_print_char_maybe_escape(&ss, c);
                    } else if (c >= 0xC0 && c <= 0xDF) {
                        // Two-byte sequence: U+00 (invalid) or U+80 to U+7FF.
                        expecting = 2;
                        bytebuf.push_back(c);
                    } else if (c >= 0xE0 && c <= 0xEF) {
                        // Three-byte sequence: U+0000/U+0800 to U+FFFF.
                        expecting = 3;
                        bytebuf.push_back(c);
                    } else if (c >= 0xF0 && c <= 0xF4) {
                        // Four-byte sequence: U+0000/U+10000 to U+10FFFF.
                        expecting = 4;
                        bytebuf.push_back(c);
                    }
                } else if (bytebuf.size() + 1 == expecting) {
                    // We're looking at the final byte (maybe) of a sequence.
                    if (c >= 0x80 && c <= 0xBF) {
                        // We have a valid continuation byte!
                        bytebuf.push_back(c);
                        uint8_t b1 = bytebuf[0];
                        // Check for ranges U+3000--U+9FFF, U+10000--U+10FFFF.
                        bool wide_char = (b1>=0xE3 && b1<=0xE9) || b1>=0xF0;
                        // Skip this char if wide and only 1 space left for it.
                        if (wide_char && cur_len == line_width - 1) {
                            break;
                        }
                        for (uint8_t b : bytebuf) {
                            ss << b;
                        }
                        cur_len += wide_char ? 2 : 1;
                    } else {
                        // Final byte wasn't valid, so give up on the sequence.
                        // Skip this char if not enough space for all escapes.
                        bytebuf.push_back(c);
                        if (cur_len >= line_width - (4 * bytebuf.size())) {
                            if (cur_len <= line_width - 3)
                                ss << "...";
                            break;
                        }
                        for (uint8_t b : bytebuf) {
                            cur_len += stream_print_char_maybe_escape(&ss, b);
                        }
                    }
                    bytebuf.clear();
                    expecting = 0;
                } else {
                    // We've seen the first byte of a sequence, but we also
                    // aren't now looking at the last byte of a sequence.
                    // Buffer a valid continuation byte, or give up on the
                    // sequence if given a non-continuation byte.
                    if (c >= 0x80 && c <= 0xBF) {
                        bytebuf.push_back(c);
                    } else {
                        bytebuf.push_back(c);
                        for (uint8_t b : bytebuf) {
                            cur_len += stream_print_char_maybe_escape(&ss, b);
                        }
                        bytebuf.clear();
                        expecting = 0;
                    }
                }
            }
            // We either ran out of space on the line, or the token ended.
            // Do we have anything buffered, and if so, can we print it?
            if (bytebuf.size() > 0 && cur_len < line_width - 4 * bytebuf.size()) {
                for (uint8_t b : bytebuf) {
                    cur_len += stream_print_char_maybe_escape(&ss, b);
                }
            }
        }
        ss << "\n";
        cout << ss.str();
    }

}

template <typename T>
void work_numeric(RLZInputReader* inputreader, FileReader<T>* dict,
                unsigned int line_len, bool hex_addresses, bool hex_output)
{
    while (inputreader->keep_going()) {
        RLZToken tok = inputreader->next_token();
        if (is_end_sentinel(&tok))
            break;
        stringstream ss;
        unsigned int cur_len = 0;
        if (hex_addresses) ss << hex; else ss << dec;
        ss << tok.start_pos << "+" << tok.length << "\t";
        // round cur_len to the next multiple of 8 -- a tab stop.
        cur_len = (1 + ((-1 + ss.tellp()) >> 3)) << 3;

        long max_possible_length = dict->size() - tok.start_pos;
        if (tok.length > max_possible_length) {
            ss << "[length too long for dictionary]";
        } else if (tok.length == 0) {
            if (hex_output) ss << hex; else ss << dec;
            // this expression creates an all-ones literal as big as T
            ss << (tok.start_pos & ((1LL << (sizeof(T)*8))-1));
        } else {
            int i = 0;
            while (i < tok.length && (cur_len < line_len || line_len == 0)) {
                T c = (*dict)[tok.start_pos + i];
                // we want to know the print width of the symbol
                stringstream nbuf;
                if (hex_output) nbuf << hex; else nbuf << dec;
                // This numeric cast is necessary, because uint8_ts would
                // otherwise just be printed as characters.
                nbuf << (unsigned long long) c;
                if (i < tok.length - 1)
                    nbuf << ' '; // separator
                if (cur_len + nbuf.tellp() > line_len) {
                    if (cur_len + 3 <= line_len) {
                        ss << "...";
                    }
                    break;
                } else {
                    ss << nbuf.str();
                    cur_len += nbuf.tellp();
                }
                // convenience for 8-byte data
                if (sizeof(T) == 1 && cur_len + 5 >= line_len && i < tok.length - 1) {
                    ss << "...";
                    break;
                }
                i++;
            }
            ss << "\n";
            cout << ss.str();
        }
    }
}


void print_plain_input(RLZInputReader* inputreader, bool hex_addresses) {
    if (hex_addresses)
        cout << hex;
    else
        cout << dec;
    while (inputreader->keep_going()) {
        RLZToken tok = inputreader->next_token();
        if (is_end_sentinel(&tok))
            break;
        cout << tok.start_pos << "+" << tok.length << "\n";
    }
}


int main(int argc, char **argv) {
    if (argc <= 1) {
        print_help();
        exit(EXIT_USER_ERROR);
    }

    string dict_file_name = "";
    string input_file_name = "";
    int symbol_width_bits = 8;
    string input_format = "";
    unsigned int input_mode = FMT_32X2;
    unsigned int line_width = DEFAULT_LINE_WIDTH;
    bool hex_addresses = false;
    bool hex_output = false;
    bool raw_bytes = false;
    bool utf8 = false;

    /* Argument parsing *****/
    int i = 1;
    while (i < argc) {
        string arg_i = string(argv[i]);
         if (arg_i.compare("--help") == 0) {
            print_help(); exit(0);
        } else if (arg_i.compare("-d") == 0 || arg_i.compare("--dict") == 0 || arg_i.compare("--dictionary") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no filename after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            dict_file_name = string(argv[++i]);
        } else if (arg_i.compare("-i") == 0 || arg_i.compare("--infile") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no filename after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            input_file_name = string(argv[++i]);
        } else if (arg_i.compare("-w") == 0 || arg_i.compare("--width") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no width after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            symbol_width_bits = atoi(argv[++i]);
            if ((symbol_width_bits != 8) && (symbol_width_bits != 16) && (symbol_width_bits != 32) && (symbol_width_bits != 64)) {
                cerr << "Bad arguments: width wasn't 8, 16, 32, or 64\n";
                exit(EXIT_USER_ERROR);
            }
        } else if (arg_i.compare("-f") == 0 || arg_i.compare("--input-fmt") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no input format after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            input_format = string(argv[++i]);
        } else if (arg_i.compare("-l") == 0 || arg_i.compare("--line-width") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no line width given after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            int l = atoi(argv[++i]);
            if (l < 0) {
                cerr << "Bad arguments: negative line width given\n";
                exit(EXIT_USER_ERROR);
            } else {
                line_width = (unsigned int) l;
            }
        } else if (arg_i.compare("--hex-addresses") == 0) {
            hex_addresses = true;
        } else if (arg_i.compare("--hex-output") == 0) {
            hex_output = true;
        } else if (arg_i.compare("--raw-bytes") == 0) {
            raw_bytes = true;
        } else if (arg_i.compare("--utf8") == 0) {
            utf8 = true;
        } else {
            cerr << "Unknown argument '" << arg_i << "'; give input file with -i.\n";
            exit(EXIT_USER_ERROR);
        }
        i++;
    }

    if (input_file_name.length() == 0) {
        cerr << "Bad arguments: input file name not specified\n";
        exit(EXIT_USER_ERROR);
    }

    if (input_format.length() == 0) {
        input_mode = FMT_32X2;
        input_format = "32x2";
    } else if (input_format.compare("32x2") == 0) {
        input_mode = FMT_32X2;
    } else if (input_format.compare("64x2") == 0) {
        input_mode = FMT_64X2;
    } else if (input_format.compare("ascii") == 0) {
        input_mode = FMT_ASCII;
    } else {
        cerr << "Bad arguments: input format not \"32x2\", \"64x2\", or \"ascii\".\n";
        exit(EXIT_USER_ERROR);
    }
    /* end argument parsing *****/

    RLZInputReader ir(input_file_name, input_mode);

    if (dict_file_name.length() == 0) {
        print_plain_input(&ir, hex_addresses);
        return 0;
    }

    switch (symbol_width_bits) {
    case 8: {
        FileReader<uint8_t> dict = FileReader<uint8_t>(dict_file_name);
        if (utf8) {
            work_utf8(&ir, &dict, line_width, hex_addresses);
        } else if (hex_output) {
            work_numeric<uint8_t>(&ir, &dict, line_width, hex_addresses, hex_output);
        } else {
            work_chars(&ir, &dict, line_width, hex_addresses, raw_bytes);
        }
        break;
    }
    default:
        cerr << "bug: unknown symbol_width_bits=" << symbol_width_bits << "\n";
        return EXIT_BUG;
    }

}

