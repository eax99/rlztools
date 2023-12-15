/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* builddict.cpp: build a dictionary for use by a RLZ compressor.
 * Input is optionally handled as 16, 32 or 64-byte units.
 * A dictionary is built up by sampling a number of fixed-size chunks
 * of the input at random (but sorted) positions.
 *
 * A bug: with some combinations of number-of-chunks (N) and
 * length-of-chunks (L), with a total output size of N*L symbols,
 * the program stalls completely -- it doesn't freeze, but the algorithm
 * avoids having overlapping chunks by regenerating them if an overlap
 * is detected. When N*L is small enough there are so few overlaps and
 * their re-generation fixes the problem fast enough for the program to
 * essentially be instantaneous, but at _some_ point, unclear exactly
 * when and why, the problem seems to change from "solved within a second"
 * to "essentially intractable". It's clear that as the ratio of N*L to
 * the size of the input increases this becomes more likely, with a
 * ratio of about N*L = 0.3*input being a _rough_ dividing line.
 * This is far from a hard rule, though, and it appears N is the more
 * sensitive factor here; an N*L size of 0.5*input is possible if N is only
 * 1 or 2 and L is therefore half or quarter of input size.
 * And the boundary can be very sensitive indeed:
 * I don't have exact numbers right now, but in some tests I found that
 * N was intractable (with an N on the order of 20), while N-2 wasn't.
 */
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstring>

#ifndef VERSION_STRING
#define VERSION_STRING "0.7.2"
#endif
#ifndef DATE_STRING
#define DATE_STRING "February 2023"
#endif

#define DEFAULT_N_SAMPLES 64
#define DEFAULT_SAMPLE_LENGTH 128
#define DEFAULT_SEED 314159

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;
using std::ifstream;
using std::ofstream;

void error_die(string msg) {
    cerr << msg << endl;
    exit(1);
}

