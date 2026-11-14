/*
 * Visualisation of a Huffman code tree. See TreeExport.hpp.
 */

#include <cstdio>
#include <ostream>
#include <string>
#include "TreeExport.hpp"

using std::uint32_t;

namespace {

std::string hexLabel(uint32_t symbol) {
	char tmp[16];
	std::snprintf(tmp, sizeof(tmp), "0x%02X", static_cast<unsigned>(symbol));
	return tmp;
}

// Escapes a string for use inside a double-quoted DOT label.
std::string dotEscape(const std::string &s) {
	std::string r;
	for (char c : s) {
		if (c == '"' || c == '\\')
			r += '\\';
		r += c;
	}
	return r;
}

void drawNode(const Node *node, const std::string &prefix, const std::string &code, std::ostream &out) {
	if (const InternalNode *in = dynamic_cast<const InternalNode *>(node)) {
		const Node *children[2] = {in->leftChild.get(), in->rightChild.get()};
		for (int i = 0; i < 2; i++) {
			const bool last = (i == 1);
			const std::string childCode = code + (i == 0 ? "0" : "1");
			out << prefix << (last ? "`-" : "|-") << i << "-- ";
			if (const Leaf *leaf = dynamic_cast<const Leaf *>(children[i]))
				out << symbolLabel(leaf->symbol) << "   [" << childCode << "]\n";
			else
				out << "*\n";
			if (dynamic_cast<const InternalNode *>(children[i]) != nullptr)
				drawNode(children[i], prefix + (last ? "    " : "|   "), childCode, out);
		}
	}
}

int dotNode(const Node *node, int &counter, std::ostream &out) {
	const int id = counter++;
	if (const InternalNode *in = dynamic_cast<const InternalNode *>(node)) {
		out << "  n" << id << " [shape=circle, label=\"\", width=0.2];\n";
		const int l = dotNode(in->leftChild.get(), counter, out);
		const int r = dotNode(in->rightChild.get(), counter, out);
		out << "  n" << id << " -> n" << l << " [label=\"0\"];\n";
		out << "  n" << id << " -> n" << r << " [label=\"1\"];\n";
	} else if (const Leaf *leaf = dynamic_cast<const Leaf *>(node)) {
		out << "  n" << id << " [shape=box, label=\"" << dotEscape(symbolLabel(leaf->symbol)) << "\"];\n";
	}
	return id;
}

}  // namespace


std::string symbolLabel(uint32_t symbol) {
	if (symbol == 256)
		return "EOF";
	switch (symbol) {
		case '\n': return "'\\n'";
		case '\r': return "'\\r'";
		case '\t': return "'\\t'";
		case ' ':  return "' '";
	}
	if (symbol > 32 && symbol < 127)
		return std::string("'") + static_cast<char>(symbol) + "'";
	return hexLabel(symbol);
}


void printTree(const CodeTree &tree, std::ostream &out) {
	out << "(root)\n";
	drawNode(&tree.root, "", "", out);
}


void writeDot(const CodeTree &tree, std::ostream &out) {
	out << "digraph HuffmanTree {\n";
	out << "  node [fontname=\"monospace\"];\n";
	int counter = 0;
	dotNode(&tree.root, counter, out);
	out << "}\n";
}
