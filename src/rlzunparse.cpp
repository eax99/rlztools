/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* rlzunparse: undo Relative Lempel-Ziv compression
 *
 * Basic usage:
 * rlzunparse -d dictionaryfile -i inputfile.rlz -o outputfile
 *            [-w 8|16|32|64] [-f 32x2|64x2|ascii]
 *
 * By default, dictionary file and output file are processed as 8-bit-wide
 * symbols. This can be changed to 16, 32 and 64-bit wide symbols with
 * -w 16, -w 32, -w 64.
 *
 * Also by default, the input file is processed as consisting of tokens
 * made of two 32-bit integers. This can be changed with '-f 64x2' for
 * 64-bit integers, and '-f ascii' for a text-based format (good for
 * debugging) where there are two hex numbers per line, separated by a space.
 */

/* Summarized changelog:
 * v0.8: now supports vbyte-encoded RLZ input
 * v0.9: implemented arbitrary position decompression, added -a and -b options
 */


#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include "rlzcommon.h"

#ifndef VERSION_STRING
#define VERSION_STRING "0.9.1"
#endif
#ifndef DATE_STRING
#define DATE_STRING "December 2023"
#endif

using std::hex;
using std::dec;
using std::cerr;
using std::endl;
using std::string;
using std::ifstream;
using std::ofstream;

void print_help() {
    cerr << "rlzunparse: decompress Relative Lempel-Ziv -encoded data made by rlzparse.\n"
            "Usage: rlzunparse [options] -d DICTIONARY -i INFILE -o OUTFILE\n"
            "Options:\n"
            "  -w, --width 8/16/32/64    Bit width of dictionary & output symbols, default=8\n"
            "  -f, --input-fmt 32x2/64x2/ascii/vbyte\n"
            "                            Different formats of RLZ files.\n"
            "                            32x2 and 64x2 are pairs of binary integers.\n"
            "                            ascii uses whitespace-separated decimal numbers.\n"
            "                            vbyte is an efficient little-endian byte encoding.\n"
            "  -a I, --from I    Start decompression at output symbol I.\n"
            "  -b J, --to J      Stop decompression at output symbol J.\n"
            "I and J are both inclusive, and start at 1. Leaving out one or the other causes\n"
            "decompression to start at I or stop at J; specifying 0 for either is equivalent\n"
            "to not specifying them at all.\n"
            "Also accepted: --dictionary, --infile, --outfile instead of -d, -i, -o.\n"
            "(rlzunparse version " VERSION_STRING ", " DATE_STRING ")\n";
}


/* OutputWriter is our unparser class: it reads from an RLZInputReader,
 * then writes T-type items (uint8_t, 16_t, 32_t, 64_t).
 * It also does the arbitrary-decompression-position math, counting in
 * T-sized symbols, not in bytes.
 * It's used by initializing with dictionary and output file names
 * (which it will open as files), then calling unparse() with an
 * RLZInputReader parameter; unparse() will read input tokens and
 * call write_next() to do output. */
