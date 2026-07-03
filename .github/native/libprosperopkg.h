/*
 * libprosperopkg.h - C interface for the LibProsperoPkg shared library.
 *
 * The shared library (libLibProsperoPkg.so / libLibProsperoPkg.dylib) is produced by the
 * native build workflows. All strings are UTF-8 and NUL-terminated. Output strings are
 * written into caller-provided buffers; the library allocates no memory the caller must free.
 *
 * String-output functions return the number of bytes written on success. When the buffer is
 * too small they return the negative of the required size (including the terminator), so a
 * caller can size a buffer and retry.
 */

#ifndef LIBPROSPEROPKG_H
#define LIBPROSPEROPKG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Build mode (lpp_build_package `mode`). */
#define LPP_MODE_APPLICATION                 0
#define LPP_MODE_HOMEBREW                    1
#define LPP_MODE_ADDITIONAL_CONTENT_DATA     2
#define LPP_MODE_ADDITIONAL_CONTENT_NO_DATA  3

/* Output container format (lpp_build_package `output_format`). */
#define LPP_OUTPUT_METADATA_CONTAINER  0
#define LPP_OUTPUT_DEBUG_IMAGE         1

/* Inner-image codec (lpp_build_package `inner_compression`). */
#define LPP_INNER_NONE    0
#define LPP_INNER_ZLIB    1
#define LPP_INNER_KRAKEN  2

/* Inner-image form (lpp_build_inner_image `form`). */
#define LPP_FORM_PLAINTEXT         0
#define LPP_FORM_ENCRYPTED         1
#define LPP_FORM_COMPRESSED        2
#define LPP_FORM_KRAKEN_COMPRESSED 3

/* Package type (lpp_detect_package_type return value). */
#define LPP_TYPE_META         0
#define LPP_TYPE_FULL_RETAIL  1
#define LPP_TYPE_FULL_DEBUG   2

/* Returns a pointer to a static, NUL-terminated version string. */
const char* lpp_version(void);

/* Copies the current thread's most recent error message into `buffer`. */
int lpp_last_error(char* buffer, int capacity);

/* Returns 1 when `content_id` is a valid 36-character content id, otherwise 0. */
int lpp_is_valid_content_id(const char* content_id);

/* Returns 1 when `title_id` looks like a PPSAxxxxx title id, otherwise 0. */
int lpp_is_valid_title_id(const char* title_id);

/*
 * Composes a 36-character content id from a publisher prefix, a title id and a label.
 * Any argument may be NULL to accept the default for that field. Writes the result into
 * `out_buffer` as UTF-8.
 */
int lpp_compose_content_id(const char* publisher, const char* title_id, const char* label,
                           char* out_buffer, int capacity);

/*
 * Builds a package from a prepared source folder. `passcode` and `version` may be NULL/empty
 * to accept their defaults (a 32-zero passcode and "01.00"). On success the output path is
 * written to `out_path` and the function returns 0. On failure it returns a negative value;
 * call lpp_last_error for a description.
 */
int lpp_build_package(const char* source_folder,
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

/*
 * Detects the package type of the file at `path`. Returns the package type (LPP_TYPE_*), or -1
 * when the file is not a recognized package or cannot be read (call lpp_last_error).
 */
int lpp_detect_package_type(const char* path);

/*
 * Lays a prepared folder out into an inner-PFS image. `form` selects the image form (LPP_FORM_*).
 * `passcode` may be NULL/empty to accept the 32-zero default. The written image path is copied
 * into `out_path`. Returns 0 on success; a negative value on failure (call lpp_last_error).
 */
int lpp_build_inner_image(const char* source_folder,
                          const char* output_path,
                          const char* content_id,
                          const char* passcode,
                          int form,
                          char* out_path,
                          int out_path_capacity);

/*
 * AES-XTS-encrypts a plaintext inner-PFS image in place, using keys derived from the content id
 * and passcode. `passcode` may be NULL/empty to accept the 32-zero default. Returns 0 on success;
 * a negative value on failure (call lpp_last_error).
 */
int lpp_encrypt_pfs_image(const char* pfs_image_path, const char* content_id, const char* passcode);

/*
 * Packs a plaintext PFS image into a PFSv3 PFSC (Kraken) container. A non-positive `level` or
 * `block_size` selects the default (7 / 262144). Returns 0 on success; a negative value on
 * failure (call lpp_last_error).
 */
int lpp_pack_pfs_image(const char* input_image_path, const char* output_path,
                       int level, int block_size);

/*
 * Unpacks a PFSv3 PFSC container back into a plaintext PFS image. Returns the number of bytes
 * written on success, or -1 on failure (call lpp_last_error).
 */
long long lpp_unpack_pfs_image(const char* input_path, const char* output_path);

/* Returns 1 when `data` (`length` bytes) holds a SELF container, otherwise 0. */
int lpp_is_self(const unsigned char* data, int length);

/* Returns 1 when `data` (`length` bytes) holds a 64-bit ELF, otherwise 0. */
int lpp_is_elf(const unsigned char* data, int length);

/* Returns 1 when `data` (`length` bytes) holds a UCP archive, otherwise 0. */
int lpp_is_ucp(const unsigned char* data, int length);

/*
 * Generates a fake-self from a 64-bit ELF. Pass out_buffer=NULL or capacity=0 to query the
 * required size (returned positive, nothing written). On success returns the number of bytes
 * written. Returns -1 and sets lpp_last_error on failure or when a non-zero buffer is too small.
 */
int lpp_make_fself(const unsigned char* elf, int elf_length,
                   unsigned char* out_buffer, int capacity);

#ifdef __cplusplus
}
#endif

#endif /* LIBPROSPEROPKG_H */
