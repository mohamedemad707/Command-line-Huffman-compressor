/*
 * huffman - command line Huffman compressor / decompressor
 *
 *   huffman -c input.txt -o output.huff     compress
 *   huffman -d output.huff -o restored.txt  decompress
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include "Compressor.hpp"
#include "TreeExport.hpp"

namespace {

void usage(const char *prog, std::ostream &out) {
	out << "Usage:\n"
	    << "  " << prog << " -c INPUT [-o OUTPUT] [options]   compress INPUT (default output: INPUT.huff)\n"
	    << "  " << prog << " -d INPUT [-o OUTPUT] [options]   decompress INPUT (default output: INPUT without .huff)\n"
	    << "\n"
	    << "Options:\n"
	    << "  -o, --output FILE   output file\n"
	    << "  -t, --tree          print the Huffman code tree as text\n"
	    << "      --dot FILE      write the code tree in Graphviz DOT format\n"
	    << "  -q, --quiet         do not print statistics\n"
	    << "  -h, --help          show this help\n";
}

bool endsWith(const std::string &s, const std::string &suffix) {
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void printStats(const char *what, const Stats &s, bool compressing) {
	std::cout << what << " done\n";
	if (compressing) {
		std::cout << "  Original size   : " << s.inputBytes << " bytes\n"
		          << "  Compressed size : " << s.outputBytes << " bytes\n";
		if (s.inputBytes > 0) {
			const double ratio = 100.0 * static_cast<double>(s.outputBytes) / static_cast<double>(s.inputBytes);
			std::printf("  Compression ratio: %.1f%% of original (space saved: %.1f%%)\n", ratio, 100.0 - ratio);
		} else {
			std::cout << "  Compression ratio: n/a (empty input)\n";
		}
		std::printf("  Time taken      : %.4f s\n", s.seconds);
	} else {
		std::cout << "  Compressed size : " << s.inputBytes << " bytes\n"
		          << "  Restored size   : " << s.outputBytes << " bytes\n";
		std::printf("  Time taken      : %.4f s\n", s.seconds);
	}
}

}  // namespace


int main(int argc, char *argv[]) {
	bool compress = false, decompress = false, showTree = false, quiet = false;
	std::string input, output, dotFile;

	for (int i = 1; i < argc; i++) {
		const std::string arg = argv[i];
		auto needValue = [&](const char *name) -> std::string {
			if (i + 1 >= argc)
				throw std::invalid_argument(std::string("Option ") + name + " needs a value");
			return argv[++i];
		};
		try {
			if (arg == "-h" || arg == "--help") {
				usage(argv[0], std::cout);
				return EXIT_SUCCESS;
			} else if (arg == "-c" || arg == "--compress") {
				compress = true;
				input = needValue("-c");
			} else if (arg == "-d" || arg == "--decompress") {
				decompress = true;
				input = needValue("-d");
			} else if (arg == "-o" || arg == "--output") {
				output = needValue("-o");
			} else if (arg == "--dot") {
				dotFile = needValue("--dot");
			} else if (arg == "-t" || arg == "--tree") {
				showTree = true;
			} else if (arg == "-q" || arg == "--quiet") {
				quiet = true;
			} else {
				throw std::invalid_argument("Unknown argument: " + arg);
			}
		} catch (const std::invalid_argument &e) {
			std::cerr << "Error: " << e.what() << "\n\n";
			usage(argv[0], std::cerr);
			return EXIT_FAILURE;
		}
	}

	if (compress == decompress) {  // neither, or both
		std::cerr << "Error: specify exactly one of -c or -d\n\n";
		usage(argv[0], std::cerr);
		return EXIT_FAILURE;
	}

	if (output.empty()) {
		if (compress)
			output = input + ".huff";
		else
			output = endsWith(input, ".huff") ? input.substr(0, input.size() - 5) : input + ".out";
	}

	try {
		CodeTree tree = [] {
			// Placeholder-free construction: filled in by compressFile/decompressFile.
			return CodeTree(InternalNode(std::unique_ptr<Node>(new Leaf(0)), std::unique_ptr<Node>(new Leaf(1))), 2);
		}();
		const Stats stats = compress ? compressFile(input, output, &tree) : decompressFile(input, output, &tree);

		if (!quiet) {
			std::cout << (compress ? "Compressed: " : "Decompressed: ") << input << " -> " << output << "\n";
			printStats(compress ? "Compression" : "Decompression", stats, compress);
		}
		if (showTree) {
			std::cout << "\nHuffman tree:\n";
			printTree(tree, std::cout);
		}
		if (!dotFile.empty()) {
			std::ofstream dot(dotFile);
			if (!dot)
				throw std::runtime_error("Cannot create DOT file: " + dotFile);
			writeDot(tree, dot);
			if (!quiet)
				std::cout << "\nDOT file written: " << dotFile << "  (render: dot -Tpng " << dotFile << " -o tree.png)\n";
		}
		return EXIT_SUCCESS;
	} catch (const std::exception &e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}