template <typename T> class OutputWriter {
private:
    FileReader<T> dict;
    long dict_size;
    ofstream outfile;
public:
    OutputWriter(string dict_file_name, string output_file_name)
        : dict(dict_file_name, false)
    {
        dict_size = dict.size();
        outfile = ofstream(output_file_name, ofstream::binary | ofstream::trunc);
        if (!outfile) {
            cerr << "Error: cannot open output file '" << output_file_name << "'\n";
            exit(1);
        }
    }

    long long write_next(RLZToken token, long long start = 0, long long stop = 0) {
        long long pos = token.start_pos;
        long long len = token.length;
        char bytebuf[sizeof(T)];
        T* outbuf = reinterpret_cast<T*>(bytebuf);

        if (len == 0) {
            // output a literal
            outbuf[0] = (T) pos;
            for (uint64_t i = 0; i < sizeof(T); i++) outfile.put(bytebuf[i]);
            return 1L;
        } else {
            if (stop <= 0)
                stop = len;
            long token_end = pos + stop;
            // off-by-one arithmetic check:
            // dict size 8 = indices 0 (inclusive) to 8 (exclusive)
            // 0  1  2  3  4  5  6  7  ! <- out of bounds
            // token (6, 2) =>   1  2     ok. 6 + 2 = 8, 8 <= dict_size.
            // token (7, 1) =>      1     ok, 7 + 1 = 8, 8 <= dict_size.
            // token (7, 2) =>      1  2  not ok: 7 + 2 = 9, 9 > dict_size.
            if (token_end > dict_size) {
                cerr << "Warning: token (0x" << hex << pos << ", 0x" << len << ") exceeds dictionary length of " << dec << dict_size << ", truncating.\n";
                token_end = dict_size;
            }
            for (long x = pos + start; x < token_end; x++) {
                outbuf[0] = dict[x];
                for (size_t i = 0; i < sizeof(T); i++) outfile.put(bytebuf[i]);
            }
            return token.length;
        }
    }

    // returns two 32-bit values packed into one 64-bit int
    uint64_t unparse(RLZInputReader* inputreader, long long start_pos, long long stop_pos) {
        long long toks_read = 0;
        long long syms_written = 0;
        // 1-based index of the last symbol of the most recent token processed
        long long output_pos = 0;
        //cerr << "start " << start_pos << " stop " << stop_pos << "\n";
        while (inputreader->keep_going()) {
            RLZToken tok = inputreader->next_token();
            if (is_end_sentinel(&tok))
                break;
            toks_read++;

            if (start_pos == 0 && stop_pos == 0) {
                syms_written += this->write_next(tok);
            } else {
                // Calculating what segment of the output text this token
                // represents, and whether it's one we need to care about.
                long long effective_token_length = tok.length == 0 ? 1 : tok.length;
                // token_start_pos is the index of token's character 0
                // (0-based indexing) in terms of the output text (1-based)
                long long token_start_pos = output_pos + 1;
                long long token_end_pos = output_pos + effective_token_length;
                //cerr << "(pos="<< tok.start_pos << " len=" << tok.length << " sp=" << token_start_pos << " ep=" << token_end_pos << " outp=" << output_pos << ") ";

                // Start at position I, continue until end.
                if (start_pos > 0 && stop_pos == 0) {
                    //cerr << "1 ";
                    if (token_start_pos >= start_pos) { // this had output_pos >= start_pos, so if it breaks check that
                        syms_written += this->write_next(tok);
                    } else if (token_start_pos < start_pos && token_end_pos >= start_pos) {
                        // "start at character 5", start_pos = 5
                        // token has length = 4
                        // before: offset = 2
                        //         token_start_pos = 2 + 1 = 3
                        //         token_end_pos = 2 + 4 = 6
                        // 0 1 2 3 4 5 6 7 8 9
                        //   a b b a b a a b c
                        //   * *                already decoded part
                        //       * * * *        token's phrase
                        //       0 1 2 3        token-internal indices
                        //           * *        part we want to keep, starting at 2
                        // different case: we start at exactly the token's start
                        // before: offset = 4, token_start_pos = 5, end_pos = 8
                        // 0 1 2 3 4 5 6 7 8 9
                        //   a b b a b a a b c
                        //   * * * *            already decoded part
                        //           * * * *    token's phrase
                        //           0 1 2 3
                        // the correct calculation is start_pos - token_start_pos

                        // Converting both to token-internal indexing and 0-based indexing.
                        long long start = start_pos - token_start_pos;
                        long long stop = tok.length;
                        syms_written += this->write_next(tok, start, stop);
                    } else {
                        //cerr << "skip ";
                        // This token occurs before the bit to output starts.
                        ;
                    }
                    output_pos = token_end_pos;
                // Start at beginning, continue until position J.
                } else if (start_pos == 0 && stop_pos > 0) {
                    //cerr << "2 ";
                    if (token_end_pos < stop_pos) {
                        syms_written += this->write_next(tok);
                    } else if (token_start_pos <= stop_pos && token_end_pos >= stop_pos) {
                        // stop_pos = 6
                        // token has length = 4, starting with offset = 3
                        // token_start = 3+1=4, token_end = 3+4=7
                        // 0 1 2 3 4 5 6 7 8 9
                        //   a b b a b a a b c
                        //   * * *              already written
                        //         * * * *      token's text
                        //         0 1 2 3      token's indices
                        //         * * *        all we want to keep
                        // write_next uses a < comparison, so
                        // token_end - stop_pos = 1 is number of chars to skip
                        // tok->length - token_end + stop_pos
                        // tok->length + stop_pos - token_end stays positive
                        long long start = 0;
                        long long stop = tok.length + stop_pos - token_end_pos;
                        syms_written += this->write_next(tok, start, stop);
                    } else {
                        //cerr << "skip\n";
                        // This token occurs wholly after position J.
                        break;
                    }
                    output_pos = token_end_pos;
                // Start at position I, stop at position J.
                // start_pos = 4, stop_pos = 6
                // 0 1 2 3 4 5 6 7 8 9
                //         * * * *      all we want to output
                //   * *           * *  case 1: token entirely outside range
                //           * *        case 2: token entirely inside range
                //     * * * *          case 3: token overlaps start of range
                //             * * * *  case 4: token overlaps end of range
                //       * * * * * *    case 5: range entirely inside token
                } else {
                    //cerr << "3-";
                    if (token_start_pos > stop_pos || token_end_pos < start_pos) {
                        //cerr << "1 ";
                        // case 1: token entirely outside range
                        if (token_start_pos > stop_pos)
                            break;
                    } else if (token_start_pos >= start_pos && token_end_pos <= stop_pos) {
                        //cerr << "2 ";
                        // case 2: token entirely inside range
                        syms_written += this->write_next(tok);
                    } else if (token_start_pos <= start_pos && token_end_pos <= stop_pos) {
                        //cerr << "3 ";
                        // case 3: token overlaps start of range
                        long long start = start_pos - token_start_pos;
                        long long stop = tok.length;
                        syms_written += this->write_next(tok, start, stop);
                    } else if (token_start_pos >= start_pos && token_end_pos >= stop_pos) {
                        //cerr << "4 ";
                        // case 4: token overlaps end of range
                        long long start = 0;
                        long long stop = tok.length + stop_pos - token_end_pos;
                        syms_written += this->write_next(tok, start, stop);
                    } else {
                        //cerr << "5 ";
                        // case 5: token overlaps entire range
                        long long start = start_pos - token_start_pos;
                        long long stop = tok.length + stop_pos - token_end_pos;
                        syms_written += this->write_next(tok, start, stop);
                    }
                    output_pos = token_end_pos;
                }
                //cerr << "\n";
            }
        }
        return (toks_read << 32) | syms_written;
    }
};


