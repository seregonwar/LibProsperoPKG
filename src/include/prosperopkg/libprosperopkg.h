/*
 * LibProsperoPkg - A library for building and inspecting PS5 packages.
 * C++ port/rewrite Copyright (C) 2026 seregonwar.
 * Original C# LibProsperoPkg by SvenGDK.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LIBPROSPEROPKG_H
#define LIBPROSPEROPKG_H

#ifdef _WIN32
#  ifdef LIBPROSPEROPKG_C_API_BUILD
#    define LPP_API __declspec(dllexport)
#  else
#    define LPP_API __declspec(dllimport)
#  endif
#else
#  define LPP_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LPP_MODE_APPLICATION                 0
#define LPP_MODE_HOMEBREW                    1
#define LPP_MODE_ADDITIONAL_CONTENT_DATA     2
#define LPP_MODE_ADDITIONAL_CONTENT_NO_DATA  3

#define LPP_OUTPUT_METADATA_CONTAINER  0
#define LPP_OUTPUT_DEBUG_IMAGE         1

#define LPP_INNER_NONE    0
#define LPP_INNER_ZLIB    1
#define LPP_INNER_KRAKEN  2

#define LPP_FORM_PLAINTEXT         0
#define LPP_FORM_ENCRYPTED         1
#define LPP_FORM_COMPRESSED        2
#define LPP_FORM_KRAKEN_COMPRESSED 3

#define LPP_TYPE_META         0
#define LPP_TYPE_FULL_RETAIL  1
#define LPP_TYPE_FULL_DEBUG   2

LPP_API const char* lpp_version(void);
LPP_API int lpp_last_error(char* buffer, int capacity);

LPP_API int lpp_is_valid_content_id(const char* content_id);
LPP_API int lpp_is_valid_title_id(const char* title_id);
LPP_API int lpp_compose_content_id(const char* publisher,
                                   const char* title_id,
                                   const char* label,
                                   char* out_buffer,
                                   int capacity);

LPP_API int lpp_build_package(const char* source_folder,
                              const char* output_folder,
                              const char* content_id,
                              const char* passcode,
                              const char* title,
                              const char* title_id,
                              const char* version,
                              int mode,
                              int output_format,
                              int inner_compression,
                              char* out_path,
                              int out_path_capacity);

LPP_API int lpp_detect_package_type(const char* path);

LPP_API int lpp_build_inner_image(const char* source_folder,
                                  const char* output_path,
                                  const char* content_id,
                                  const char* passcode,
                                  int form,
                                  char* out_path,
                                  int out_path_capacity);

LPP_API int lpp_encrypt_pfs_image(const char* pfs_image_path,
                                  const char* content_id,
                                  const char* passcode);

LPP_API int lpp_pack_pfs_image(const char* input_image_path,
                               const char* output_path,
                               int level,
                               int block_size);

LPP_API long long lpp_unpack_pfs_image(const char* input_path, const char* output_path);

LPP_API int lpp_is_self(const unsigned char* data, int length);
LPP_API int lpp_is_elf(const unsigned char* data, int length);
LPP_API int lpp_is_ucp(const unsigned char* data, int length);

LPP_API int lpp_make_fself(const unsigned char* elf,
                           int elf_length,
                           unsigned char* out_buffer,
                           int capacity);

#ifdef __cplusplus
}
#endif

#endif /* LIBPROSPEROPKG_H */
