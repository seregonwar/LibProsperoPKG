# LibProsperoPkg Native Comparison

- C# baseline: `test/libprosperopkg-osx-arm64/LibProsperoPkg.dylib`
- C++ library: `build-ninja-release/LibProsperoPkg.0.1.0.dylib`

## Correctness

| Check | Status | Details |
|---|---:|---|
| `exported-lpp-functions` | PASS | ['lpp_build_inner_image', 'lpp_build_package', 'lpp_compose_content_id', 'lpp_detect_package_type', 'lpp_encrypt_pfs_image', 'lpp_is_elf', 'lpp_is_self', 'lpp_is_ucp', 'lpp_is_valid_content_id', 'lpp_is_valid_title_id', 'lpp_last_error', 'lpp_make_fself', 'lpp_pack_pfs_image', 'lpp_unpack_pfs_image', 'lpp_version'] |
| `expected-export-count` | PASS | 15 |
| `version-string` | INFO | csharp=LibProsperoPkg 1.3.0; cpp=LibProsperoPkg C++ 0.1.0 |
| `is-valid-content-id:b'UP9000-PPSA00000_00-PROSPERO00000000'` | PASS | 1 |
| `is-valid-content-id:b'up9000-PPSA00000_00-PROSPERO00000000'` | PASS | 0 |
| `is-valid-content-id:b'bad'` | PASS | 0 |
| `is-valid-content-id:None` | PASS | 0 |
| `is-valid-title-id:b'PPSA00000'` | PASS | 1 |
| `is-valid-title-id:b'CUSA00000'` | PASS | 1 |
| `is-valid-title-id:b'abcd12345'` | PASS | 0 |
| `is-valid-title-id:b'PPSA0000'` | PASS | 0 |
| `is-valid-title-id:None` | PASS | 0 |
| `compose:(None, None, None, 64)` | PASS | (36, b'UP9000-PPSA00000_00-0000000000000000') |
| `compose:(b'up9', b'ppsa1', b'hello world!', 64)` | PASS | (36, b'UP9000-PPSA10000_00-HELLOWORLD000000') |
| `compose:(b'EU1234', b'ABCD12345', b'a&b', 64)` | PASS | (36, b'EU1234-ABCD12345_00-AB00000000000000') |
| `compose:(None, None, None, 4)` | PASS | (-37, b'') |
| `is-elf:test-elf` | PASS | 1 |
| `is-self:test-elf` | PASS | 0 |
| `is-ucp:minimal-header` | PASS | 1 |
| `make-fself:size` | PASS | 1040 |
| `make-fself:bytes` | PASS | exact byte comparison |
| `make-fself:is-self` | PASS | 1 |
| `detect-package-type:meta` | PASS | expected=0 |
| `detect-package-type:full-debug` | PASS | expected=2 |
| `detect-package-type:full-retail` | PASS | expected=1 |
| `detect-package-type:unknown` | PASS | expected=-1 |
| `encrypt-pfs-image:bytes` | PASS | exact byte comparison; bytes=12288 |
| `pfsc-pack-unpack:csharp-baseline` | INFO | pack_rc=-1; error=SHA3-256 is required for the PS5 PFSv3 compression format but is not available on this host. |
| `pfsc-pack-unpack:cpp-zlib` | PASS | pack_rc=0; unpack_rc=131072; bytes=131072 |
| `pfsc-pfsv3-stored:cpp-cabi` | PASS | pack_rc=0; unpack_rc=266273; version=3; bytes=267297 |
| `build-inner-image:cpp-cabi` | PASS | path=/var/folders/ms/6xb1xj5x6rz0_ldg1s_zhp1c0000gn/T/tmpso04k4md/inner.img; bytes=196608; mode=0xc |
| `build-inner-image-pfsv3:cpp-cabi` | PASS | path=/var/folders/ms/6xb1xj5x6rz0_ldg1s_zhp1c0000gn/T/tmpso04k4md/inner-pfsv3.img; bytes=197632; version=3 |
| `build-package:cpp-cabi` | PASS | path=/var/folders/ms/6xb1xj5x6rz0_ldg1s_zhp1c0000gn/T/tmpso04k4md/pkg/UP9000-PPSA00000_00-PROSPERO00000000-A0123-V0123.pkg; bytes=264704; detected=2 |

## Performance

| Case | Iterations | C# us/call | C++ us/call | Speedup |
|---|---:|---:|---:|---:|
| `is_valid_content_id` | 100000 | 1.028 | 0.476 | 2.16x |
| `compose_content_id` | 25000 | 3.643 | 2.234 | 1.63x |
| `is_elf` | 100000 | 0.906 | 0.953 | 0.95x |
| `make_fself` | 500 | 7.006 | 4.395 | 1.59x |

## Binary Size

| Artifact | Bytes |
|---|---:|
| C# `LibProsperoPkg` | 6064504 |
| C# baseline folder | 34378430 |
| C++ `LibProsperoPkg` | 164392 |
| C++ tools total | 295104 |
| C++ library + tools | 459496 |
| C++ `prosperopkg-fself` | 42288 |
| C++ `prosperopkg-gp5` | 62944 |
| C++ `prosperopkg-inspect` | 86688 |
| C++ `prosperopkg-keys` | 39568 |
| C++ `prosperopkg-lzn` | 63616 |

## Known C++ Gaps

- None in the exported C ABI surface.
