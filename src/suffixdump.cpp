/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
// Read in a dictionary file and a suffix array file, then print out a bit of each suffix.
#include <iostream>
#include <fstream>
#include <cstdlib>
#include "rlzcommon.h"

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::ifstream;
using std::ostringstream;

void print_help() {
    cout << "Usage: suffixdump [-w 8/16/32/64] DICT_FILE [-W 32/64] SA_FILE" << endl;
    cout << "-w 8/16/32/64: symbol width of dictionary" << endl;
    cout << "-W 32/64: integer width of suffix array file" << endl;
    cout << "Suffix array is interpreted in platform-native byte order." << endl
         << "8-bit data printed out as characters, other widths in hexadecimal." << endl;
}


template <typename T, typename S>
void print_suffixes(FileReader<T>* dict, FileReader<S>* sa) {
    int chars_per_symbol = sizeof(T) == 1 ? 1 : 1 + sizeof(T)*2; // Two nybbles per byte + space
    for (int i = 0; i < sa->size(); i++) {
        S idx = (*sa)[i];
        // Don't print past end-of-file, or the width of the screen
        long num_print = (dict->size() - idx) * chars_per_symbol > 56 ? 56 / chars_per_symbol : (dict->size() - idx);
        cout << std::dec << i << " 0x" << std::hex << idx << " " << std::dec << num_print << ":\t";
        for (unsigned int j = idx; j < idx + num_print; j++) {
            cout << dict->as_string(j);
            if (chars_per_symbol > 1 && j+1 != idx+num_print)
                cout << " ";
        }
        cout << endl;
    }
}


int main(int argc, char** argv) {
    if (argc <= 1) { print_help(); exit(2); }

    int dict_width = 8;
    int sa_width = 32;
    string dict_file_name = "";
    string sa_file_name = "";
    bool dict_given = false;

    int i = 1;
    while (i < argc) {
        string arg_i = string(argv[i]);
        if (arg_i.compare("--help") == 0) {
            print_help(); exit(0);
        } else if (arg_i.compare("-w") == 0 || arg_i.compare("--width") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no width after " << arg_i << endl;
                exit(127);
            }
            i++;
            dict_width = atoi(argv[i]);
            if ((dict_width != 8) && (dict_width != 16) && (dict_width != 32) && (dict_width != 64)) {
                cerr << "Bad arguments: width wasn't 8, 16, 32, or 64" << endl;
                exit(126);
            }
        } else if (arg_i.compare("-W") == 0 || arg_i.compare("--sa-width") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no width after " << arg_i << endl;
                exit(125);
            }
            i++;
            sa_width = atoi(argv[i]);
            if ((sa_width != 32) && (sa_width != 64)) {
                cerr << "Bad arguments: SA symbol width wasn't 32 or 64" << endl;
                exit(124);
            }
        } else {
            if (dict_given) {
                if (sa_file_name.length() != 0) {
                    cerr << "Bad arguments: too many filenames" << endl;
                    exit(123);
                }
                sa_file_name = arg_i;
            } else {
                dict_file_name = arg_i;
                dict_given = true;
            }
        }
        i++;
    }

    if (dict_file_name.length() == 0 || sa_file_name.length() == 0) {
        print_help(); exit(3);
    }

    //cout << "dict " << dict_file_name << " " << dict_width << ", SA " << sa_file_name << " " << sa_width << "." << endl;

    switch (dict_width) {
        case 64: {
            FileReader<uint64_t> dict = FileReader<uint64_t>(dict_file_name);
            switch (sa_width) {
                case 64: {
                    FileReader<uint64_t> sa = FileReader<uint64_t>(sa_file_name);
                    print_suffixes(&dict, &sa);
                    break;
                }
                default:
                case 32: {
                    FileReader<uint32_t> sa = FileReader<uint32_t>(sa_file_name);
                    print_suffixes(&dict, &sa);
                    break;
                }
            }
            break;
        }
        case 32: {
            FileReader<uint32_t> dict = FileReader<uint32_t>(dict_file_name);
            switch (sa_width) {
                case 64: {
                    FileReader<uint64_t> sa = FileReader<uint64_t>(sa_file_name);
                    print_suffixes(&dict, &sa);
                    break;
                }
                default:
                case 32: {
                    FileReader<uint32_t> sa = FileReader<uint32_t>(sa_file_name);
                    print_suffixes(&dict, &sa);
                    break;
                }
            }
            break;
        }
        case 16: {
            FileReader<uint16_t> dict = FileReader<uint16_t>(dict_file_name);
            /*cout << "Size of dict: " << dict.size() << endl;
            cout << "Start of dict:" << endl;
            for (int i = 0; i < 32; i++) {
                cout << std::hex << dict[i] << " ";
            }
            cout << std::dec << endl;*/
            switch (sa_width) {
                case 64: {
                    FileReader<uint64_t> sa = FileReader<uint64_t>(sa_file_name);
                    print_suffixes(&dict, &sa);
                    break;
                }
                default:
                case 32: {
                    FileReader<uint32_t> sa = FileReader<uint32_t>(sa_file_name);
                    print_suffixes(&dict, &sa);
                    break;
                }
            }
            break;
        }
        default:
        case 8: {
            FileReader<uint8_t> dict = FileReader<uint8_t>(dict_file_name);
            switch (sa_width) {
                case 64: {
                    FileReader<uint64_t> sa = FileReader<uint64_t>(sa_file_name);
                    print_suffixes(&dict, &sa);
                    break;
                }
                default:
                case 32: {
                    FileReader<uint32_t> sa = FileReader<uint32_t>(sa_file_name);
                    print_suffixes(&dict, &sa);
                    break;
                }
            }
            break;
        }
    }

    return 0;
}


