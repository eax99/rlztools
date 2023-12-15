/* SPDX-License-Identifier: MPL-2.0
 *
 * Copyright 2023 Eve Kivivuori
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* rlzparse: Relative Lempel-Ziv compressor
 *
 * Basic usage:
 * rlzparse input_file -d dictionary_file -s suffix_array_file [-o output_file]
 * Output file specification is optional; the default is "input_file.rlz".
 *
 * Suffix array file is to be created by a separate utility, and by
 * default it's assumed to be just a series of 32-bit-wide integers
 * in machine default byte order (probably little-endian on a PC);
 * for big dictionaries a 64-bit-wide SA can be specified with '-W 64'.
 *
 * Input and dictionary are processed as 8-bit bytes by default;
 * 16, 32 and 64-bit-wide units are available with '-w 16', '-w 32', '-w 64'.
 *
 * Output is a plain file without metadata, by default with two 32-bit
 * integers per token (again, machine endianness), the first integer being
 * offset and the second being length. A two-times-64-bit output is available
 * with '-f 64x2', as is a textual ASCII format for the curious (-f ascii).
 * An efficient variable-byte encoding (basically LEB128) is available
 * with '-f vbyte'.
 */

/* Very quick changelog summary:
 * v0.6: first "release"
 * v0.7: input buffering, cmdline interface changes ("-i" is allowed now)
 * v0.7.1: removed manually-done input buffering, relying on ifstream's buffers
 * v0.7.2: printout changes: less verbose now. new flags: "-q", "--progress".
 *         '-f ascii' output is now in decimal.
 * v0.8: support for variable-byte output encoding
 */

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <chrono>
// Defines RLZToken and FileReader.
#include "rlzcommon.h"

#ifndef VERSION_STRING
#define VERSION_STRING "0.8.1"
#endif
#ifndef DATE_STRING
#define DATE_STRING "December 2023"
#endif

// use `xxd -g4 -e file.rlz` to examine binary output

// if asked for with --progress, print a message this many milliseconds
#define PROGRESS_PRINT_INTERVAL_MS 5000

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;
using std::ifstream;
using std::ofstream;
using wall_clock = std::chrono::system_clock;
using std::chrono::milliseconds;

/* Literal tokens, for symbols in the input that aren't in the dictionary,
 * are output as a token which has the symbol in the start_pos field and
 * a length field of zero. This works for our purposes because a length of
 * zero is otherwise nonsensical: why would one copy zero bytes from the
 * dictionary? */

void print_help()
{
    cerr << "rlzparse: compress with the Relative Lempel-Ziv algorithm.\n"
            "Usage: rlzparse [options] -i INFILE -d DICTIONARY -s SUFFIX_ARRAY [-o OUTFILE]\n"
            "Input is compressed against a dictionary and a suffix array of that dictionary;\n"
            "the suffix array is a list of 32-bit binary ints in machine-native byte order.\n"
            "Options:\n"
            "  -w, --width 8/16/32/64    Process input and dictionary as 8/16/32/64-bit\n"
            "                            units; the default is 8-bit=one-byte symbols.\n"
            "  -W, --sa-width 32/64      Use 32- or 64-bit integers in the suffix array.\n"
            "  -f, --output-fmt 32x2/64x2/ascii/vbyte\n"
            "                            Different output formats, default=32x2.\n"
            "                            32x2 and 64x2 are pairs of binary integers.\n"
            "                            ascii is two space-separated numbers per line.\n"
            "                            vbyte is an efficient variable-width byte encoding.\n"
            "With no OUTFILE specified, output is written to 'INFILE.rlz'.\n"
            "Also accepted are --dictionary, --suffix-array, --output instead of -d, -s, -o.\n"
            "Other options: -q/--quiet (no output unless an error occurs),\n"
            "               --progress (periodically print out a progress counter)\n"
            "(rlzparse version " VERSION_STRING ", " DATE_STRING ")\n";
}

void error_die(string msg)
{
    cerr << msg << endl;
    exit(EXIT_BUG);
}

/* A warning avoidance function safer than a plain cast: negatives are
 * turned to zero rather than wrapping around into the quintillions. */
unsigned long long unsign(long long i)
{
    if (i < 0) return 0UL;
    return (unsigned long long) i;
}



