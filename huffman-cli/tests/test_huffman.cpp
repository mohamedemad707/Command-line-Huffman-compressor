/*
 * Unit tests for the Huffman compressor. Dependency-free on purpose so it builds anywhere;
 * the TEST/CHECK macros below map almost 1:1 onto Catch2 (TEST_CASE / REQUIRE) if you
 * later want to switch.
 */

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "Compressor.hpp"
#include "TreeExport.hpp"

static int g_checks = 0, g_failures = 0;
static std::vector<std::pair<std::string, std::function<void()> > > g_tests;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; \
	std::cerr << "    FAILED " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; } } while (0)

#define CHECK_THROWS(expr) do { g_checks++; bool thrown = false; \
	try { expr; } catch (const std::exception &) { thrown = true; } \
	if (!thrown) { g_failures++; std::cerr << "    FAILED " << __FILE__ << ":" << __LINE__ \
		<< ": expected exception from " #expr "\n"; } } while (0)

struct Registrar {
	Registrar(const char *name, std::function<void()> f) { g_tests.push_back(std::make_pair(name, f)); }
};
#define TEST(name) static void name(); static Registrar reg_##name(#name, name); static void name()


static std::string compress(const std::string &data, std::string *packed = nullptr) {
	std::istringstream in(data);
	std::ostringstream out;
	compressStream(in, out);
	if (packed != nullptr)
		*packed = out.str();
	return out.str();
}

static std::string decompress(const std::string &packed) {
	std::istringstream in(packed);
	std::ostringstream out;
	decompressStream(in, out);
	return out.str();
}

static bool roundTrips(const std::string &data) {
	return decompress(compress(data)) == data;
}

static std::string randomBytes(std::size_t n, unsigned seed) {
	std::mt19937 rng(seed);
	std::string s(n, '\0');
	for (char &c : s)
		c = static_cast<char>(rng() & 0xFF);
	return s;
}


/*---- Edge cases ----*/

TEST(empty_input) {
	CHECK(roundTrips(""));
}

TEST(single_byte) {
	CHECK(roundTrips("a"));
	CHECK(roundTrips(std::string(1, '\0')));
}

TEST(single_repeated_character) {
	CHECK(roundTrips("aaaaa"));
	CHECK(roundTrips(std::string(100000, 'a')));
}

TEST(two_distinct_characters) {
	CHECK(roundTrips("ab"));
	CHECK(roundTrips("abababababbbbbbbba"));
}

TEST(all_256_byte_values) {
	std::string s;
	for (int i = 0; i < 256; i++)
		s += static_cast<char>(i);
	CHECK(roundTrips(s));
}

TEST(binary_zero_bytes_and_0xFF) {
	CHECK(roundTrips(std::string(1000, '\0')));
	CHECK(roundTrips(std::string(1000, '\xFF')));
	CHECK(roundTrips(std::string("\0a\0b\0", 5)));  // embedded NULs
}

TEST(text_with_newlines_and_unicode) {
	CHECK(roundTrips("line one\r\nline two\n\ttabbed\n"));
	CHECK(roundTrips("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xE4\xB8\x96\xE7\x95\x8C \xF0\x9F\x98\x80"));  // Arabic, Chinese, emoji (UTF-8)
}

TEST(random_binary_data) {
	CHECK(roundTrips(randomBytes(1, 1)));
	CHECK(roundTrips(randomBytes(255, 2)));
	CHECK(roundTrips(randomBytes(257, 3)));
	CHECK(roundTrips(randomBytes(300000, 4)));  // crosses several 64 KiB chunk boundaries
}

TEST(chunk_boundary_sizes) {
	for (std::size_t n : {65535u, 65536u, 65537u, 131072u})
		CHECK(roundTrips(std::string(n, 'x') + "y"));
}

TEST(deeply_skewed_frequencies_fibonacci) {
	// Fibonacci frequencies produce the deepest possible Huffman tree.
	std::string s;
	std::uint64_t a = 1, b = 1;
	for (int sym = 0; sym < 26; sym++) {
		s += std::string(static_cast<std::size_t>(a), static_cast<char>('A' + sym));
		std::uint64_t c = a + b;
		a = b;
		b = c;
	}
	CHECK(roundTrips(s));
}


/*---- Properties ----*/

TEST(compresses_redundant_data) {
	// Huffman spends at least 1 bit per symbol, so the floor here is 12.5% plus the
	// 261-byte header; anything under 1/7 of the original proves real compression.
	const std::string s(100000, 'a');
	CHECK(compress(s).size() < s.size() / 7);
}

TEST(output_is_deterministic) {
	const std::string s = randomBytes(5000, 7);
	CHECK(compress(s) == compress(s));
}

TEST(stats_are_consistent) {
	const std::string s(1000, 'z');
	std::istringstream in(s);
	std::ostringstream out;
	const Stats st = compressStream(in, out);
	CHECK(st.inputBytes == 1000);
	CHECK(st.outputBytes == out.str().size());
	CHECK(st.seconds >= 0.0);

	std::istringstream in2(out.str());
	std::ostringstream out2;
	const Stats st2 = decompressStream(in2, out2);
	CHECK(st2.outputBytes == 1000);
	CHECK(st2.inputBytes == out.str().size());
}


/*---- Corrupt / invalid input must throw, never crash ----*/

TEST(rejects_non_huff_input) {
	CHECK_THROWS(decompress(""));
	CHECK_THROWS(decompress("hello world, this is not a huff file"));
	CHECK_THROWS(decompress("HUF"));
}

TEST(rejects_truncated_files) {
	const std::string packed = compress("some reasonably long text to compress, some reasonably long text");
	CHECK_THROWS(decompress(packed.substr(0, 3)));          // inside the magic
	CHECK_THROWS(decompress(packed.substr(0, 4 + 100)));    // inside the code table
	CHECK_THROWS(decompress(packed.substr(0, packed.size() - 3)));  // inside the data
}

TEST(rejects_corrupt_code_table) {
	std::string packed = compress("hello hello hello");
	for (int i = 4; i < 4 + 257; i++)
		packed[static_cast<std::size_t>(i)] = '\x01';  // 257 symbols all of length 1: over-full tree
	CHECK_THROWS(decompress(packed));
}

TEST(random_garbage_after_valid_magic_never_crashes) {
	for (unsigned seed = 0; seed < 50; seed++) {
		const std::string garbage = "HUF1" + randomBytes(400, seed);
		try {
			decompress(garbage);
		} catch (const std::exception &) {
			// expected for almost all inputs
		}
	}
	CHECK(true);
}


/*---- Files ----*/

static std::string readFile(const std::string &path) {
	std::ifstream f(path, std::ios::binary);
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

static void writeFile(const std::string &path, const std::string &data) {
	std::ofstream f(path, std::ios::binary);
	f << data;
}

TEST(file_round_trip_is_byte_identical) {
	const std::string original = randomBytes(50000, 11) + std::string(50000, 'q') + "end\n";
	writeFile("test_in.bin", original);
	compressFile("test_in.bin", "test_mid.huff");
	decompressFile("test_mid.huff", "test_out.bin");
	CHECK(readFile("test_out.bin") == original);
	std::remove("test_in.bin"); std::remove("test_mid.huff"); std::remove("test_out.bin");
}

TEST(empty_file_round_trip) {
	writeFile("test_empty.bin", "");
	compressFile("test_empty.bin", "test_empty.huff");
	decompressFile("test_empty.huff", "test_empty.out");
	CHECK(readFile("test_empty.out").empty());
	std::remove("test_empty.bin"); std::remove("test_empty.huff"); std::remove("test_empty.out");
}

TEST(missing_input_file_throws) {
	CHECK_THROWS(compressFile("definitely_missing_file.xyz", "unused.huff"));
}

TEST(same_input_and_output_is_refused_and_input_survives) {
	writeFile("test_same.bin", "precious data");
	CHECK_THROWS(compressFile("test_same.bin", "test_same.bin"));
	CHECK(readFile("test_same.bin") == "precious data");
	std::remove("test_same.bin");
}

TEST(failed_decompression_removes_partial_output) {
	writeFile("test_bad.huff", "not a huff file");
	CHECK_THROWS(decompressFile("test_bad.huff", "test_bad.out"));
	std::ifstream f("test_bad.out");
	CHECK(!f.good());
	std::remove("test_bad.huff");
}


/*---- Tree export ----*/

TEST(tree_export_text_and_dot) {
	std::istringstream in("abracadabra");
	std::ostringstream out;
	CodeTree tree = [] { return CodeTree(InternalNode(std::unique_ptr<Node>(new Leaf(0)), std::unique_ptr<Node>(new Leaf(1))), 2); }();
	compressStream(in, out, &tree);

	std::ostringstream text, dot;
	printTree(tree, text);
	writeDot(tree, dot);
	CHECK(text.str().find("'a'") != std::string::npos);
	CHECK(text.str().find("EOF") != std::string::npos);
	CHECK(dot.str().find("digraph") != std::string::npos);
	CHECK(dot.str().find("->") != std::string::npos);
	CHECK(symbolLabel('\n') == "'\\n'");
	CHECK(symbolLabel(0) == "0x00");
	CHECK(symbolLabel(256) == "EOF");
}


int main() {
	for (const auto &t : g_tests) {
		const int before = g_failures;
		t.second();
		std::cout << (g_failures == before ? "[ OK ] " : "[FAIL] ") << t.first << "\n";
	}
	std::cout << "\n" << g_checks << " checks, " << g_failures << " failures\n";
	return g_failures == 0 ? 0 : 1;
}