template <class T> class DictionaryGenerator {
    ifstream infile;
    long long int infilesize_bytes;
    long long int infilesize_symbols;
    ofstream outfile;
    unsigned int n_samples;
    unsigned int sample_length;

    vector<long int> sampling_positions;
    
    bool has_overlaps() {
        auto it = sampling_positions.begin();
        while (it != sampling_positions.end()) {
            long int start_of_prev = *(it - 1);
            long int end_of_prev = start_of_prev + sample_length;
            if (end_of_prev >= *it) {
                return true;
            }
            it++;
        }
        return false;
    }

public:
    DictionaryGenerator(string infilename, string outfilename, unsigned int n, unsigned int l) {
        n_samples = n;
        sample_length = l;
    
        infile = ifstream(infilename, ifstream::binary);
        if (!infile) error_die("Error: cannot open input file " + infilename);
        outfile = ofstream(outfilename, ofstream::binary);
        if (!outfile) error_die("Error: cannot open output file " + outfilename);
        
        infile.seekg(0, infile.end);
        infilesize_bytes = infile.tellg();
        infile.seekg(0, infile.beg);
        infilesize_symbols = infilesize_bytes / sizeof(T);
    }

    vector<long int> get_samp_pos() {
        return sampling_positions;
    }
    
    void gen_sampling_positions() {
        sampling_positions.clear();
        unsigned int n = 0;
        // counting in symbols, not bytes
        while (n < n_samples) {
            long int pos = rand() % (infilesize_symbols - sample_length);
            sampling_positions.push_back(pos);
            n++;
        }
        std::sort(sampling_positions.begin(), sampling_positions.end());
    }
    
    /* It's possible this function loops indefinitely, but it's quite unlikely
     * until calculated output size is about 91% of input size.
     * 
     * Testing with a small setup, with input length 12262 symbols:
     * - asked for 12 samples of 1000 symbols (97.8%), looped indefinitely.
     * - 11 samples of 1000 (89.7%) returned immediately;
     * - 12 samples of 930 (91.01%) returned immediately;
     * - 12 samples of 940 (91.99%) looped indefinitely;
     * - 13 samples of 864 (91.60%) returned immediately;
     * - 13 samples of 865 (91.70) looped indefinitely;
     * - 5 samples of 2000 (81.5%) returned immediately;
     * - 6 samples of 2000 (97.86%) looped indefinitely;
     * - 6 samples of 1800 (88.07%) looped indefinitely;
     * - 6 samples of 1739 (85.14%) returned immediately.
     * - 162 samples of 60 (79.26%) returned in 1 second, 75000 iterations.
     * 
     * Even an "immediate return" does anywhere from 100 iterations (at 50%
     * ratio of output to input) to 130,000 iterations (at 91.11%, 12*931),
     * depending on random seed of course, but it's quite imperceptible.
     * The heuristic at which a non-overlapping set of positions turns from
     * quickly solved to highly unlikely with the rather naive algorithm below
     * isn't immediately obvious, and I don't know it yet, but it depends both
     * on n_samples and sample_length, probably has something to do with the
     * difference (infilesize_symbols - n_samples*sample_length) also, but I
     * guess the main factor is the probability that a random assortment of
     * n_samples points is arranged less than sample_length apart.
     * Perhaps, if this is a real-world problem, the program should detect a
     * point where getting stuck is likely and fall back to a different algo,
     * perhaps one which just samples at regular intervals?
     * In any case, this goes against the use case, of building a dictionary
     * only a fraction of the size of the input, like 1/3 the size at most.
     * A thought: maybe more efficient if sampling_positions is a list? */
    void fix_overlaps() {
        // Two parts: count how many positions overlap, then fix those.
        //int recursions = 0;
        while (has_overlaps()) {
            int needed_rerolls = 0;
            auto it = sampling_positions.begin();
            while (it != sampling_positions.end()) {
                long int start_of_prev = *(it - 1);
                long int end_of_prev = start_of_prev + sample_length;
                if (end_of_prev >= *it) {
                    needed_rerolls++;
                    it = sampling_positions.erase(it);
                } else {
                    it++;
                }
            }
            
            if (needed_rerolls > 0) {
                for (int i = 0; i < needed_rerolls; i++) {
                    long int pos = rand() % (infilesize_symbols - sample_length);
                    sampling_positions.push_back(pos);
                }
                std::sort(sampling_positions.begin(), sampling_positions.end());
            }
            //recursions++;
        }
        //cout << "overlap fixes: " << recursions << endl;
    }

    /* // we don't actually ever need to turn the bytes into a T, as long as we stay aligned
    T read_next_symbol() {
        unsigned char buf[sizeof(T) + 1]; // +1 for the terminating '\0'.
        infile >> buf;
        T out = 0;
        // byte-by-byte is the first way that came into my mind
        for (int i = 0; i < sizeof(T); i++) {
            out = out | buf[i];
            out << 8;
        }
        return T;
    } */

    void write_output() {
        for (long pos : sampling_positions) {
            long pos_byte = pos * sizeof(T);
            infile.seekg(pos_byte);
            for (unsigned long i = 0; i < sample_length * sizeof(T); i++) {
                char c;
                if (infile.get(c))
                    outfile.put(c);
            }
        }
    }

    void work(bool quiet_mode) {
        this->gen_sampling_positions();
        this->fix_overlaps();
        if (!quiet_mode) {
            long long total_sampled_symbols = n_samples * sample_length;
            double sample_percentage = 100 * total_sampled_symbols / (double) infilesize_symbols;
            cerr << "generated " << total_sampled_symbols << " samples, " << std::fixed << std::setprecision(2) << sample_percentage << " % of input\n";
        }
        this->write_output();
    }

};


void print_help() {
    cerr << "builddict: randomly sample input for use as an RLZ dictionary.\n"
            "Usage: builddict [options] input_file [-o output_file]\n"
            "With no output file, output is written to input_file.dict.\n"
            "Options:\n"
            "  -n, --num-samples N    Default " << DEFAULT_N_SAMPLES << " samples.\n"
            "  -l, --sample-length L  Default " << DEFAULT_N_SAMPLES << " symbols per sample.\n"
            "  -w, --width W          Bits per symbol, allowed values: 8, 16, 32, 64.\n"
            "  -s, --random-seed S\n"
            "(builddict version " VERSION_STRING ", " DATE_STRING ")\n";
}


