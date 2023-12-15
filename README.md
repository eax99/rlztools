# rlztools: A Relative Lempel-Ziv data compressor, decompressor, and auxiliary utilities

The Relative Lempel-Ziv (RLZ) data compression algorithm is a lossless, dictionary-based compression algorithm, especially suited for compressing highly self-similar texts or collections of texts (such as full version histories or collections of genomes), supporting random access decompression of the compressed text.
This repository contains my implementation of RLZ, written as part of my Master's thesis for the University of Helsinki in 2023;
it is my (and my supervisor's) hope that this would serve as a simple, straightforward "reference implementation" for future work with the algorithm.
Also included are some tools for generating dictionaries and manipulating suffix arrays.

My implementation is written in C++, with some of the auxiliary tools in plain C, and it has no dependencies apart from the C and C++ standard libraries.
Compilation has so far only been tested with GCC on Linux, but I expect little difficulty with compiling with other compilers on any vaguely POSIX-y system.

## Components

The rlztools suite includes:
* `rlzparse`: Data compressor
* `rlzunparse`: Decompresses rlzparse's output
* `builddict`: You can use this to create a dictionary by sampling an input file at random positions
* `rlztools.5to4` and `rlztools.5to8`: Suffix array manipulation tools: these turn 40-bit (5-byte) unsigned integers in little-endian byte order into 32-bit (4-byte) and 64-bit (8-byte) integers, also in little-endian byte order.
* `rlztools.count-vbyte-tokens`: A tool used in debugging or analyzing rlzparse's _vbyte_ output format. Counts the number of variable-length LEB128-encoded integers, then divides that by two.
* `rlztools.divsuffix`: A tool used in the multi-step process of constructing a suffix array for wide-symbol input. Essentially, reads in 4-byte or 8-byte little-endian unsigned integers, and those which are divisible by _N_ are divided by _N_ and written out, and those which aren't are dropped.
* `rlztools.endflip`: A tool used in the multi-step process of constructing a suffix array for wide-symbol input. Turns little-endian into big-endian and back again, with any length of integer you want from 2 to 99.
* `rlztools.rlzexplain`: A partly-complete program that prints out rlzparse's output in human-readable form, for debugging or curiosity.
* `rlztooks.suffixdump`: The same, but for suffix arrays: takes in a suffix array and a dictionary, and prints out a bit of each suffix in the array so you can see its structure.

What isn't included is a suffix array generator: you will need one to compress your files with RLZ.
I've mainly used [Yuta Mori's libdivsufsort](https://github.com/y-256/libdivsufsort), which is very fast,
but [Kärkkäinen and Kempa's pSAscan](https://www.cs.helsinki.fi/group/pads/pSAscan.html) can compute suffix arrays both with more limited RAM and for much bigger files, not being limited to 4 GiB.

## Compilation

You will need a C and C++ compiler, as well as Make (I used GNU make, but I avoided GNU-specific features in the makefile).

Download all these files somewhere, then in a terminal go to that directory.
Run:
```console
$ make
```
(If that doesn't work, try `make all`.)

If your compilers are called something different than `gcc` and `g++`, you can tell Make about this with a variable override; the C compiler's name is stored in a variable called `CC` and the C++ compiler's name is in `CXX`:
```console
$ make CC='clang' CXX='clang++'
```

All compiled binaries are put in the newly-created `build/` directory.
Compilation shouldn't leave any intermediate object files lying around – it creates executables directly.
After compilation, you can copy some or all of the built binaries to some other directory that's in your shell's search path (like `/usr/local/bin`, or `~/.local/bin` if that's in your `$PATH`).
You can clean up what's left by running `make clean`, or you can just delete the `build/` directory yourself.

The makefile doesn't support `make install` (yet).

## Usage

### Ordinary case

You have a file of bytes, let's call it `bigfile.txt`.

You don't yet have a dictionary of it, so the first step is to create a dictionary.
Find out `bigfile.txt`'s filesize in bytes. Multiply that number by 0.05 in order to get 5 % of its filesize, and then divide that number by 1000 (rounding up) to get the _number of samples_, which we call _N_.
Run builddict, replacing `<N>` with the _N_ you calculated:
```console
$ builddict -n <N> -l 1000 bigfile.txt -o bigfile.dict
```
You now have a dictionary called `bigfile.dict`.

(Go ahead and experiment with different parameters of `-n` and `-l`!
These are what will affect the compression ratio the most.
I can't tell you the best values to use for your input, because it depends on so many factors (not all of them known), but from my experiments I can say that a dictionary that's 5 % the size of the input, with samples a kilobyte in size, is a reasonable starting point. It's better to make your dictionary too big rather than too small: doubling the dictionary typically halves the output, but halving the dictionary will probably more than double the output.)

Then, calculate the dictionary's suffix array. You will need another program to do this; for illustration, I will assume you're using a program called `build-sa` that takes the input file as its first argument and the output file as its second argument.
I also assume that that program's output consists of 4-byte unsigned little-endian integers.
```console
$ build-sa bigfile.dict bigfile.sa
```

Now, compression itself, to which you give the input, dictionary and suffix array:
```console
$ rlzparse -i bigfile.txt -d bigfile.dict -s bigfile.sa -o bigfile.rlz
```
You only need the suffix array for decompression; if you won't compress any more files with that dictionary, you can delete it. You will need to keep the dictionary, though.

Decompression requires the RLZ file and the dictionary:
```console
$ rlzunparse -d bigfile.dict -i bigfile.rlz -o bigfile.txt.2
```

If you only want to decompress a bit of the text, such as 10 kilobytes starting from uncompressed byte 1,000,000, you can pass the `-a` and `-b` parameters to specify the start and end position. Note that both these parameters are inclusive, and that indexing starts from 1!
```
$ rlzunparse -d bigfile.dict -i bigfile.rlz -a 1000000 -b 1009999 -o bigfile.txt.part
```

### A case with wide input symbols

All the RLZ tools support working with _wide_ data, with widths of 16, 32 or 64 bits.
This is where your data consists of symbols that are all of an equal width, such as an array of integers.
Of course, the underlying representation will be a series of bytes (such as 4 bytes per 32-bit integer), because that's just what our operating systems and disks deal with, but for various reasons it may be desirable to handle compression of these larger units directly – the RLZ parse will "look nicer", and random-access decompression will be aligned to these wider units, and maybe you need it for further algorithmic reasons.

You can pass the `-w` option to each of `rlzparse`, `rlzunparse` and `builddict` to tell them to handle your data as if it were composed of these wider symbols, such as `-w 32` for 32-bit/4-byte symbols.
This parameter affects all other options: in `builddict`, combining `-w 32` with `-n 5000` and `-l 1000` means you will get a 20 megabyte dictionary consisting of 5000 samples of 1000 32-bit integers each.
In `rlzunparse`, combining `-w 64` with `-a 65536 -b 81919` results in a 131,072-byte output (which is 16,384 64-bit integers), starting at the original input's 65536th 64-bit integer, or its 524,288th byte.

```console
$ builddict -w 32 -n 5000 -l 1000 bigfile.ints -o bigfile.dict32
$ rlzparse -w 32 -i bigfile.ints -d bigfile.dict32 -s bigfile.sa32 -o bigfile.rlz
$ rlzunparse -w 32 -d bigfile.dict32 -i bigfile.rlz -o bigfile.ints.2
```

Building a suffix array for the wide-symbol `bigfile.dict32` is the hard part:
there are probably _some_ suffix array calculators out there that can compute a suffix array with a 16/32/64-bit alphabet, but I haven't found one that I could get working.
There is a workaround, though. The exact details of how and why [can be found in section 2.5 of my thesis](http://urn.fi/URN:NBN:fi:hulib-202306273299) (and if you need an academic citation for this method, you can cite that instead of this readme; I was personally unable to find an earlier description of this trick). The short version is as follows (using 32 bits/4 bytes as an example case):
1. Flip the byte order of each of the individual 4-byte symbols, using the `rlztools.endflip` tool (or equivalent): `rlztools.endflip 4 bigfile.dict32 bigfile.dict32.flipped`
2. Calculate the suffix array of this flipped version, _assuming it were just composed of bytes_: `build-sa bigfile.dict32.flipped bigfile.sa8`
3. Go through that suffix array, element by element. Delete any element that isn't divisible by 4, then divide those that remain by 4: `rlztools.divsuffix 4 bigfile.sa8 bigfile.sa32`
4. Delete the intermediate files `bigfile.dict32.flipped` and `bigfile.sa8`. Use `bigfile.sa32` for compression, and `bigfile.dict32` for compression and decompression.

### A case with a very big dictionary

If your dictionary is 2<sup>32</sup> elements* long or larger,
then you can no longer use ordinary 32-bit suffix arrays: there aren't enough bits in the integers to refer to the bytes.
You can upgrade to suffix arrays with 64-bit integers instead,
and pass `rlzparse` the `-W 64` (capital W) option to tell it you're using one.

The [pSAscan program](https://www.cs.helsinki.fi/group/pads/pSAscan.html) writes suffix arrays that use 40-bit unsigned little-endian integers. You can convert these to the 64-bit unsigned little-endian integers that `rlzparse` uses with the `rlztools.5to8` program.
If you're using pSAscan with shorter dictionaries, you can also convert the 40-bit integers to 32-bit ones with the `rlztools.5to4` program.

\*: The limit is specifically 2<sup>32</sup> elements. With ordinary one-byte-wide inputs, this means a limit of 4 GiB, but if you're using 32-bit input symbols with `-w 32`, the limit is 16 GiB instead.
You might need to upgrade to a wider symbol width for the intermediate steps of dividing out the correct suffix array from a flipped-8-bit suffix array even if you're under that limit, though.

## File formats

None of the file formats used by any of the programs in the rlztools suite uses any sort of file header or metadata.
The most common file format is that of the 32-bit unsigned little-endian integer:
`rlzparse` assumes that the suffix arrays it is given are such, and `rlztools.divsuffix` also deals with them, and the default compressed output of `rlzparse` and the default input format of `rlzunparse` represents the RLZ references as a pair of unsigned 32-bit little-endian integers.

For suffix arrays, `rlzparse` and `divsuffix` also support the 64-bit unsigned little-endian integer.

There are alternative output formats for the RLZ-parsed output that are supported by both compressor and decompressor. A different one can be selected with the `-f` option: `-f 32x2` (the default), `-f 64x2`, `-f vbyte` and `-f ascii` are supported.
- `-f 64x2`: Double the size of the default format. Each reference is stored as a pair of 64-bit unsigned little-endian integers. This only needs to be used if the dictionary is longer than 2<sup>32</sup>−1 symbols, or if the input consists of 64-bit symbols itself (this is required in order to represent literal symbols not present in the dictionary).
- `-f vbyte`: A variable-width integer format. This is identical to the little-endian base-128 (LEB128) format used in various projects: the number is split into 7-bit units, and all but the most significant of these will have their high bit set to 1, and the most significant unit has its high bit set to 0. All integers between 0 and 127 (inclusive) are just their one-byte equivalents. This format is very space-efficient: in my tests it was typically around 60 % the size of the default `32x2` format, but it suffers a bit in decoding speed (a slowdown of a few percent). Any case where `64x2` would need to be used should probably use this instead. This may become the default format at some point.
- `-f ascii`: The two numbers of the reference are written out in decimal, separated from each other with a space and separated from other references with a newline. This is human-readable, if it's for some reason necessary. It's never more efficient than `-f vbyte`, it's often more efficient than `64x2` (because of the overhead in that format – even storing just a 1 takes eight bytes), and it's slightly more space-efficient than `-f 32x2` when the dictionary is only a few kilobytes in size.

## Manual pages

I've included manual pages for these programs in the `man/` directory.
(So far, I've only written a combined one for `rlzparse` and `rlzunparse`; others may be written at some point.)
The manuals have been written with the [mdoc language](https://mandoc.bsd.lv/).
Because Linux distributions typically don't come with the mandoc program that compiles the mdoc language, I have also included in that directory pre-compiled versions that should be compatible with the default `man` program, for convenience.

## Bugs

As of the first publicly-released version in December 2023, tagged "0.9.0" on GitHub, I am confident that the RLZ parsings produced by `rlzparse` are correct, and that `rlzparse` is free of serious data-corrupting bugs.
I am confident that the decompression done by `rlzunparse` is also correct when used in the default 8-bit mode, and when *not* using the `-a` or `-b` options: decompression of an entire file should always work.
I am quite confident (like, "98% confident": I've tried a bunch of inputs, but I haven't tested every edge case I can think of) that `rlzunparse` also works as intended in 16, 32 and 64-bit mode when *not* using the `-a` or `-b` options.

I'm less confident in the combination of the `-a`/`-b` options (that is, partial decompression) and the `-w 16`/`-w 32`/`-w 64` modes.
Partial decompression has been a late addition, and while it looks correct at first glance with sensible inputs and 8-bit data, I can't guarantee that there aren't some off-by-one errors in there, or that it doesn't fail with weirder inputs or edge cases.
I do not suggest using partial decompression at all with the wide-symbol modes at this time: this hasn't been tested much, and I remember that the partial decompression index-calculating code has some errors with it when symbols get wide.

### "Error: failed binary search"

This is probably not a bug, but rather due to your suffix array being faulty and not matching the dictionary. This is especially common when trying wide-symbol compression and the wide-symbol suffix array wasn't constructed quite right, or when the wrong suffix array is used for another dictionary, or you forgot to tell `rlzparse` you're dealing with wide-symbol data with the `-w 16/32/64` options.

This error occurs when, during the string-searching loop, the compressor's binary searcher encounters a situation where the data referenced by the suffix array is out of order.
More specifically: the routines require that _t_\[_s_\[i\]\] ≤ _t_\[_s_\[i+1\]\] ≤ _t_\[_s_\[i+2\]\] ≤ ... (where _t_ is the input text and _s_ is the suffix array), but it detected that _t_\[_s_\[i\]\] > _t_\[_s_\[i+1\]\].
There is no way for the algorithm to recover from this situation, so it complains and then terminates the program.

### "Error (bug): outside token-finding loop"

This is more likely to be a bug. I don't know if it can still happen, but during development this came up when one part of the program's state thought there was no more data to compress while another part was still expecting more.
You might still see this if your file stops existing mid-compression, or something.

### builddict never terminates

This is due to the non-deterministic algorithm it uses. Kill it, then try decreasing the `-n` or the `-l` parameters, or both, until it starts working again. You shouldn't wait more than a few seconds to see the "generated _n×l_ samples" message.

Why it stops working is a very interesting question from a computer science lens. I don't have the probabilistic math on hand to explain why or how, but it exhibits a phase transition-like behavior, where very small adjustments to either `-n` or `-l` cause it to jam up. When this happens, it's unlikely to ever fix itself.
The algorithm works by generating a set of `-n` random points in the range of 0 to the length of the dictionary, sorting them, and then checking if any of them overlap (which they do if they're less than `-l` symbols from each other).
If there are overlaps, the program deletes both from the list and generates new ones. It keeps going in a loop until it has its `-n` sampling points, all at least `-l` symbols away from each other.
Most of the time that loop fixes a few overlaps and then terminates. Even with very large `-n`s, it may only have to fix each overlap once; the samples are still sparse.
However, if `-n` and `-l` are too large, then this loop very suddenly becomes unlikely to ever terminate: the samples are just too densely packed together. The interesting part is with how close "sparse enough" and "too dense" are to each other; I've found cases where increasing either parameter by just one is enough.

But in any case: yes, this is a known bug. I might fix it at some point by changing the algorithm entirely (this has some pitfalls – we can uniformly sample the input, but then it will be more uniformly sampled and less random, but maybe that's what desired), or detecting getting stuck and then changing the algorithm, but as it is now the program can indeed never terminate with some parameters.

## The RLZ algorithm

With a general, decently compressible human-written text, RLZ typically attains compression ratios on par with `gzip`, but with particular inputs – such as the concatenation of the full text of every version of a Wikipedia article – it can compress far more than `xz` can.
The time complexity of RLZ compression is O(_n_ + _p_ log _d_), where _n_ is the length of the input text, _d_ is the length of the dictionary, and _p_ is the number of phrases in the compressed text; space complexity is most commonly O(5d), but this varies with symbol-width parameters.

The algorithm compresses the input _relative_ to a dictionary, which is typically composed of samples of the input.
The result of the algorithm is a list of references to the dictionary.
(Scientific literature on RLZ calls these references _phrases_, and my code calls them _tokens_; they're represented as a dictionary index + phrase length number pair.
The entire list of references is called the _RLZ parsing_ or the _RLZ parse_ of the input.)
Taken together, the list of references and the dictionary can be used to reconstruct the original input.
Almost-random-access decompression is possible because the references and the dictionary are decoupled: unlike in other compression algorithms (such as LZ77, which RLZ is similar to), the text that references refer to isn't compressed by other references, so decompression can happen in any order, starting from any reference.

The RLZ compression operation is called _parsing_, and it is, in essence, repeated substring searching.
The program sits in a loop, and tries to find the longest possible substring in the dictionary that matches whatever segment of the input it is currently looking at.
In order for this substring searching to be feasible, the dictionary is preprocessed and a [suffix array](https://en.wikipedia.org/wiki/Suffix_array) of it is formed.
With the suffix array, the substring search problem can be solved with repeated binary searches.
Multiple O(n)-time suffix array computation algorithms are known.
My implementation, here in this repository, does *not* do suffix array calculation itself – instead, the suffix array is calculated by an external program, stored in a file, and then `rlzparse` reads it from that file.
(Implementing suffix array computation within `rlzparse` itself was considered out of scope and at odds with the goal of providing a reference implementation of RLZ; I may or may not end up implementing it at some point.
In any case, it's likely that these external suffix array computers would be faster than what I could write – suffix array computation is an involved topic unto itself.)

The RLZ decompression operation is simpler, and it requires no suffix array.
The entirety of the dictionary is read into RAM, then the RLZ parsing's references are read one at a time.
The decompressor jumps to the reference's stored position in the dictionary, and copies the specified length of text to the output.
Almost-random-access decompression is done by _not_ copying the text to the output immediately; instead, careful index arithmetic is done with the indices of the references to find the position the asked-for text starts at, and only then does copying start.

For a more detailed description, I refer you to the source code here, or chapter 2 of [my thesis](http://urn.fi/URN:NBN:fi:hulib-202306273299), or the papers cited within, such as S. Kuruppu, S. J. Puglizi, J. Zobel: "[Relative Lempel-Ziv compression of genomes for large-scale storage and retrieval](https://people.eng.unimelb.edu.au/jzobel/fulltext/spire10.pdf)", in Proc. 17th SPIRE, pages 201–206, 2010 (which was, as far as I can tell, the first description of the algorithm).
My thesis also includes some analysis of RLZ's performance (including deriving the O(_n_ + _p_ log _d_) time complexity), some benchmarking numbers with various kinds of inputs, and some discussion on how to sample a dictionary.

## License

All code is licensed under the [Mozilla Public License, version 2.0](https://www.mozilla.org/en-US/MPL/2.0/).