// Returns > 0 if the end token hasn't been seen yet, 0 if it's time to stop.
// (Returned value is the token length in symbols, or 1 if it was a literal.)
// Adds the number of bytes that are output to *bytes_output.
// Does not check that all lengths are nonnegative -- they should be, anyway.
unsigned long output_token(
        RLZToken token, std::ostream* out,
        int output_mode, uint64_t* bytes_output)
{
    if (is_end_sentinel(&token)) {
        out->flush();
        return 0; // end token seen
    }
    int64_t length_copy = token.length;
    switch (output_mode) {
        case FMT_32X2: {
            char bytebuf[8];
            uint32_t* intbuf = reinterpret_cast<uint32_t*>(bytebuf);
            intbuf[0] = (uint32_t) token.start_pos;
            intbuf[1] = (uint32_t) token.length;
            // ostream::write doesn't work because it needs const
            for (int i = 0; i < 8; i++) out->put(bytebuf[i]);
            *bytes_output += 8;
            break;
        }
        case FMT_64X2: {
            char bytebuf[16];
            uint64_t* intbuf = reinterpret_cast<uint64_t*>(bytebuf);
            intbuf[0] = (uint64_t) token.start_pos;
            intbuf[1] = (uint64_t) token.length;
            for (int i = 0; i < 16; i++) out->put(bytebuf[i]);
            *bytes_output += 16;
            break;
        }
        case FMT_ASCII: {
            string outstring = std::to_string(token.start_pos) + " "
                               + std::to_string(token.length) + "\n";
            (*out) << outstring;
            *bytes_output += outstring.length();
            break;
        }
        case FMT_VBYTE: {
            char bytebuf[20]; // vbyte-encoded 64-bit ints need at most 10 bytes
            int bufptr = 0;
            if (token.start_pos == 0)
                bytebuf[bufptr++] = 0;
            while (token.start_pos > 0) {
                if (token.start_pos <= 127) {
                    bytebuf[bufptr++] = (char) token.start_pos;
                } else {
                    char low_7 = token.start_pos & 0x7F;
                    bytebuf[bufptr++] = low_7 | 0x80;
                }
                token.start_pos = token.start_pos >> 7;
            }
            if (token.length == 0)
                bytebuf[bufptr++] = 0;
            while (token.length > 0) {
                if (token.length <= 127) {
                    bytebuf[bufptr++] = (char) token.length;
                } else {
                    char low_7 = token.length & 0x7F;
                    bytebuf[bufptr++] = low_7 | 0x80;
                }
                token.length = token.length >> 7;
            }
            for (int i = 0; i < bufptr; i++)
                out->put(bytebuf[i]);
            *bytes_output += bufptr;
            break;
        }
        default: {
            cerr << "bug: no output handler in output_token for mode 0x" << std::hex << output_mode << std::dec << endl;
            exit(EXIT_BUG);
        }
    }
    return length_copy > 0 ? length_copy : 1;
}


bool progress_msgs_initialized = false;
wall_clock::time_point prev_print_time;
long long pos_at_last_printout = 0;
// Prints progress bar if the progress bar printout time is up.
// Assumes that cur_pos is 0-indexed, so its 100% value is max_pos-1.
void print_progress(string filename, long long cur_pos, long long max_pos,
                    bool force_print)
{
    if (!progress_msgs_initialized) {
        prev_print_time = wall_clock::now();
        progress_msgs_initialized = true;
        force_print = true;
    }
    wall_clock::time_point now = wall_clock::now();
    milliseconds time_elapsed = std::chrono::duration_cast<milliseconds>(now - prev_print_time);
    if (time_elapsed.count() >= PROGRESS_PRINT_INTERVAL_MS || force_print) {
        double progress_percent = (cur_pos + 1) * 100 / (double) max_pos;
        long long bytes_processed = cur_pos - pos_at_last_printout;
        long long bps = bytes_processed * 1000 / PROGRESS_PRINT_INTERVAL_MS;
        string rate_string = std::to_string(bps) + " B/s";
        if (bps > 9999 && bps <= 9999999) {
            string s = std::to_string((double) bps / 1000);
            rate_string = s.substr(0, s.length() - 3) + " kB/s";
        } else if (bps > 9999999) { // 10 MB - 1
            string s = std::to_string((double) bps / 1000000);
            rate_string = s.substr(0, s.length() - 3) + " MB/s";
        }
        cerr << "\r" << filename << ": "
             << std::fixed << std::setprecision(2) << progress_percent
             << "%  " << rate_string << " "; // no newline on purpose
        prev_print_time = now;
        pos_at_last_printout = cur_pos;
    }
}


/* The suffix array & dictionary file readers are hidden inside templated
 * classes, because they both need to be held in memory while the parser runs,
 * and so they need to be in the correct type -- an integer of some width,
 * probably 32 for the SA and 8 in most cases for the dictionary, and C++'s
 * type system isn't flexible enough (as far as I can tell) to allow for
 * a generic integer of some length, determined solely by user input, without
 * specifying all cases exactly, which is tedious, but this containerization
 * helps.
 * (The SA needs to actually be cast into integers, but the dictionary could
 * remain as a series of bytes -- as long as we remain very careful with
 * alignment -- might this be easier?)
 *
 * The classes that do the file reading are in filereader.h.
 *
 * T is the type of the symbols of our dictionary and input file, while
 * S is the type of the symbols of the suffix array file.
 */
