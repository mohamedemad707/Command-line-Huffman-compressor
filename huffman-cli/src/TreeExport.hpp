/*
 * Visualisation of a Huffman code tree: a text drawing for the terminal,
 * and Graphviz DOT output (render with: dot -Tpng tree.dot -o tree.png).
 */

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include "CodeTree.hpp"


// Human-readable name of a symbol: 'a', '\n', 0x07, or EOF for the end-of-data symbol (256).
std::string symbolLabel(std::uint32_t symbol);

// Draws the tree as indented text. Left edges are labelled 0, right edges 1.
void printTree(const CodeTree &tree, std::ostream &out);

// Writes the tree in Graphviz DOT format.
void writeDot(const CodeTree &tree, std::ostream &out);
