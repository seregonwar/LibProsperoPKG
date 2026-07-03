# LZN Benchmark

- Tool: `build-ninja-release/prosperopkg-lzn`
- Fixture size: 8 MiB each
- Iterations: 10
- LZN level: 2
- LZNB block size: 524288
- zlib level: 6

Kraken/newLZ is not measured here because no licensed, clean-room-compatible oracle is configured in this repository yet.

| Fixture | Codec | Input bytes | Compressed bytes | Ratio | Compress MiB/s | Decompress MiB/s |
|---|---|---:|---:|---:|---:|---:|
| repeated-text | LZNB-block | 8388608 | 193696 | 0.023 | 124.5 | 190.8 |
| repeated-text | LZN1-frame | 8388608 | 192177 | 0.023 | 381.8 | 1908.2 |
| repeated-text | zlib-6 | 8388608 | 24470 | 0.003 | 343.1 | 2465.7 |
| structured-pfs-like | LZNB-block | 8388608 | 656192 | 0.078 | 145.1 | 197.3 |
| structured-pfs-like | LZN1-frame | 8388608 | 655384 | 0.078 | 307.6 | 731.4 |
| structured-pfs-like | zlib-6 | 8388608 | 57685 | 0.007 | 239.8 | 4320.3 |
| sparse-pages | LZNB-block | 8390656 | 206002 | 0.025 | 133.0 | 154.9 |
| sparse-pages | LZN1-frame | 8390656 | 204859 | 0.024 | 329.0 | 400.5 |
| sparse-pages | zlib-6 | 8390656 | 23170 | 0.003 | 170.1 | 1982.8 |
| high-entropy | LZNB-block | 8388608 | 8389056 | 1.000 | 63.7 | 318.2 |
| high-entropy | LZN1-frame | 8388608 | 8388632 | 1.000 | 130.4 | 27894.0 |
| high-entropy | zlib-6 | 8388608 | 8391174 | 1.000 | 46.3 | 5395.4 |

These numbers are a local optimization baseline.  