template <typename T, typename S> class Parser {
    long long dict_size;
    long long sa_size;

    /* We need our own buffer, because if we're reading in symbols of e.g.
     * four bytes width, we can't unget symbols straight back into the
     * ifstream because ifstream doesn't guarantee more than one _byte_ of
     * unget capability. */
    T unget_buffer;
    bool has_unget;
    ifstream source_file;
    long long source_file_size_symbols;
    long long read_counter;

    bool print_progress_messages;
    bool print_interval_message_printed; // not needed any more?
    long long input_file_size; // used for calls to print_progress()
    string input_file_name; // used for calls to print_progress()

public:
    FileReader<T> dict;
    FileReader<S> sa;

    Parser(string input_file_name, string dict_file_name, string sa_file_name,
           bool verbose)
        : dict(dict_file_name, verbose), sa(sa_file_name, verbose)
    {
        dict_size = dict.size();
        sa_size = sa.size();

        source_file = ifstream(input_file_name, ifstream::binary);
        if (!source_file) error_die("Error: cannot open input file " + input_file_name);
        has_unget = false;
        long long source_file_size_bytes = file_size(&source_file);
        input_file_size = source_file_size_bytes;
        source_file_size_symbols = source_file_size_bytes / sizeof(T);
        long long test = source_file_size_symbols * sizeof(T);
        if (test != source_file_size_bytes) {
            cerr << "Warning: input file size is indivisible by " << sizeof(T) << "; output will ignore extra bytes.\n";
        }
        read_counter = 0;
        //print_interval_message_printed = false;
        print_progress_messages = verbose;
        this->input_file_name = input_file_name;
    }

    long long dict_size_bytes()
    {
        return dict.size() * sizeof(T);
    }

    /* Token finder: using the dictionary and the suffix array, finds the
     * longest occurrence of a prefix of the source text in the dictionary.
     *
     * IF A SYMBOL ISN'T IN THE DICTIONARY:
     * outputs the symbol itself for the position and 0 for the length. */
    RLZToken next_token()
    {
        RLZToken token;
        token.start_pos = ULLONG_MAX;
        token.length = -1LL;

        // Storing the best partial match so far.
        // Position is stored unsigned because of literals.
        uint64_t best_pos = 0LL;
        int64_t best_len = 0LL;
        bool matching_suffix_found = false;

        // These bound the SA search range, and are indices into the SA.
        int64_t leftmost = 0, rightmost = sa_size - 1;

        /* This has a twofold meaning:
         * it's the length of the substring we've encoded so far, i.e.
         * the number of symbols we've read from the file while creating
         * the next token (minus one), and it's also the number of symbols
         * we need to skip over when searching for strings in the suffix
         * list, implicitly constructed from the suffix array. */
        long long offset = 0;
        T c = this->getnext();

        while (read_counter <= source_file_size_symbols) {

            if (this->end_of_input()) {
                // Output the special end sentinel
                return end_sentinel;
            }

            leftmost = search_left(c, offset, leftmost, rightmost);

            /* A very common case: either there is no suffix matching the
             * current character because the character doesn't exist in the
             * dictionary (in case we return a literal token),
             * or we were leftward searching for a longer suffix than the
             * longest one we already have, but there are none.
             *
             * Example: the current suffix is CDEFXYZ... and our offset is 4
             * so we're comparing suffixes against character 'X'. The work
             * we've done so far has given us a range of suffixes that all
             * start with "CDEF"; the matching dictionary suffixes could be,
             * for example, "CDEFA...", "CDEFF...", "CDEFG...", "CDEFZ".
             * None of these offset=4 characters (A, F, G, Z) match X, so the
             * leftmost returned by search_left will be negative, while
             * best_pos will point to one of those suffixes (prob. CDEFA...)
             * and best_len will be 4.
             *
             * Keep in mind that leftmost and rightmost are boundaries for
             * a range of possible suffixes, and each time we run this loop
             * we're moving leftmost to the right and rightmost to the left,
             * and so if leftmost is < 0 that indicates that "I simply cannot
             * give you any bounds for this substring you've given me,
             * because your substring doesn't start any suffixes".
             */
            if (leftmost < 0) {
                if (matching_suffix_found) {
                    /* We already have a partial suffix we can return, so push
                     * the extra unmatched character we already read back so
                     * that the next next_token call can start with it. */
                    token.start_pos = unsign(sa[best_pos]);
                    token.length = !matching_suffix_found ? 0 : best_len;
                    this->unget(c);
                } else {
                    /* The symbol we have doesn't occur in the dictionary at
                     * all, so encode a literal and return it. */
                    token.start_pos = (uint64_t) c;
                    token.length = 0;
                }
                return token;
            }

            auto old_rightmost = rightmost; // only needed for a debug message
            rightmost = search_right(c, offset, leftmost, rightmost);

            /* Like the leftward search case, we were looking to move the right
             * boundary of our range of suffixes leftward, but this isn't
             * possible for our current substring: there's no suffix for which
             * which its offset'th character equals c.
             * However, if the input data is sane (= the suffix array describes
             * an actual, valid, sorted suffix array; this can mess up if
             * widths get confused (an SA calculated for 8-bit data while input
             * input is 32-bit, for example)) then rightmost should never be
             * negative.
             * To illustrate why, let's use the same example data from before:
             * offset = 4, the current suffix of input we're looking at is
             * CDEFXYZ..., and so far we've found four suffixes matching the
             * first four characters:
             *
             *            leftmost|              |rightmost
             * i:     ...  14  15 |16  17  18  19| 20  21  22  23 ...
             * SA[i]: ...  93  31 |94  32  73  25| 95  33  74  26 ... offset:
             * Dict[SA]:   C   C  |C   C   C   C | D   D   D   D  ---- 0
             *             C   C  |D   D   D   D | E   E   E   E  ---- 1
             *             D   D  |E   E   E   E | F   F   F   F  ---- 2
             *             E   E  |F   F   F   F | A   F   G   Z  ---- 3
             *             F   F  |A   F   G   Z | $   .   .   .  ---- 4
             *             A   Z  |$   .   .   . |     .   .   .  ---- 5
             *             $   .  |    .   .   . |     .   .   .  ---- 6
             * At the start of this iteration of the while loop, leftmost has
             * been set to 16 (matching SA[16]=94, matching "CDEFA...") and
             * rightmost has been set to 19 (matching SA[19]=25, matching
             * "CDEFZ...").
             *
             * The current character is 'X', and it doesn't occur at offset 4
             * in the range of suffixes we're restricted to. search_left works
             * by starting with L = leftmost, then moving L in a binary search
             * fashion between leftmost and rightmost, until it either finds
             * the leftmost L where character Dict[SA[L]+offset] == 'X', or a
             * negative value if no such suffix exists, meaning there's no
             * suffix in the range with the offset'th = 4th character = 'X'.
             * In that case this function is never run, because we've already
             * returned a token in the if above.
             *
             * Let's change the suffix a bit: the suffix will be CDEFGHI...,
             * and offset will still be 4. In this case search_left will return
             * a new leftmost = 18, the if block will not be run, and we'll
             * head onto the search for a rightmost character. It works much
             * the same way: it starts with R = rightmost and binary searches
             * leftward, with leftmost as a hard bound.
             * **Assuming the suffix array is correct this should never fail**:
             * in the worst case there is only one suffix (SA[18]) that matches
             * the current character, and so the new rightmost will be 18.
             * If this function returns a not-found, it means that the binary
             * search in search_right failed. The binary search cannot fail,
             * unless the suffixes aren't ordered: if suffix SA[19] was instead
             * "CDEFA..." a failure would be understandable, because the search
             * looks at the character 'A' and determines, correctly, that
             * 'G' > 'A' and therefore index of 'G' > 19.
             *
             * How can this happen? It can happen easily with mismatched
             * widths: if input data is 32-bit, then a suffix array calculated
             * assuming that the input is 8-bit will produce out-of-order
             * suffixes, unless careful pre- and postprocessing is done to
             * produce a still-valid suffix array.
             * (Preprocessing is ensuring that the input data is big-endian,
             * and postprocessing removes all suffixes that don't point to a
             * suffix starting at a 32-bit boundary & dividing the rest by 4.)
             * It can also happen if this program has a bug, like a wrong kind
             * of type, processing 64-bit data as 32-bit and losing half of
             * each word.
             */
            if (rightmost < 0) {
                cerr << "Error: failed binary search. Check your flags and your suffix array input;\nmaybe you forgot a --width flag, or skipped some suffix array processing?\nDebug: search_right(c=" << std::hex << std::showbase << (uint64_t) c << " offset=" << offset << " leftmost=" << leftmost << " rightmost=" << old_rightmost << ") retval=" << (int64_t) rightmost << " match_found=" << matching_suffix_found << " best_pos=" << best_pos << " best_len=" << best_len << std::dec << "\n";
                exit(EXIT_BUG);
            } else {
                /* Bounds were successfully shrunk, so update our best known
                 * partial suffix and length.
                 * length + 1 because strings are zero-indexed. */
                best_len = offset + 1;
                best_pos = leftmost;
                matching_suffix_found = true;
            }

            /* We're at the one suffix that matches the substring
             * we have so far. Keep looking at how far we can take it. */
            if (leftmost == rightmost) {
                //cerr << "leftmost == rightmost\n"; /* to be deleted */
                // Get the start of the one suffix...
                S token_start_pos = sa[leftmost];
                while (read_counter <= source_file_size_symbols) {
                    // ...and get the next symbol along it.
                    T dict_sym_here = dict[token_start_pos + offset];
                    if (c != dict_sym_here) {
                        /* A mismatch: we now know how long the suffix is. */
                        this->unget(c);
                        token.start_pos = token_start_pos;
                        token.length = offset;
                        return token;
                    }
                    c = this->getnext();
                    offset++;
                }
                /* The file ends here, and we know that the suffix we looked
                 * at is good up to the very last symbol of the file. We know
                 * this, because if there was a mismatch, even at the very
                 * last symbol, the 'if (c != dict_sym_here)' block would be
                 * run. The offset is just right, also: normally, we'd need
                 * to decrement it (because ->getnext() got an EOF and so
                 * 'offset' would point to a character past the end of the
                 * suffix), but we also need to increment it because that's
                 * how the data format works (strings are zero-indexed but
                 * we store their length), so it cancels out.
                 * next_token() will be run one more time, in order to return
                 * the sentinel token that marks the end of input. */
                token.start_pos = sa[leftmost];
                token.length = offset;
                return token;
            }

            offset++;
            c = this->getnext();
            /* It's not obvious why this check is required here: wouldn't this
             * case have been handled in the leftmost==rightmost block, or
             * wouldn't it suffice to leave this for the next iteration of
             * the loop? Not necessarily: leftmost==rightmost only runs when
             * there's only one matching suffix, and if we do nothing here then
             * offset++ will cause the next iteration to be skipped and we'll
             * run into the (true) source_file.eof() below, with the end
             * sentinel returned. There's also always a good token for us to
             * return here: literals (no suffix match) would've happened way
             * back in the search_left check. */
            if (this->end_of_input()) {
                token.start_pos = sa[leftmost];
                token.length = offset; // no need to increment again
                return token;
            }
        }
        /* Pondering:
         * Being here means: we ->getnext()'ed a character, and that was the
         * _last_ character, so the 'read_counter < source_file_size_symbols'
         * condition I used to have fails, and we have to do a final round of
         * string comparison.
         * However, we only need to do a very partial comparison:
         * the 'leftmost == rightmost' conditional takes care of cases where
         * there's only one suffix that matches the end of the file, but
         * here we're in a situation where there is more than one matching
         * suffix -- however, matching to all but the last character,
         * so we would need to check the offset'th character of the remaining
         * suffixes.
         * Changing the < to <= took care of that, but this code still runs,
         * on the final iteration that's supposed to return the end sentinel.
         * Return it, but also verify that the file is actually finished.
         */
        if (this->end_of_input()) {
            // Natural end of parsing.
            return end_sentinel;
        } else {
            cerr << "Error (bug): outside token-finding loop\n";
            cerr << "offset " << std::dec << offset
                 << ", read_counter " << read_counter
                 << ", source_file_size_symbols " << source_file_size_symbols
                 << ", left " << leftmost << ", right " << rightmost
                 << ", char=" << c
                 << ", eof=" << (source_file.eof() ? "yes" : "no") << endl;
            exit(EXIT_BUG);
        }
    }


    void work(std::ostream* outfile, int output_mode, uint64_t* longest_token,
              uint64_t* num_tokens, uint64_t* bytes_input,
              uint64_t* bytes_output)
    {
        if (print_progress_messages) cerr << "Starting parsing...\n";
        uint64_t keep_going = 1;
        while (keep_going > 0) {
            RLZToken token = this->next_token();
            keep_going = output_token(token, outfile, output_mode, bytes_output);
            if (keep_going > *longest_token)
                *longest_token = keep_going;
            if (keep_going > 0)
                *bytes_input += token.length == 0
                                ? sizeof(T)
                                : token.length * sizeof(T);
            (*num_tokens)++;
            if (print_progress_messages)
                print_progress(input_file_name, *bytes_input, input_file_size,
                               keep_going == 0); // force printout at 100%
        }
    }


private:
    T getnext()
    {
        if (has_unget) {
            has_unget = false;
            read_counter++;
            return unget_buffer;
        }
        T buf[1];
        buf[0] = 0;
        source_file.read(reinterpret_cast<char *>(buf), sizeof(T));
        /* In cases where T is N>1 bytes wide and the input isn't a multiple
         * of N, the buffer will be filled with the leftover bytes and both
         * eofbit and failbit are set.
         * We don't check for that here -- there are only a couple of places
         * where ->getnext() is called, and in all of those but one there's
         * a natural eof check in the next iteration of the loop. */
        read_counter++;
        return buf[0];
    }

    bool end_of_input()
    {
        return source_file.eof() && !has_unget;
    }

    void unget(T sym)
    {
        unget_buffer = sym;
        read_counter--;
        has_unget = true;
    }

    /* The 'offset' parameter is an index to the string we're searching:
     * if we're trying to tokenize the string "string", and we've already
     * the first and last suffix in the SA that begin with 's', we'd set
     * 'offset' to 1 and search for those suffixes that begin with "st";
     * the suffixes that begin with "st" are entirely a subset of the
     * suffixes that begin with "s", so they'll be in the range given by
     * old_left_bound and right_bound, if they exist. */
    long long search_left(T text_symbol, int offset, long long old_left_bound, long long right_bound)
    {
        long long left = old_left_bound, right = right_bound;
        while (left <= right) { // safe cutoff condition?
            long long mid = (left + right) / 2;
            if (sa[mid] + offset >= unsign(dict.size())) {
                // End of string: the dictionary, & thus the suffix, ends here.
                // Traditionally the end-of-string "character" sorts lower
                // than any symbol in the alphabet, so this is equivalent to
                // the mid_symbol < text_symbol case below.
                left = mid + 1;
                continue;
            }
            T mid_symbol = dict[sa[mid] + offset];
            if (mid_symbol < text_symbol) {
                left = mid + 1;
            } else if (mid_symbol > text_symbol) {
                right = mid - 1;
            } else {
                if (mid == old_left_bound) {
                    // At the leftmost occurrence of the key
                    return mid;
                }
                if (sa[mid - 1] + offset >= unsign(dict.size())) {
                    // The suffix sorted right before mid is at the end of the
                    // dictionary, ending with the end-of-string symbol.
                    // Implications: mid_minus_one != mid_symbol, therefore the
                    // if-else below would take the else branch.
                    return mid;
                }
                // This can't underflow, because of the previous if
                T mid_minus_one = dict[sa[mid - 1] + offset];
                if (mid_minus_one == mid_symbol) {
                    right = mid - 1; // discard mid and everything to its right
                } else {
                    return mid; // leftmost occurrence of key found
                }
            }
        }
        return -(left + 1); // key not found
    }

    long long search_right(T text_symbol, int offset, long long left_bound, long long old_right_bound)
    {
        long long left = left_bound, right = old_right_bound;
        while (left <= right) { // safe cutoff condition?
            long long mid = (left + right) / 2;
            if (sa[mid] + offset >= unsign(dict.size())) {
                // End of dictionary, end of suffix, sorts lower than any symbol.
                // No need to update right: this is a special case of the
                // mid_symbol < text_symbol case.
                left = mid + 1;
                continue;
            }
            T mid_symbol = dict[sa[mid] + offset];
            if (mid_symbol < text_symbol) {
                left = mid + 1;
            } else if (mid_symbol > text_symbol) {
                right = mid - 1;
            } else {
                if (mid == old_right_bound) {
                    // At the rightmost occurrence of the key
                    return mid;
                }
                if (sa[mid + 1] + offset >= unsign(dict.size())) {
                    // The suffix sorted right afer mid is at the end of the
                    // dictionary, ending with the end-of-string symbol.
                    // Implications: mid_plus_one != mid_symbol, therefore the
                    // if-else below would take the else branch.
                    return mid;
                }
                T mid_plus_one = dict[sa[mid + 1] + offset];
                if (mid_plus_one == mid_symbol) {
                    left = mid + 1; // discard mid and everything to its left
                } else {
                    return mid; // rightmost occurrence of key found
                }
            }
        }
        return -(right - 1); // key not found
    }

};


