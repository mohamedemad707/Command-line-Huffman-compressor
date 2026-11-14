/*
 * Compressor: file-level and stream-level Huffman compression.
 *
 * File format (.huff):
 *   bytes 0-3   : magic "HUF1"
 *   next 257 B  : code length of each symbol (256 byte values + 1 EOF symbol),
 *                 one byte each, forming a canonical Huffman code
 *   rest        : Huffman-coded data ending with the EOF symbol, padded with 0 bits
 *
 * Built on top of the Reference Huffman coding library by Project Nayuki (MIT License).
 */

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include "CodeTree.hpp"


struct Stats {
	std::uint64_t inputBytes  = 0;
	std::uint64_t outputBytes = 0;
	double seconds = 0.0;
};


// Stream versions. The input stream of compressStream() must be seekable (two passes).
// If outTree is not null, it receives the canonical code tree that was used.
// All functions throw std::exception subclasses on error (bad input, I/O failure, corrupt data).
Stats compressStream(std::istream &in, std::ostream &out, CodeTree *outTree = nullptr);
Stats decompressStream(std::istream &in, std::ostream &out, CodeTree *outTree = nullptr);

// File versions. On failure, a partially written output file is removed.
Stats compressFile(const std::string &inPath, const std::string &outPath, CodeTree *outTree = nullptr);
Stats decompressFile(const std::string &inPath, const std::string &outPath, CodeTree *outTree = nullptr);
