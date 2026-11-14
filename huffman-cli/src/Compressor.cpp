/*
 * Compressor: file-level and stream-level Huffman compression.
 * See Compressor.hpp for the file format.
 */

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>
#include "BitIoStream.hpp"
#include "CanonicalCode.hpp"
#include "Compressor.hpp"
#include "FrequencyTable.hpp"
#include "HuffmanCoder.hpp"

using std::int32_t;
using std::uint32_t;
using std::uint64_t;

namespace {

const char MAGIC[4] = {'H', 'U', 'F', '1'};
const uint32_t EOF_SYMBOL = 256;
const uint32_t SYMBOL_LIMIT = 257;  // 256 byte values + EOF
const std::size_t CHUNK = 1 << 16;

double secondsSince(std::chrono::steady_clock::time_point start) {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// Flattens a code tree into an array: child[2*i] / child[2*i+1] are the 0- and 1-children of
// internal node i (root = 0). A value >= 0 is another internal node; a value < 0 is a leaf
// holding symbol (-1 - value). Returns the encoded form of the given node.
int32_t flattenTree(const Node *node, std::vector<int32_t> &child) {
	if (const Leaf *leaf = dynamic_cast<const Leaf *>(node))
		return -1 - static_cast<int32_t>(leaf->symbol);
	const InternalNode *in = dynamic_cast<const InternalNode *>(node);
	if (in == nullptr)
		throw std::logic_error("Illegal node type");
	const int32_t index = static_cast<int32_t>(child.size() / 2);
	child.push_back(0);
	child.push_back(0);
	const int32_t l = flattenTree(in->leftChild.get(), child);
	const int32_t r = flattenTree(in->rightChild.get(), child);
	child[2 * static_cast<std::size_t>(index)] = l;
	child[2 * static_cast<std::size_t>(index) + 1] = r;
	return index;
}

}  // namespace


Stats compressStream(std::istream &in, std::ostream &out, CodeTree *outTree) {
	const auto start = std::chrono::steady_clock::now();
	Stats stats;

	// Pass 1: count byte frequencies. A flat array indexed by byte value is the fastest
	// possible "map" here (O(1), no hashing, cache friendly) - faster than std::unordered_map.
	std::vector<uint64_t> counts(SYMBOL_LIMIT, 0);
	std::vector<char> buf(CHUNK);
	while (in) {
		in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
		const std::streamsize n = in.gcount();
		for (std::streamsize i = 0; i < n; i++)
			counts[static_cast<unsigned char>(buf[static_cast<std::size_t>(i)])]++;
		stats.inputBytes += static_cast<uint64_t>(n);
	}
	if (in.bad())
		throw std::runtime_error("Read error while reading input");
	in.clear();
	in.seekg(0);
	if (!in)
		throw std::runtime_error("Input stream is not seekable");

	// Build the code. An empty input or an input with a single distinct byte is fine:
	// the EOF symbol always has frequency 1, so the tree always has at least 2 leaves.
	FrequencyTable freqs(std::vector<uint32_t>(SYMBOL_LIMIT, 0));
	for (uint32_t s = 0; s < 256; s++) {
		if (counts[s] > UINT32_MAX)
			throw std::overflow_error("Input too large: a byte value occurs more than 4294967295 times");
		freqs.set(s, static_cast<uint32_t>(counts[s]));
	}
	freqs.set(EOF_SYMBOL, 1);
	CodeTree code = freqs.buildCodeTree();
	const CanonicalCode canon(code, SYMBOL_LIMIT);
	code = canon.toCodeTree();  // canonical form: only the code lengths need to be stored

	// Header: magic, then 257 code lengths of 8 bits each
	const std::streamoff startPos = out.tellp();
	out.write(MAGIC, sizeof(MAGIC));
	BitOutputStream bout(out);
	for (uint32_t s = 0; s < SYMBOL_LIMIT; s++) {
		const uint32_t len = canon.getCodeLength(s);
		if (len >= 256)
			throw std::domain_error("A symbol's code is longer than 255 bits");
		for (int j = 7; j >= 0; j--)
			bout.write(static_cast<int>((len >> j) & 1));
	}

	// Pass 2: encode
	HuffmanEncoder enc(bout);
	enc.codeTree = &code;
	while (in) {
		in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
		const std::streamsize n = in.gcount();
		for (std::streamsize i = 0; i < n; i++)
			enc.write(static_cast<unsigned char>(buf[static_cast<std::size_t>(i)]));
	}
	if (in.bad())
		throw std::runtime_error("Read error while reading input");
	enc.write(EOF_SYMBOL);
	bout.finish();
	out.flush();
	if (!out)
		throw std::runtime_error("Write error while writing output");

	const std::streamoff endPos = out.tellp();
	stats.outputBytes = (startPos >= 0 && endPos >= startPos) ? static_cast<uint64_t>(endPos - startPos) : 0;
	stats.seconds = secondsSince(start);
	if (outTree != nullptr)
		*outTree = std::move(code);
	return stats;
}


Stats decompressStream(std::istream &in, std::ostream &out, CodeTree *outTree) {
	const auto start = std::chrono::steady_clock::now();
	Stats stats;
	char magic[sizeof(MAGIC)] = {};
	in.read(magic, sizeof(magic));
	if (in.gcount() != static_cast<std::streamsize>(sizeof(magic)) || std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0)
		throw std::runtime_error("Not a valid .huff file (missing or wrong header)");

	BitInputStream bin(in);
	std::vector<uint32_t> lengths;
	try {
		for (uint32_t s = 0; s < SYMBOL_LIMIT; s++) {
			uint32_t val = 0;
			for (int j = 0; j < 8; j++)
				val = (val << 1) | static_cast<uint32_t>(bin.readNoEof());
			lengths.push_back(val);
		}
	} catch (const std::runtime_error &) {
		throw std::runtime_error("Corrupt file: truncated code table");
	}
	if (lengths[EOF_SYMBOL] == 0)
		throw std::runtime_error("Corrupt file: the end-of-data symbol has no code");

	CodeTree code = [&]() -> CodeTree {
		try {
			return CanonicalCode(lengths).toCodeTree();
		} catch (const std::invalid_argument &e) {
			throw std::runtime_error(std::string("Corrupt file: invalid code table (") + e.what() + ")");
		}
	}();

	// Fast decoding: flatten the tree into an int array (no virtual calls / dynamic_cast per bit)
	// and consume the bytes straight from a buffer. The header was read byte-aligned, so the
	// stream now sits exactly at the first byte of the coded data.
	std::vector<int32_t> child;  // child[2*i + bit]: >= 0 is an internal node index, < 0 encodes leaf symbol (-1 - symbol)
	flattenTree(&code.root, child);

	std::vector<char> inBuf(CHUNK), outBuf;
	outBuf.reserve(CHUNK);
	uint64_t consumed = sizeof(MAGIC) + SYMBOL_LIMIT;  // header bytes
	int32_t node = 0;
	bool done = false;
	while (!done) {
		in.read(inBuf.data(), static_cast<std::streamsize>(inBuf.size()));
		const std::size_t n = static_cast<std::size_t>(in.gcount());
		if (n == 0) {
			if (in.bad())
				throw std::runtime_error("Read error while reading input");
			throw std::runtime_error("Corrupt file: data ended before the end-of-data symbol");
		}
		std::size_t i = 0;
		for (; i < n && !done; i++) {
			const unsigned byte = static_cast<unsigned char>(inBuf[i]);
			for (int bit = 7; bit >= 0; bit--) {
				const int32_t next = child[2 * static_cast<std::size_t>(node) + ((byte >> bit) & 1)];
				if (next >= 0) {
					node = next;
					continue;
				}
				const uint32_t symbol = static_cast<uint32_t>(-1 - next);
				node = 0;
				if (symbol == EOF_SYMBOL) {
					done = true;  // remaining bits of this byte are padding
					break;
				}
				outBuf.push_back(static_cast<char>(static_cast<unsigned char>(symbol)));
				if (outBuf.size() == CHUNK) {
					out.write(outBuf.data(), static_cast<std::streamsize>(outBuf.size()));
					stats.outputBytes += outBuf.size();
					outBuf.clear();
				}
			}
		}
		consumed += i;
	}
	out.write(outBuf.data(), static_cast<std::streamsize>(outBuf.size()));
	stats.outputBytes += outBuf.size();
	out.flush();
	if (!out)
		throw std::runtime_error("Write error while writing output");

	stats.inputBytes = consumed;
	stats.seconds = secondsSince(start);
	if (outTree != nullptr)
		*outTree = std::move(code);
	return stats;
}


namespace {

void checkPaths(const std::string &inPath, const std::string &outPath) {
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::is_directory(inPath, ec))
		throw std::runtime_error("Input is a directory, not a file: " + inPath);
	if (!fs::exists(inPath, ec))
		throw std::runtime_error("Input file does not exist: " + inPath);
	if (fs::exists(outPath, ec) && fs::equivalent(inPath, outPath, ec))
		throw std::runtime_error("Input and output are the same file; refusing to overwrite it");
}

}  // namespace


Stats compressFile(const std::string &inPath, const std::string &outPath, CodeTree *outTree) {
	checkPaths(inPath, outPath);
	std::ifstream in(inPath, std::ios::binary);
	if (!in)
		throw std::runtime_error("Cannot open input file: " + inPath);
	std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
	if (!out)
		throw std::runtime_error("Cannot create output file: " + outPath);
	try {
		return compressStream(in, out, outTree);
	} catch (...) {
		out.close();
		std::remove(outPath.c_str());
		throw;
	}
}


Stats decompressFile(const std::string &inPath, const std::string &outPath, CodeTree *outTree) {
	checkPaths(inPath, outPath);
	std::ifstream in(inPath, std::ios::binary);
	if (!in)
		throw std::runtime_error("Cannot open input file: " + inPath);
	std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
	if (!out)
		throw std::runtime_error("Cannot create output file: " + outPath);
	try {
		return decompressStream(in, out, outTree);
	} catch (...) {
		out.close();
		std::remove(outPath.c_str());
		throw;
	}
}