int main(int argc, char* argv[]) {
    // Default options
    int symbol_width_bits = 8; // working in uint8s by default
    unsigned int n_samples = DEFAULT_N_SAMPLES;
    unsigned int sample_length = DEFAULT_SAMPLE_LENGTH;
    int seed = DEFAULT_SEED;
    string input_file_name = "";
    string output_file_name = "";
    bool quiet_mode = false;

    if (argc <= 1) {
        print_help();
        exit(127);
    }

    /* Argument parsing *****/
    int i = 1;
    while (i < argc) {
        string arg_i = string(argv[i]);
        if (arg_i.compare("--help") == 0) {
            print_help();
            exit(0);
        } else if (arg_i.compare("-q") == 0 || arg_i.compare("--quiet") == 0) {
            quiet_mode = true;
        } else if (arg_i.compare("--num-samples") == 0 || arg_i.compare("-n") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no number after --num-samples" << endl;
                exit(127);
            }
            i++;
            n_samples = atoi(argv[i]);
        } else if (arg_i.compare("--sample-length") == 0 || arg_i.compare("-l") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no number after --sample-length" << endl;
                exit(127);
            }
            i++;
            sample_length = atoi(argv[i]);
        } else if (arg_i.compare("--random-seed") == 0 || arg_i.compare("-s") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no number after --sample-length" << endl;
                exit(127);
            }
            i++;
            seed = atoi(argv[i]);
        } else if (arg_i.compare("--width") == 0 || arg_i.compare("-w") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no number after --width" << endl;
                exit(127);
            }
            i++;
            symbol_width_bits = atoi(argv[i]);
            if ((symbol_width_bits != 8) && (symbol_width_bits != 16) && (symbol_width_bits != 32) && (symbol_width_bits != 64)) {
                cerr << "Bad arguments: --width wasn't 8, 16, 32 or 64" << endl;
                exit(127);
            }
        } else if (arg_i.compare("--outfile") == 0 || arg_i.compare("-o") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no filename after --outfile" << endl;
                exit(127);
            }
            i++;
            output_file_name = string(argv[i]);
        } else if (arg_i.compare("--infile") == 0 || arg_i.compare("-i") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no filename after --infile" << endl;
                exit(127);
            }
            i++;
            input_file_name = string(argv[i]);
        } else {
            if (input_file_name.length() != 0) {
                cerr << "Bad arguments: input file name already specified" << endl;
                exit(127);
            } else {
                input_file_name = string(argv[i]);
            }
        }
        i++;
    }

    srand(seed);

    if (input_file_name.length() == 0) {
        cerr << "Bad arguments: input file name not specified\n";
        exit(127);
    } else {
        if (output_file_name.length() == 0) {
            output_file_name = input_file_name + ".dict";
        }
    }

    if (n_samples <= 0) {
        cerr << "Bad arguments: number of samples needs to be at least 1\n";
        exit(127);
    }
    if (sample_length <= 0) {
        cerr << "Bad arguments: sample length needs to be at least 1\n";
        exit(127);
    }

    if (!quiet_mode) {
        cerr << input_file_name << " / n= " << n_samples << " l= "
             << sample_length << " (" << std::fixed << std::setprecision(2);
        // denominator is guaranteed to be >= 1 by argument parsing
        if (n_samples < sample_length) {
            double l_per_n = (double) sample_length / (double) n_samples;
            cerr << "1:" << l_per_n;
        } else if (n_samples > sample_length) {
            double n_per_l = (double) n_samples / (double) sample_length;
            cerr << n_per_l << ":1";
        } else {
            cerr << "1:1";
        }
        cerr << ") -> " << output_file_name << "\n";
        if (seed != DEFAULT_SEED) cerr << "seed = " << seed << "\n";
    }

    switch (symbol_width_bits) {
        case 8:
        {
            DictionaryGenerator<uint8_t> dg = DictionaryGenerator<uint8_t>(input_file_name, output_file_name, n_samples, sample_length);
            dg.work(quiet_mode);
            break;
        }
        case 16:
        {
            DictionaryGenerator<uint16_t> dg = DictionaryGenerator<uint16_t>(input_file_name, output_file_name, n_samples, sample_length);
            dg.work(quiet_mode);
            break;
        }
        case 32:
        {
            DictionaryGenerator<uint32_t> dg = DictionaryGenerator<uint32_t>(input_file_name, output_file_name, n_samples, sample_length);
            dg.work(quiet_mode);
            break;
        }
        case 64:
        {
            DictionaryGenerator<uint64_t> dg = DictionaryGenerator<uint64_t>(input_file_name, output_file_name, n_samples, sample_length);
            dg.work(quiet_mode);
            break;
        }
        default:
            error_die("Unknown symbol width " + std::to_string(symbol_width_bits));
    }

    if (!quiet_mode) {
        long long total_output_syms = n_samples * sample_length;
        int bytes_per_symbol = symbol_width_bits / 8;
        long long total_output_bytes = total_output_syms * bytes_per_symbol;
        if (bytes_per_symbol == 1) {
            cerr << "builddict done, wrote " << total_output_bytes << " bytes\n";
        } else {
            cerr << "builddict done, wrote " << total_output_syms << " symbols, " << total_output_bytes << " bytes\n";
        }
    }

    return 0;
}


