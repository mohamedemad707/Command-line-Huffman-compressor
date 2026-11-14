# huffman-cli

A command-line Huffman compressor/decompressor in C++17, built on top of
[Project Nayuki's Reference Huffman coding](https://www.nayuki.io/page/reference-huffman-coding)
(MIT License, see `LICENSE-NAYUKI.txt`).

## Build and run

```
make              # builds ./huffman
make test         # unit tests
make sanitize     # same tests under AddressSanitizer + UBSan
```

```
./huffman -c input.txt -o output.huff        # compress
./huffman -d output.huff -o restored.txt     # decompress
./huffman -c input.txt --tree                # also print the code tree
./huffman -c input.txt --dot tree.dot        # export Graphviz DOT (dot -Tpng tree.dot -o tree.png)
./huffman -c input.txt -q                    # no statistics
```

If `-o` is omitted: `input.txt` -> `input.txt.huff`, and `x.huff` -> `x`.

Each run prints original/compressed size, compression ratio (compressed as % of
original, and space saved) and the time taken.

## Layout

| File | Responsibility |
|---|---|
| `src/main.cpp` | argument parsing and reporting only |
| `src/Compressor.*` | file/stream compress + decompress, binary I/O, validation |
| `src/TreeExport.*` | text drawing and DOT export of the code tree |
| `src/CodeTree.*`, `FrequencyTable.*`, `CanonicalCode.*`, `HuffmanCoder.*`, `BitIoStream.*` | Huffman algorithm and bit-level I/O (from the reference, unchanged) |
| `tests/test_huffman.cpp` | unit tests |

## File format (.huff)

`"HUF1"` magic, 257 code-length bytes (256 byte values + an EOF symbol; the code is
canonical so lengths are enough to rebuild it), then the coded data ending with the
EOF symbol, padded with 0 bits to a byte boundary.

The EOF symbol is why empty files and single-character files (`aaaaa`) need no special
case: the tree always has at least two leaves.

## Known limitations

- The 261-byte header makes very small files *larger* after compression. That is normal
  for static Huffman; the ratio line will show it.
- A single byte value may occur at most 2^32-1 times (inputs over ~4 GiB of one byte are rejected with a clear error).
- Static Huffman does not exploit repetition across bytes (it is not LZ-style compression).