// Only for testing purposes, and only for character data.
void print_token(RLZToken token, FileReader<uint8_t>* dr)
{
    if (token.length == 0) {
        cout << (uint8_t) token.start_pos;
    } else {
        for (int i = 0; i < token.length; i++) {
            uint8_t c = (*dr)[token.start_pos + i];
            if (c == '\n') { cout << "\\n";
            } else if (c == '\r') { cout << "\\r";
            } else if (c == '\t') { cout << "\\t";
            } else if (c == '\\') { cout << "\\\\";
            } else if (c < ' ' || c > '~') {
                // Output other non-printables and \ as octal escapes
                cout << '\\' << std::setw(3) << std::setfill('0') << std::oct << (int) c << std::setw(0);
            } else {
                cout << c;
            }
        }
    }
}


int main(int argc, char **argv) {
    if (argc <= 1) {
        print_help();
        exit(EXIT_USER_ERROR);
    }

    // Defaults
    int symbol_width_bits = 8;
    int sa_symbol_width_bits = 32;
    string input_file_name = "";
    string dict_file_name = "";
    string output_file_name = "";
    string sa_file_name = "";
    string output_format = "";
    unsigned int output_mode = FMT_32X2;
    //bool output_to_stdout = false; /* planned optional feature, but it's complicated */
    bool quiet_mode = false;
    bool progress_messages = false;

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
            i++;
            dict_file_name = string(argv[i]);
        } else if (arg_i.compare("-s") == 0 || arg_i.compare("--sa") == 0 || arg_i.compare("--suffix-array") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no filename after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            i++;
            sa_file_name = string(argv[i]);
        } else if (arg_i.compare("-w") == 0 || arg_i.compare("--width") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no width after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            i++;
            symbol_width_bits = atoi(argv[i]);
            if ((symbol_width_bits != 8) && (symbol_width_bits != 16) && (symbol_width_bits != 32) && (symbol_width_bits != 64)) {
                cerr << "Bad arguments: width wasn't 8, 16, 32, or 64" << endl;
                exit(EXIT_USER_ERROR);
            }
        } else if (arg_i.compare("-W") == 0 || arg_i.compare("--sa-width") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no width after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            i++;
            sa_symbol_width_bits = atoi(argv[i]);
            if ((sa_symbol_width_bits != 32) && (sa_symbol_width_bits != 64)) {
                cerr << "Bad arguments: SA symbol width wasn't 32 or 64" << endl;
                exit(EXIT_USER_ERROR);
            }
        } else if (arg_i.compare("-f") == 0 || arg_i.compare("--output-fmt") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no output format after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            i++;
            output_format = string(argv[i]);
        } else if (arg_i.compare("-o") == 0 || arg_i.compare("--outfile") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no filename after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            i++;
            output_file_name = string(argv[i]);
        } else if (arg_i.compare("-i") == 0 || arg_i.compare("--infile") == 0) {
            if (argc < i + 2) {
                cerr << "Bad arguments: no filename after " << arg_i << endl;
                exit(EXIT_USER_ERROR);
            }
            i++;
            input_file_name = string(argv[i]);
        } else if (arg_i.compare("-q") == 0 || arg_i.compare("--quiet") == 0) {
            quiet_mode = true;
        } else if (arg_i.compare("--progress") == 0) {
            progress_messages = true;
        } else {
            if (input_file_name.length() != 0) {
                cerr << "Bad arguments: input file name already specified, or unknown parameter '" << arg_i << "' (specify output file with -o)" << endl;
                exit(EXIT_USER_ERROR);
            } else {
                input_file_name = string(argv[i]);
            }
        }
        i++;
    }

    if (input_file_name.length() == 0) {
        cerr << "Bad arguments: input file name not specified" << endl;
        exit(EXIT_USER_ERROR);
    }

    if (dict_file_name.length() == 0) {
        cerr << "Bad arguments: dictionary file name not specified" << endl;
        exit(EXIT_USER_ERROR);
    }

    if (sa_file_name.length() == 0) {
        cerr << "Bad arguments: suffix array file name not specified" << endl;
        exit(EXIT_USER_ERROR);
    }

    // Autogenerate output file name, or output to stdout.
    if (output_file_name.length() == 0) {
        output_file_name = input_file_name + string(".rlz");
    }

    if (output_format.length() == 0) {
        output_mode = FMT_32X2;
        output_format = "32x2";
    } else if (output_format.compare("32x2") == 0) {
        output_mode = FMT_32X2;
    } else if (output_format.compare("64x2") == 0) {
        output_mode = FMT_64X2;
    } else if (output_format.compare("ascii") == 0) {
        output_mode = FMT_ASCII;
    } else if (output_format.compare("vbyte") == 0) {
        output_mode = FMT_VBYTE;
    } else {
        cerr << "Bad arguments: output format not \"32x2\", \"64x2\", \"ascii\" or \"vbyte\".\n";
        exit(EXIT_USER_ERROR);
    }

    std::ofstream tmpval = ofstream(output_file_name, ofstream::binary | ofstream::trunc);
    std::ofstream* outfile = &tmpval; // can't reference an rvalue, hence the tmpval

    if (!quiet_mode) {
        string ifmt = "", ofmt = "", sfmt = "";
        if (symbol_width_bits != 8)
            ifmt = " (" + std::to_string(symbol_width_bits) + "-bit)";
        if (output_mode != FMT_32X2)
            ofmt = " (" + output_format + ")";
        if (sa_symbol_width_bits != 32)
            sfmt = " (" + std::to_string(sa_symbol_width_bits) + "-bit)";
        cerr << "rlzparsing " << input_file_name << ifmt << " -> " << output_file_name << ofmt
             << "\nrlz dictionary: " << dict_file_name << " + " << sa_file_name << sfmt
             << "\n";
    }

    /* Sanity checks: these combinations of input options can't mix safely,
     * so warn about them. */
    if (output_mode == FMT_32X2 && symbol_width_bits == 64 && !quiet_mode) {
        cerr << "Warning: with --output-fmt 32x2 and --width 64 it's impossible for\nthe output file to contain literals. If you're ABSOLUTELY SURE the dictionary\ncontains every possible input symbol, no problem; otherwise set \"-f 64x2\".\n";
    }

    if (output_mode == FMT_32X2 && sa_symbol_width_bits == 64 && !quiet_mode) {
        cerr << "Warning: you've set --sa-width 64 and --output-fmt 32x2. If the dictionary\nactually has less than 2^32 symbols, no problem, but for bigger dictionaries\nyou will need --output-fmt 64x2 so that all addresses can be represented.\n";
    }

    cerr.flush();

    // Statistical variables, passed as reference to Parser.work().
    uint64_t longest_token = 1;
    uint64_t num_tokens = 0;
    uint64_t bytes_input = 0;
    uint64_t bytes_output = 0;
    uint64_t total_size_out = 0;
    // Strong typing :D
    // I haven't figured a clean way around this switch tree.
    switch (symbol_width_bits) {
    case 8: {
        switch (sa_symbol_width_bits) {
        case 32: {
            // Parser< input/dictionary width, suffix array width >
            Parser<uint8_t, uint32_t> parser = Parser<uint8_t, uint32_t>(input_file_name, dict_file_name, sa_file_name, progress_messages);
            parser.work(outfile, output_mode, &longest_token, &num_tokens, &bytes_input, &bytes_output);
            total_size_out = bytes_output + parser.dict_size_bytes();
            break;
        }
        case 64: {
            Parser<uint8_t, uint64_t> parser = Parser<uint8_t, uint64_t>(input_file_name, dict_file_name, sa_file_name, progress_messages);
            parser.work(outfile, output_mode, &longest_token, &num_tokens, &bytes_input, &bytes_output);
            total_size_out = bytes_output + parser.dict_size_bytes();
            break;
        }
        default:
            cerr << "bug in sa_symbol_width_bits switch (parent case 8), got " << sa_symbol_width_bits << "\n";
            exit(EXIT_BUG);
            break;
        }
        break;
    }
    case 16: {
        switch (sa_symbol_width_bits) {
        case 32: {
            Parser<uint16_t, uint32_t> parser = Parser<uint16_t, uint32_t>(input_file_name, dict_file_name, sa_file_name, progress_messages);
            parser.work(outfile, output_mode, &longest_token, &num_tokens, &bytes_input, &bytes_output);
            total_size_out = bytes_output + parser.dict_size_bytes();
            break;
        }
        case 64: {
            Parser<uint16_t, uint64_t> parser = Parser<uint16_t, uint64_t>(input_file_name, dict_file_name, sa_file_name, progress_messages);
            parser.work(outfile, output_mode, &longest_token, &num_tokens, &bytes_input, &bytes_output);
            total_size_out = bytes_output + parser.dict_size_bytes();
            break;;
        }
        default:
            cerr << "bug in sa_symbol_width_bits switch (parent case 16), got " << sa_symbol_width_bits << "\n";
            exit(EXIT_BUG);
            break;
        }
        break;
    }
    case 32: {
        switch (sa_symbol_width_bits) {
        case 32: {
            Parser<uint32_t, uint32_t> parser = Parser<uint32_t, uint32_t>(input_file_name, dict_file_name, sa_file_name, progress_messages);
            parser.work(outfile, output_mode, &longest_token, &num_tokens, &bytes_input, &bytes_output);
            total_size_out = bytes_output + parser.dict_size_bytes();
            break;
        }
        case 64: {
            Parser<uint32_t, uint64_t> parser = Parser<uint32_t, uint64_t>(input_file_name, dict_file_name, sa_file_name, progress_messages);
            parser.work(outfile, output_mode, &longest_token, &num_tokens, &bytes_input, &bytes_output);
            total_size_out = bytes_output + parser.dict_size_bytes();
            break;
        }
        default:
            cerr << "bug in sa_symbol_width_bits switch (parent case 32), got " << sa_symbol_width_bits << "\n";
            exit(EXIT_BUG);
            break;
        }
        break;
    }
    case 64: {
        switch (sa_symbol_width_bits) {
        case 32: {
            Parser<uint64_t, uint32_t> parser = Parser<uint64_t, uint32_t>(input_file_name, dict_file_name, sa_file_name, progress_messages);
            parser.work(outfile, output_mode, &longest_token, &num_tokens, &bytes_input, &bytes_output);
            total_size_out = bytes_output + parser.dict_size_bytes();
            break;
        }
        case 64: {
            Parser<uint64_t, uint64_t> parser = Parser<uint64_t, uint64_t>(input_file_name, dict_file_name, sa_file_name, progress_messages);
            parser.work(outfile, output_mode, &longest_token, &num_tokens, &bytes_input, &bytes_output);
            total_size_out = bytes_output + parser.dict_size_bytes();
            break;
        }
        default: {
            cerr << "bug in sa_symbol_width_bits switch (parent case 8), got " << sa_symbol_width_bits << "\n";
            exit(EXIT_BUG);
            break;
        }
        }
        break;
    }
    default: {
        cerr << "bug in symbol_width_bits switch tree, got " << symbol_width_bits << "\n";
        exit(EXIT_BUG);
    }
    }
    num_tokens -= 1; // the end sentinel is also counted, so discount it here.

    outfile->flush();

    if (!quiet_mode) {
        if (progress_messages) cerr << "\n";
        double compression_pct = total_size_out / (double) bytes_input * 100;
        double symbols_input = bytes_input / symbol_width_bits * 8;
        double avg_tok_len = symbols_input / num_tokens;
        cerr << "rlzparse: " << output_file_name << " done, "
             << std::dec << num_tokens << " tokens, " << bytes_output << " bytes\n";
        cerr << "mean token length " << std::fixed << std::setprecision(2)
             << avg_tok_len << " symbols, longest " << longest_token
             << ", out/in ratio " << compression_pct << "%\n";
    }

    return 0;
}