int main(int argc, char **argv) {
    if (argc <= 1) {
        print_help();
        exit(EXIT_USER_ERROR);
    }

    string dict_file_name = "";
    string input_file_name = "";
    string output_file_name = "";
    int symbol_width_bits = 8;
    string input_format = "";
    int input_mode = FMT_32X2;
    long long start_pos = 0;
    long long stop_pos = 0;
    bool quiet_mode = false;

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
        } else if (arg_i.compare("-o") == 0 || arg_i.compare("--outfile") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no filename after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            output_file_name = string(argv[++i]);
        } else if (arg_i.compare("-w") == 0 || arg_i.compare("--width") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no width after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            i++;
            symbol_width_bits = atoi(argv[i]);
            if ((symbol_width_bits != 8) && (symbol_width_bits != 16) && (symbol_width_bits != 32) && (symbol_width_bits != 64)) {
                cerr << "Bad arguments: width wasn't 8, 16, 32, or 64.\n";
                exit(EXIT_USER_ERROR);
            }
        } else if (arg_i.compare("-f") == 0 || arg_i.compare("--input-fmt") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no input format after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            i++;
            input_format = string(argv[i]);
        } else if (arg_i.compare("-a") == 0 || arg_i.compare("--from") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no start position after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            i++;
            start_pos = atoll(argv[i]);
            if (start_pos < 0)
                start_pos = 0;
        } else if (arg_i.compare("-b") == 0 || arg_i.compare("--to") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no stop position after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            i++;
            stop_pos = atoll(argv[i]);
            if (stop_pos < 0)
                stop_pos = 0;
        } else if (arg_i.compare("-q") == 0 || arg_i.compare("--quiet") == 0) {
            quiet_mode = true;
        } else {
            cerr << "Unknown argument '" << arg_i << "'; give input file with -i.\n";
            exit(EXIT_USER_ERROR);
        }
        i++;
    }

    if (start_pos > stop_pos && stop_pos > 0) {
        cerr << "Bad arguments: --from was greater than --to.\n";
        exit(EXIT_USER_ERROR);
    }

    if (dict_file_name.length() == 0) {
        cerr << "Bad arguments: dictionary file name not specified.\n";
        exit(EXIT_USER_ERROR);
    }

    if (input_file_name.length() == 0) {
        cerr << "Bad arguments: input file name not specified.\n";
        exit(EXIT_USER_ERROR);
    }

    if (output_file_name.length() == 0) {
        cerr << "Bad arguments: output file name not specified.\n";
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
    } else if (input_format.compare("vbyte") == 0) {
        input_mode = FMT_VBYTE;
    } else {
        cerr << "Bad arguments: input format not \"32x2\", \"64x2\", \"ascii\" or \"vbyte\".\n";
        exit(EXIT_USER_ERROR);
    }
    /* end argument parsing *****/

    if (!quiet_mode) {
        string dfmt = "", ifmt = "";
        if (symbol_width_bits != 8)
            dfmt = " (" + std::to_string(symbol_width_bits) + "-bit)";
        if (input_mode != FMT_32X2)
            ifmt = " (" + input_format + ")";
        cerr << "rlz-unparsing " << input_file_name << ifmt
             << " + " << dict_file_name << dfmt
             << " -> " << output_file_name << "\n";
    }

    RLZInputReader inputreader(input_file_name, input_mode);

    uint64_t x = 0;
    switch (symbol_width_bits) {
        case 8: {
            OutputWriter<uint8_t> ow = OutputWriter<uint8_t>(dict_file_name, output_file_name);
            x += ow.unparse(&inputreader, start_pos, stop_pos);
            break;
        }
        case 16: {
            OutputWriter<uint16_t> ow = OutputWriter<uint16_t>(dict_file_name, output_file_name);
            x += ow.unparse(&inputreader, start_pos, stop_pos);
            break;
        }
        case 32: {
            OutputWriter<uint32_t> ow = OutputWriter<uint32_t>(dict_file_name, output_file_name);
            x += ow.unparse(&inputreader, start_pos, stop_pos);
            break;
        }
        case 64: {
            OutputWriter<uint64_t> ow = OutputWriter<uint64_t>(dict_file_name, output_file_name);
            x += ow.unparse(&inputreader, start_pos, stop_pos);
            break;
        }
        default:
            cerr << "Bug: unknown symbol_width_bits " << symbol_width_bits << "\n";
            exit(EXIT_BUG);
    }

    if (!quiet_mode) {
        uint32_t num_tokens = x >> 32;
        uint32_t num_symbols = x & 0xFFFFFFFF;
        cerr << input_file_name << ": " << dec << num_tokens << " tokens unparsed into ";
        if (symbol_width_bits == 8) {
            cerr << num_symbols << " bytes\n";
        } else {
            cerr << num_symbols << " symbols = " << (num_symbols * (symbol_width_bits / 8)) << " bytes\n";
        }
    }

    return 0;
}

