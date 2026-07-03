/*
 * psxpkg.h - PlayStation Package Reader (PS4 + PS5, single-header C11)
 * Copyright (C) 2026 seregonwar
 *
 * Optional reference reader for the LibProsperoPkg C++ rewrite/fork.
 * Not linked into the native CMake library or tools unless explicitly integrated.
 * Licensed under the GNU General Public License v3.0 or later.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* _POSIX_C_SOURCE must precede all system headers for fseeko/ftello */
#if !defined(_WIN32) && !defined(_WIN64)
#  if !defined(_POSIX_C_SOURCE) || (_POSIX_C_SOURCE < 200112L)
#    undef  _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200112L
#  endif
#endif

/*
 ******************************************************************************
 * psxpkg.h  -  PlayStation Package Reader  (PS4 + PS5, single-header C11)
 * Version 1.0.0
 ******************************************************************************
 *
 * Reads PS4 and PS5 .pkg files with a single, unified API.
 * Zero external dependencies. Works on Windows, Linux and macOS.
 *
 * USAGE
 *   In exactly ONE translation unit:
 *       #define PSXPKG_IMPLEMENTATION
 *       #include "psxpkg.h"
 *
 *   In every other translation unit just include the header:
 *       #include "psxpkg.h"
 *
 * QUICK EXAMPLE
 *   psxpkg_t *pkg = NULL;
 *   if (psxpkg_open("game.pkg", NULL, &pkg) == PSXPKG_OK) {
 *       if (psxpkg_get_type(pkg, NULL) == PSXPKG_OK) {
 *           // PS4 path
 *           psxpkg_ps4_param_t p4;
 *           if (psxpkg_read_ps4_param(pkg, &p4) == PSXPKG_OK)
 *               printf("PS4 Title: %s  ID: %s\n", p4.title, p4.title_id);
 *           // PS5 path
 *           psxpkg_ps5_param_t p5;
 *           if (psxpkg_read_ps5_param(pkg, &p5) == PSXPKG_OK)
 *               printf("PS5 Title ID: %s\n", p5.title_id);
 *       }
 *       // Images work the same for both platforms
 *       psxpkg_read_images(pkg, my_cb, NULL);
 *       psxpkg_close(pkg);
 *   }
 *
 * PLATFORMS
 *   Windows (MSVC >= 2019, MinGW-w64), Linux (GCC/Clang), macOS (Clang).
 *   On 32-bit Linux compile with -D_FILE_OFFSET_BITS=64 for large files.
 *
 * LICENSE: GPL-3.0-or-later (see repository LICENSE)
 ******************************************************************************
 */

#ifndef PSXPKG_H
#define PSXPKG_H

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 201112L)
#  if !defined(_MSC_VER)
#    error "psxpkg.h requires C11 (compile with -std=c11 or later)."
#  endif
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * VERSION
 * ========================================================================= */
#define PSXPKG_VERSION_MAJOR  1
#define PSXPKG_VERSION_MINOR  0
#define PSXPKG_VERSION_PATCH  0
#define PSXPKG_VERSION_STRING "1.0.0"

/* =========================================================================
 * ERROR CODES
 * ========================================================================= */

/**
 * @brief Return type for every library function.
 * Errors are negative; PSXPKG_OK is zero.
 * Use psxpkg_error_string() for a readable description.
 */
typedef enum psxpkg_error
{
    PSXPKG_OK                =  0,
    PSXPKG_ERR_INVALID_PARAM = -1,  /* NULL pointer or bad argument          */
    PSXPKG_ERR_IO            = -2,  /* File open / seek / read failure       */
    PSXPKG_ERR_NOT_A_PKG     = -3,  /* Header magic not recognised           */
    PSXPKG_ERR_NOT_FOUND     = -4,  /* Requested asset not in PKG            */
    PSXPKG_ERR_CORRUPT       = -5,  /* Integrity / format check failed       */
    PSXPKG_ERR_OVERFLOW      = -6,  /* Arithmetic would overflow             */
    PSXPKG_ERR_NO_MEMORY     = -7,  /* Allocator returned NULL               */
    PSXPKG_ERR_UNSUPPORTED   = -8,  /* Operation invalid for this PKG type   */
    PSXPKG_ERR_TOO_LARGE     = -9,  /* Asset exceeds PSXPKG_MAX_ASSET_BYTES  */
    PSXPKG_ERR_ABORTED       = -10, /* Image callback returned false         */
    PSXPKG_ERR_WRONG_PLATFORM= -11, /* Function called on wrong platform     */
} psxpkg_error_t;

/* =========================================================================
 * PKG TYPE
 * ========================================================================= */

/**
 * @brief PKG classification determined from the file header.
 *
 * Detection rules (applied in order):
 *  1. Magic = 0x7F464948 ("\x7fFIH")  ->  PS5 Full (retail or debug)
 *  2. Magic = 0x7F434E54 ("\x7fCNT") AND tail zeros present  ->  PS5 Meta
 *  3. Magic = 0x7F434E54 ("\x7fCNT") AND no tail zeros       ->  PS4
 */
typedef enum psxpkg_type
{
    PSXPKG_TYPE_UNKNOWN     = 0,
    PSXPKG_TYPE_PS4         = 1,  /* PS4 package (param.sfo metadata)        */
    PSXPKG_TYPE_PS5_META    = 2,  /* PS5 meta/patch envelope                 */
    PSXPKG_TYPE_PS5_RETAIL  = 3,  /* PS5 full retail (signed)                */
    PSXPKG_TYPE_PS5_DEBUG   = 4,  /* PS5 full debug (unsigned, devkit)       */
} psxpkg_type_t;

/* =========================================================================
 * IMAGE KIND
 * ========================================================================= */

/**
 * @brief Classification of an embedded PNG by pixel dimensions or entry ID.
 */
typedef enum psxpkg_image_kind
{
    PSXPKG_IMAGE_ICON       = 0,  /* Icon  (PS4: 320x176 or 512x512, PS5: 512x512)   */
    PSXPKG_IMAGE_BACKGROUND = 1,  /* Background (PS4: 1920x1080, PS5: 3840x2160)     */
    PSXPKG_IMAGE_OTHER      = 2,  /* Any other PNG                                   */
} psxpkg_image_kind_t;

/* =========================================================================
 * COMPILE-TIME LIMITS
 * ========================================================================= */

/**
 * @brief Hard ceiling on any single extracted asset (default 256 MiB).
 * Override before including: #define PSXPKG_MAX_ASSET_BYTES (512U*1024U*1024U)
 */
#ifndef PSXPKG_MAX_ASSET_BYTES
#  define PSXPKG_MAX_ASSET_BYTES ((size_t)(256U * 1024U * 1024U))
#endif

/** Maximum byte length (including NUL) of every string field. */
#define PSXPKG_STR_MAX  512U

/* =========================================================================
 * PS4 PARAMETER STRUCTURE  (from param.sfo / SFO binary format)
 * ========================================================================= */

/**
 * @brief Metadata extracted from a PS4 PKG's embedded param.sfo.
 *
 * Every string is UTF-8, NUL-terminated, at most PSXPKG_STR_MAX bytes.
 * Empty string = field not present.  Integer fields are 0 when absent.
 *
 * The 'region' field is inferred from 'title_id':
 *   CUSA -> "North America/Europe"  |  PCAS / PCJS -> "Asia / Japan"
 *   PCSF / PCSB -> "Europe"         |  other -> ""
 */
typedef struct psxpkg_ps4_param
{
    /* Identification */
    char title_id   [PSXPKG_STR_MAX]; /* TITLE_ID   e.g. "CUSA12345"          */
    char content_id [PSXPKG_STR_MAX]; /* CONTENT_ID full unique identifier     */
    char title      [PSXPKG_STR_MAX]; /* TITLE      default/English title      */
    char title_00   [PSXPKG_STR_MAX]; /* TITLE_00   additional localized title */
    char title_01   [PSXPKG_STR_MAX]; /* TITLE_01                              */
    char title_02   [PSXPKG_STR_MAX]; /* TITLE_02                              */
    char title_03   [PSXPKG_STR_MAX]; /* TITLE_03                              */
    char title_04   [PSXPKG_STR_MAX]; /* TITLE_04                              */
    char title_05   [PSXPKG_STR_MAX]; /* TITLE_05                              */
    char title_06   [PSXPKG_STR_MAX]; /* TITLE_06                              */
    char title_07   [PSXPKG_STR_MAX]; /* TITLE_07                              */

    /* Versions */
    char app_ver    [PSXPKG_STR_MAX]; /* APP_VER     application version       */
    char system_ver [PSXPKG_STR_MAX]; /* SYSTEM_VER  minimum firmware required */
    char version    [PSXPKG_STR_MAX]; /* VERSION                               */

    /* Category / flags */
    char category   [PSXPKG_STR_MAX]; /* CATEGORY  "gd"=game disc,"gp"=patch  */

    /* PKG header fields (decoded from binary header, not SFO) */
    char pkg_content_type [PSXPKG_STR_MAX]; /* Human-readable content type     */
    char pkg_drm_type     [PSXPKG_STR_MAX]; /* Human-readable DRM type         */
    char pkg_content_flags[PSXPKG_STR_MAX]; /* Comma-separated content flags   */
    char region           [PSXPKG_STR_MAX]; /* Inferred from title_id          */

    /* Integer SFO fields */
    int32_t parental_level;   /* PARENTAL_LEVEL */
    int32_t attribute;        /* ATTRIBUTE      */
    int32_t resolution;       /* RESOLUTION     */
    int32_t sound_format;     /* SOUND_FORMAT   */

    /* Raw PKG header integers */
    uint32_t raw_content_type;  /* pkg_content_type  as uint32  */
    uint32_t raw_drm_type;      /* pkg_drm_type      as uint32  */
    uint32_t raw_content_flags; /* pkg_content_flags as uint32  */
} psxpkg_ps4_param_t;

/* =========================================================================
 * PS5 PARAMETER STRUCTURE  (from param.json / JSON format)
 * ========================================================================= */

/**
 * @brief Per-locale title names from localizedParameters (PS5 param.json).
 * An empty string means the locale was absent.
 */
typedef struct psxpkg_ps5_locales
{
    char en_us [PSXPKG_STR_MAX];
    char en_gb [PSXPKG_STR_MAX];
    char de_de [PSXPKG_STR_MAX];
    char fr_fr [PSXPKG_STR_MAX];
    char it_it [PSXPKG_STR_MAX];
    char es_es [PSXPKG_STR_MAX];
    char es_419[PSXPKG_STR_MAX];
    char ja_jp [PSXPKG_STR_MAX];
    char ko_kr [PSXPKG_STR_MAX];
    char pt_br [PSXPKG_STR_MAX];
    char pt_pt [PSXPKG_STR_MAX];
    char zh_hans[PSXPKG_STR_MAX];
    char zh_hant[PSXPKG_STR_MAX];
    char ru_ru [PSXPKG_STR_MAX];
    char pl_pl [PSXPKG_STR_MAX];
    char tr_tr [PSXPKG_STR_MAX];
    char nl_nl [PSXPKG_STR_MAX];
    char sv_se [PSXPKG_STR_MAX];
    char fi_fi [PSXPKG_STR_MAX];
    char da_dk [PSXPKG_STR_MAX];
    char ar_ae [PSXPKG_STR_MAX];
    char th_th [PSXPKG_STR_MAX];
    char cs_cz [PSXPKG_STR_MAX];
    char hu_hu [PSXPKG_STR_MAX];
    char ro_ro [PSXPKG_STR_MAX];
    char el_gr [PSXPKG_STR_MAX];
    char id_id [PSXPKG_STR_MAX];
    char vi_vn [PSXPKG_STR_MAX];
    char no_no [PSXPKG_STR_MAX];
    char fr_ca [PSXPKG_STR_MAX];
} psxpkg_ps5_locales_t;

/**
 * @brief Metadata parsed from a PS5 PKG's embedded param.json.
 *
 * All strings are UTF-8, NUL-terminated, at most PSXPKG_STR_MAX bytes.
 * Empty string = field absent.  Integer fields are 0 when absent.
 *
 * Region inferred from title_id:
 *   PPSA -> "NA / Europe" | ECAS / ELAS -> "Asia" | ELJM -> "Japan"
 */
typedef struct psxpkg_ps5_param
{
    char title_id        [PSXPKG_STR_MAX];
    char content_id      [PSXPKG_STR_MAX];
    char content_version [PSXPKG_STR_MAX];
    char master_version  [PSXPKG_STR_MAX];
    char sdk_version     [PSXPKG_STR_MAX];
    char required_firmware[PSXPKG_STR_MAX];
    char drm_type        [PSXPKG_STR_MAX];
    char pubtool_version [PSXPKG_STR_MAX];
    char creation_date   [PSXPKG_STR_MAX];
    char version_file_uri[PSXPKG_STR_MAX];
    char region          [PSXPKG_STR_MAX];

    psxpkg_ps5_locales_t titles;

    int32_t  category_type;
    int64_t  download_data_size;
} psxpkg_ps5_param_t;

/* =========================================================================
 * IMAGE STRUCTURE
 * ========================================================================= */

/**
 * @brief A single PNG image extracted from a PKG.
 *
 * 'data' is valid ONLY for the duration of the psxpkg_image_callback_t call.
 * Copy the bytes if you need them after the callback returns.
 */
typedef struct psxpkg_image
{
    psxpkg_image_kind_t kind;   /* Icon, Background, or Other    */
    uint32_t            width;  /* Width  in pixels (PNG IHDR)   */
    uint32_t            height; /* Height in pixels (PNG IHDR)   */
    const uint8_t*      data;   /* Complete PNG file bytes        */
    size_t              size;   /* Byte count                     */
    uint32_t            index;  /* Zero-based delivery index      */
} psxpkg_image_t;

/* =========================================================================
 * ALLOCATOR
 * ========================================================================= */

/**
 * @brief Optional custom allocator passed to psxpkg_open().
 * Pass NULL to use the standard malloc/free.
 */
typedef struct psxpkg_allocator
{
    void* (*alloc)(size_t size, void* userdata); /* Returns NULL on failure  */
    void  (*free) (void*  ptr,  void* userdata); /* NULL ptr must be no-op   */
    void* userdata;
} psxpkg_allocator_t;

/**
 * @brief Callback invoked once per PNG during psxpkg_read_images().
 * Return true to continue, false to abort.
 */
typedef bool (*psxpkg_image_callback_t)(const psxpkg_image_t* image,
                                         void*                  userdata);

/**
 * @brief Opaque PKG context. Create with psxpkg_open(). Destroy with psxpkg_close().
 */
typedef struct psxpkg_ctx psxpkg_t;

/* =========================================================================
 * PUBLIC API
 * ========================================================================= */

/** Return library version string (e.g. "1.0.0"). Thread-safe. */
const char* psxpkg_version(void);

/** Convert error code to English description. Thread-safe. */
const char* psxpkg_error_string(psxpkg_error_t err);

/**
 * @brief Open a PS4 or PS5 .pkg file and create a context.
 *
 * Auto-detects the platform and PKG type from the file header.
 * The file is kept open (read-only) until psxpkg_close().
 *
 * @param path      File path (UTF-8 / system encoding). Must not be NULL.
 * @param allocator Custom allocator, or NULL for malloc/free.
 * @param out_pkg   Receives the handle on success; NULL on failure.
 *
 * @return PSXPKG_OK              on success.
 *         PSXPKG_ERR_INVALID_PARAM if path or out_pkg is NULL.
 *         PSXPKG_ERR_IO            if the file cannot be opened/read.
 *         PSXPKG_ERR_NOT_A_PKG     if the header is not a PS4/PS5 PKG.
 *         PSXPKG_ERR_NO_MEMORY     if context allocation fails.
 */
psxpkg_error_t psxpkg_open(const char*                path,
                             const psxpkg_allocator_t*  allocator,
                             psxpkg_t**                 out_pkg);

/** Close context and release all resources. NULL-safe (no-op). */
void psxpkg_close(psxpkg_t* pkg);

/**
 * @brief Query the PKG type.
 *
 * @param pkg      Valid open context.
 * @param out_type Receives the type. May be NULL if you only need the error.
 *
 * @return PSXPKG_OK or PSXPKG_ERR_INVALID_PARAM (pkg is NULL).
 */
psxpkg_error_t psxpkg_get_type(const psxpkg_t* pkg, psxpkg_type_t* out_type);

/**
 * @brief Read and parse the PS4 param.sfo embedded in the PKG.
 *
 * Only valid for PSXPKG_TYPE_PS4 packages.
 * On error, *out_param is zero-initialised before returning.
 *
 * @return PSXPKG_OK              on success.
 *         PSXPKG_ERR_WRONG_PLATFORM if the PKG is not PS4.
 *         PSXPKG_ERR_NOT_FOUND     if param.sfo is not present.
 *         PSXPKG_ERR_CORRUPT       if the SFO binary is malformed.
 *         PSXPKG_ERR_IO            on read failure.
 *         PSXPKG_ERR_NO_MEMORY     on allocation failure.
 *         PSXPKG_ERR_TOO_LARGE     if param.sfo exceeds 256 KiB.
 */
psxpkg_error_t psxpkg_read_ps4_param(psxpkg_t*           pkg,
                                       psxpkg_ps4_param_t* out_param);

/**
 * @brief Read and parse the PS5 param.json embedded in the PKG.
 *
 * Valid for PSXPKG_TYPE_PS5_RETAIL and PSXPKG_TYPE_PS5_DEBUG.
 * PS5_META packages return PSXPKG_ERR_UNSUPPORTED.
 * On error, *out_param is zero-initialised before returning.
 *
 * @return PSXPKG_OK              on success.
 *         PSXPKG_ERR_WRONG_PLATFORM if the PKG is not PS5.
 *         PSXPKG_ERR_UNSUPPORTED    if the PKG is PS5_META.
 *         PSXPKG_ERR_NOT_FOUND      if param.json is not located.
 *         PSXPKG_ERR_CORRUPT        if the JSON is malformed.
 *         PSXPKG_ERR_IO             on read failure.
 *         PSXPKG_ERR_NO_MEMORY      on allocation failure.
 *         PSXPKG_ERR_TOO_LARGE      if param.json exceeds 256 KiB.
 */
psxpkg_error_t psxpkg_read_ps5_param(psxpkg_t*           pkg,
                                       psxpkg_ps5_param_t* out_param);

/**
 * @brief Extract all PNG images from the PKG (PS4 and PS5).
 *
 * - For PS4: uses the structured entry table (direct offset reads).
 * - For PS5: scans for PNG signatures in the last 80 MiB.
 *
 * Each valid PNG is delivered once to 'callback'. Blank placeholder images
 * below a minimum byte threshold are silently skipped.
 *
 * @param pkg       Valid open context.
 * @param callback  Called once per image. Return false to abort.
 * @param userdata  Forwarded to every callback call.
 *
 * @return PSXPKG_OK              if all images were delivered.
 *         PSXPKG_ERR_NOT_FOUND   if no images were found.
 *         PSXPKG_ERR_IO          on read failure.
 *         PSXPKG_ERR_NO_MEMORY   on allocation failure.
 *         PSXPKG_ERR_ABORTED     if callback returned false.
 */
psxpkg_error_t psxpkg_read_images(psxpkg_t*               pkg,
                                    psxpkg_image_callback_t  callback,
                                    void*                    userdata);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* #########################################################################
 * IMPLEMENTATION
 * ######################################################################### */
#ifdef PSXPKG_IMPLEMENTATION

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#if !defined(_WIN32) && !defined(_WIN64)
#  include <sys/types.h>   /* off_t */
#endif

/* ---- Platform large-file seek/tell shims ---- */
#if defined(_WIN32) || defined(_WIN64)
#  define psxpkg__fseek(f,off,org)  _fseeki64((f),(off),(org))
#  define psxpkg__ftell(f)          _ftelli64(f)
#else
#  define psxpkg__fseek(f,off,org)  fseeko((f),(off_t)(off),(org))
#  define psxpkg__ftell(f)          ((int64_t)ftello(f))
#endif

/* =========================================================================
 * INTERNAL CONSTANTS
 * ========================================================================= */

/* PNG signature (8 bytes) */
static const uint8_t K_PNG_SIG[8]  = {
    0x89U,0x50U,0x4EU,0x47U,0x0DU,0x0AU,0x1AU,0x0AU
};
/* PNG IEND chunk (12 bytes: 4-byte zero length + "IEND" + CRC) */
static const uint8_t K_PNG_IEND[12] = {
    0x00U,0x00U,0x00U,0x00U,
    0x49U,0x45U,0x4EU,0x44U,
    0xAEU,0x42U,0x60U,0x82U
};

/* PS4/PS5-META magic: "\x7fCNT" = 0x7F434E54 in big-endian */
static const uint8_t K_MAGIC_CNT[4] = { 0x7FU,0x43U,0x4EU,0x54U };
/* PS5-FULL magic:     "\x7fFIH" = 0x7F464948 in big-endian */
static const uint8_t K_MAGIC_FIH[4] = { 0x7FU,0x46U,0x49U,0x48U };

/* PSF (param.sfo) magic: "\x00PSF" stored little-endian at offset 0 */
static const uint8_t K_PSF_MAGIC[4] = { 0x00U,0x50U,0x53U,0x46U };

#define K_PS5_RETAIL_BYTE  ((uint8_t)0x80U)
#define K_CHUNK            4096U
#define K_TAIL_ZEROS       10U
#define K_MAX_PNGS         64U
#define K_PARAM_RANGE      ((int64_t)(64L  * 1024L * 1024L))
#define K_PNG_RANGE        ((int64_t)(80L  * 1024L * 1024L))
#define K_ASSET_MAX        (256U * 1024U)

/* PS4 PKGEntry well-known IDs */
#define PS4_ENTRY_PARAM_SFO  0x1000U
#define PS4_ENTRY_ICON0_PNG  0x1200U
#define PS4_ENTRY_PIC0_PNG   0x1220U
#define PS4_ENTRY_PIC1_PNG   0x1006U
#define PS4_ENTRY_SND0_AT9   0x1240U

/* Minimum PNG sizes to skip blank placeholders */
#define K_ICON_MIN  (10U  * 1024U)
#define K_BG_MIN    (100U * 1024U)

/* =========================================================================
 * MEMORY PAL
 * ========================================================================= */

typedef struct {
    void* (*alloc)(size_t, void*);
    void  (*free) (void*,  void*);
    void* ud;
} psxpkg__mem_t;

static void* psxpkg__dflt_alloc(size_t n, void* ud) { (void)ud; return malloc(n); }
static void  psxpkg__dflt_free (void* p,  void* ud) { (void)ud; free(p); }

static psxpkg__mem_t psxpkg__mem_resolve(const psxpkg_allocator_t* u)
{
    psxpkg__mem_t m;
    if (u && u->alloc && u->free) {
        m.alloc = u->alloc; m.free = u->free; m.ud = u->userdata;
    } else {
        m.alloc = psxpkg__dflt_alloc; m.free = psxpkg__dflt_free; m.ud = NULL;
    }
    return m;
}

static inline void* psxpkg__alloc(const psxpkg__mem_t* m, size_t n)
{ assert(m && n > 0U); return m->alloc(n, m->ud); }

static inline void  psxpkg__free(const psxpkg__mem_t* m, void* p)
{ assert(m); if (p) m->free(p, m->ud); }

/* =========================================================================
 * OPAQUE CONTEXT
 * ========================================================================= */

/* PS4 entry table cache entry (32 bytes each in file, stored parsed here) */
typedef struct {
    uint32_t id;
    uint32_t offset;  /* direct file offset */
    uint32_t size;
} psxpkg__ps4_entry_t;

struct psxpkg_ctx
{
    FILE*               fp;
    psxpkg__mem_t       mem;
    psxpkg_type_t       type;
    int64_t             file_size;

    /* PS4-only: cached entry table */
    psxpkg__ps4_entry_t* ps4_entries;
    uint32_t             ps4_entry_count;

    /* PS4-only: raw header fields for param decoding */
    uint32_t ps4_raw_content_type;
    uint32_t ps4_raw_drm_type;
    uint32_t ps4_raw_content_flags;
};

/* =========================================================================
 * FILE I/O HELPERS
 * ========================================================================= */

static size_t psxpkg__read_exact(FILE* fp, void* buf, size_t n)
{
    assert(fp && buf);
    uint8_t* d = (uint8_t*)buf;
    size_t   t = 0U;
    while (t < n) { size_t g = fread(d + t, 1U, n - t, fp); if (!g) break; t += g; }
    return t;
}

static bool psxpkg__read_at(FILE* fp, int64_t off, void* buf, size_t n)
{
    assert(fp && buf && n > 0 && off >= 0);
    if (psxpkg__fseek(fp, off, SEEK_SET) != 0) return false;
    return psxpkg__read_exact(fp, buf, n) == n;
}

static int64_t psxpkg__file_size(FILE* fp)
{
    assert(fp);
    if (psxpkg__fseek(fp, 0, SEEK_END) != 0) return -1;
    int64_t s = psxpkg__ftell(fp);
    (void)psxpkg__fseek(fp, 0, SEEK_SET);
    return s;
}

/* =========================================================================
 * BIG-ENDIAN READ HELPERS  (PS4 header is all big-endian)
 * ========================================================================= */

static uint16_t psxpkg__be16(const uint8_t* b)
{ return (uint16_t)((uint16_t)b[0] << 8 | b[1]); }

static uint32_t psxpkg__be32(const uint8_t* b)
{ return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3]; }

static __attribute__((unused)) uint64_t psxpkg__be64(const uint8_t* b)
{
    return ((uint64_t)b[0]<<56)|((uint64_t)b[1]<<48)|
           ((uint64_t)b[2]<<40)|((uint64_t)b[3]<<32)|
           ((uint64_t)b[4]<<24)|((uint64_t)b[5]<<16)|
           ((uint64_t)b[6]<<8) | (uint64_t)b[7];
}

/* =========================================================================
 * LITTLE-ENDIAN READ HELPERS  (PSF format fields)
 * ========================================================================= */

static uint16_t psxpkg__le16(const uint8_t* b)
{ return (uint16_t)((uint16_t)b[1] << 8 | b[0]); }

static uint32_t psxpkg__le32(const uint8_t* b)
{ return ((uint32_t)b[3]<<24)|((uint32_t)b[2]<<16)|((uint32_t)b[1]<<8)|b[0]; }

/* =========================================================================
 * IN-MEMORY PATTERN SEARCH
 * ========================================================================= */

static size_t psxpkg__mem_rfind(const uint8_t* h, size_t hl,
                                  const uint8_t* n, size_t nl)
{
    assert(h && n && nl > 0);
    if (nl > hl) return (size_t)-1;
    size_t lim = hl - nl;
    for (size_t i = lim; ; --i) {
        if (memcmp(h + i, n, nl) == 0) return i;
        if (i == 0) break;
    }
    return (size_t)-1;
}

static size_t psxpkg__mem_find(const uint8_t* h, size_t hl,
                                 const uint8_t* n, size_t nl)
{
    assert(h && n && nl > 0);
    if (nl > hl) return (size_t)-1;
    size_t lim = hl - nl;
    for (size_t i = 0; i <= lim; i++)
        if (memcmp(h + i, n, nl) == 0) return i;
    return (size_t)-1;
}

/* =========================================================================
 * STREAMING SCAN — BACKWARD (last occurrence)
 * ========================================================================= */
static psxpkg_error_t psxpkg__rfind(
    FILE* fp, int64_t begin, int64_t end,
    const uint8_t* ndl, size_t nlen,
    const psxpkg__mem_t* mem, int64_t* out)
{
    assert(fp && ndl && nlen > 0 && nlen <= K_CHUNK && mem && out && begin >= 0);
    *out = -1;
    if (begin >= end) return PSXPKG_OK;

    size_t   ovl = nlen - 1U;
    uint8_t* w   = (uint8_t*)psxpkg__alloc(mem, K_CHUNK + ovl + 1U);
    if (!w) return PSXPKG_ERR_NO_MEMORY;

    uint8_t* sto  = w + K_CHUNK;
    size_t   olen = 0;
    int64_t  pos  = end;
    psxpkg_error_t err = PSXPKG_OK;

    while (pos > begin) {
        int64_t av  = pos - begin;
        int64_t rd  = (av < (int64_t)K_CHUNK) ? av : (int64_t)K_CHUNK;
        int64_t cst = pos - rd;

        if (psxpkg__fseek(fp, cst, SEEK_SET) != 0) { err = PSXPKG_ERR_IO; break; }
        size_t got = psxpkg__read_exact(fp, w, (size_t)rd);
        if (!got) break;

        if (olen) memmove(w + got, sto, olen);
        size_t wlen = got + olen;

        size_t idx = psxpkg__mem_rfind(w, wlen, ndl, nlen);
        if (idx != (size_t)-1) { *out = cst + (int64_t)idx; break; }

        if (ovl) { size_t cp = (got < ovl) ? got : ovl; memcpy(sto, w, cp); olen = cp; }
        pos = cst;
    }

    psxpkg__free(mem, w);
    return err;
}

/* =========================================================================
 * STREAMING SCAN — FORWARD (first occurrence)
 * ========================================================================= */
static psxpkg_error_t psxpkg__ffind(
    FILE* fp, int64_t begin, int64_t end,
    const uint8_t* ndl, size_t nlen,
    const psxpkg__mem_t* mem, int64_t* out)
{
    assert(fp && ndl && nlen > 0 && nlen <= K_CHUNK && mem && out && begin >= 0);
    *out = -1;
    if (begin >= end) return PSXPKG_OK;

    size_t   ovl = nlen - 1U;
    uint8_t* w   = (uint8_t*)psxpkg__alloc(mem, K_CHUNK + ovl + 1U);
    if (!w) return PSXPKG_ERR_NO_MEMORY;

    size_t  olen = 0;
    int64_t pos  = begin;
    psxpkg_error_t err = PSXPKG_OK;

    if (psxpkg__fseek(fp, begin, SEEK_SET) != 0) {
        psxpkg__free(mem, w); return PSXPKG_ERR_IO;
    }

    while (pos < end) {
        int64_t av  = end - pos;
        int64_t rd  = (av < (int64_t)K_CHUNK) ? av : (int64_t)K_CHUNK;
        uint8_t* dst = w + ovl;

        size_t got = psxpkg__read_exact(fp, dst, (size_t)rd);
        if (!got) break;

        if (olen < ovl) memmove(w + olen, dst, got);
        size_t wlen = olen + got;

        size_t idx = psxpkg__mem_find(w, wlen, ndl, nlen);
        if (idx != (size_t)-1) {
            int64_t abs_pos = (pos - (int64_t)olen) + (int64_t)idx;
            if (abs_pos >= begin) { *out = abs_pos; break; }
        }

        if (ovl) { size_t cp = (wlen < ovl) ? wlen : ovl; memmove(w, w + wlen - cp, cp); olen = cp; }
        pos += (int64_t)got;
    }

    psxpkg__free(mem, w);
    return err;
}

/* =========================================================================
 * STREAMING SCAN — BACKWARD ALL OCCURRENCES (up to cap)
 * ========================================================================= */
static psxpkg_error_t psxpkg__rfind_all(
    FILE* fp, int64_t begin, int64_t end,
    const uint8_t* ndl, size_t nlen,
    const psxpkg__mem_t* mem,
    int64_t* positions, size_t cap, size_t* count)
{
    assert(fp && ndl && nlen > 0 && nlen <= K_CHUNK && mem && positions && count);
    if (begin >= end) return PSXPKG_OK;

    size_t   ovl = nlen - 1U;
    uint8_t* w   = (uint8_t*)psxpkg__alloc(mem, K_CHUNK + ovl + 1U);
    if (!w) return PSXPKG_ERR_NO_MEMORY;

    uint8_t* sto  = w + K_CHUNK;
    size_t   olen = 0;
    int64_t  pos  = end;
    psxpkg_error_t err = PSXPKG_OK;

    while (pos > begin && *count < cap) {
        int64_t av  = pos - begin;
        int64_t rd  = (av < (int64_t)K_CHUNK) ? av : (int64_t)K_CHUNK;
        int64_t cst = pos - rd;

        if (psxpkg__fseek(fp, cst, SEEK_SET) != 0) { err = PSXPKG_ERR_IO; break; }
        size_t got = psxpkg__read_exact(fp, w, (size_t)rd);
        if (!got) break;

        if (olen) memmove(w + got, sto, olen);
        size_t wlen = got + olen;

        size_t srch = wlen;
        while (srch >= nlen && *count < cap) {
            size_t idx = psxpkg__mem_rfind(w, srch, ndl, nlen);
            if (idx == (size_t)-1) break;
            int64_t abs_pos = cst + (int64_t)idx;
            if (abs_pos >= begin && abs_pos < end) positions[(*count)++] = abs_pos;
            srch = idx;
        }

        if (ovl) { size_t cp = (got < ovl) ? got : ovl; memcpy(sto, w, cp); olen = cp; }
        pos = cst;
    }

    psxpkg__free(mem, w);
    return err;
}

/* =========================================================================
 * STRING HELPERS
 * ========================================================================= */

static void psxpkg__scopy(char* dst, const char* src, size_t dsz)
{
    assert(dst && src);
    if (!dsz) return;
    size_t i = 0;
    while (i + 1 < dsz && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* =========================================================================
 * PKG TYPE DETECTION
 * ========================================================================= */

static bool psxpkg__valid_tail(FILE* fp, int64_t fsz)
{
    if (fsz < (int64_t)K_TAIL_ZEROS) return false;
    uint8_t tail[K_TAIL_ZEROS];
    if (!psxpkg__read_at(fp, fsz - (int64_t)K_TAIL_ZEROS, tail, K_TAIL_ZEROS)) return false;
    for (size_t i = 0; i < K_TAIL_ZEROS; i++) if (tail[i]) return false;
    return true;
}

static psxpkg_type_t psxpkg__detect_type(FILE* fp, int64_t fsz)
{
    uint8_t h[6];
    if (!psxpkg__read_at(fp, 0, h, 6)) return PSXPKG_TYPE_UNKNOWN;

    if (memcmp(h, K_MAGIC_FIH, 4) == 0) {
        return (h[5] == K_PS5_RETAIL_BYTE) ? PSXPKG_TYPE_PS5_RETAIL
                                            : PSXPKG_TYPE_PS5_DEBUG;
    }
    if (memcmp(h, K_MAGIC_CNT, 4) == 0) {
        /* Distinguish PS4 from PS5-META by the zero-tail heuristic */
        return psxpkg__valid_tail(fp, fsz) ? PSXPKG_TYPE_PS5_META
                                           : PSXPKG_TYPE_PS4;
    }
    return PSXPKG_TYPE_UNKNOWN;
}

/* =========================================================================
 * PS4 HEADER PARSING  (binary, big-endian, 0xC80 bytes total)
 *
 * Offsets (all big-endian unless noted):
 *  0x00  magic         u32be
 *  0x04  pkg_type      u32be
 *  0x08  unknown       u32be
 *  0x0C  file_count    u32be
 *  0x10  entry_count   u32be
 *  0x14  sc_count      u16be
 *  0x16  entry_count2  u16be
 *  0x18  entry_offset  u32be  <- table of PKGEntry
 *  0x1C  sc_data_size  u32be
 *  0x20  body_offset   u64be
 *  0x28  body_size     u64be
 *  0x30  content_off   u64be
 *  0x38  content_size  u64be
 *  0x40  content_id    36 bytes ASCII
 *  0x64  padding       12 bytes
 *  0x70  drm_type      u32be
 *  0x74  content_type  u32be
 *  0x78  content_flags u32be
 * ========================================================================= */

/* Minimum header size we need to read */
#define PS4_HDR_NEED  0x7CU

static psxpkg_error_t psxpkg__ps4_load_header(psxpkg_t* ctx)
{
    assert(ctx);
    if (ctx->file_size < (int64_t)PS4_HDR_NEED) return PSXPKG_ERR_CORRUPT;

    uint8_t h[PS4_HDR_NEED];
    if (!psxpkg__read_at(ctx->fp, 0, h, PS4_HDR_NEED)) return PSXPKG_ERR_IO;

    uint32_t entry_offset = psxpkg__be32(h + 0x18);
    uint32_t entry_count  = psxpkg__be32(h + 0x10);

    ctx->ps4_raw_drm_type      = psxpkg__be32(h + 0x70);
    ctx->ps4_raw_content_type  = psxpkg__be32(h + 0x74);
    ctx->ps4_raw_content_flags = psxpkg__be32(h + 0x78);

    /* Validate: entry table must be within the file */
    if (entry_count == 0 || entry_count > 4096U) return PSXPKG_ERR_CORRUPT;
    uint64_t table_end = (uint64_t)entry_offset + (uint64_t)entry_count * 32U;
    if ((int64_t)table_end > ctx->file_size) return PSXPKG_ERR_CORRUPT;

    /* Allocate entry cache */
    ctx->ps4_entries = (psxpkg__ps4_entry_t*)psxpkg__alloc(
        &ctx->mem, entry_count * sizeof(psxpkg__ps4_entry_t));
    if (!ctx->ps4_entries) return PSXPKG_ERR_NO_MEMORY;
    ctx->ps4_entry_count = entry_count;

    /* Read all entries: each is 32 bytes, big-endian */
    /* Layout: id(4) filename_offset(4) flags1(4) flags2(4) offset(4) size(4) pad(8) */
    uint8_t  ebuf[32];
    if (psxpkg__fseek(ctx->fp, entry_offset, SEEK_SET) != 0)
        return PSXPKG_ERR_IO;

    for (uint32_t i = 0; i < entry_count; i++) {
        if (psxpkg__read_exact(ctx->fp, ebuf, 32U) != 32U) return PSXPKG_ERR_IO;
        ctx->ps4_entries[i].id     = psxpkg__be32(ebuf + 0x00);
        ctx->ps4_entries[i].offset = psxpkg__be32(ebuf + 0x10);
        ctx->ps4_entries[i].size   = psxpkg__be32(ebuf + 0x14);
    }
    return PSXPKG_OK;
}

/* Find a PS4 entry by ID; returns NULL if not found */
static const psxpkg__ps4_entry_t* psxpkg__ps4_find_entry(
    const psxpkg_t* ctx, uint32_t id)
{
    for (uint32_t i = 0; i < ctx->ps4_entry_count; i++)
        if (ctx->ps4_entries[i].id == id) return &ctx->ps4_entries[i];
    return NULL;
}

/* Read raw bytes for a PS4 entry into a heap buffer */
static psxpkg_error_t psxpkg__ps4_read_entry(
    psxpkg_t* ctx, uint32_t id,
    uint8_t** out_buf, size_t* out_size)
{
    assert(ctx && out_buf && out_size);
    *out_buf = NULL; *out_size = 0;

    const psxpkg__ps4_entry_t* e = psxpkg__ps4_find_entry(ctx, id);
    if (!e) return PSXPKG_ERR_NOT_FOUND;

    if (e->size == 0)            return PSXPKG_ERR_NOT_FOUND;
    if (e->size > K_ASSET_MAX)   return PSXPKG_ERR_TOO_LARGE;
    if ((int64_t)(e->offset + e->size) > ctx->file_size) return PSXPKG_ERR_CORRUPT;

    uint8_t* buf = (uint8_t*)psxpkg__alloc(&ctx->mem, e->size + 1U);
    if (!buf) return PSXPKG_ERR_NO_MEMORY;

    if (!psxpkg__read_at(ctx->fp, (int64_t)e->offset, buf, e->size)) {
        psxpkg__free(&ctx->mem, buf); return PSXPKG_ERR_IO;
    }
    buf[e->size] = '\0';
    *out_buf  = buf;
    *out_size = e->size;
    return PSXPKG_OK;
}

/* =========================================================================
 * PS4 SFO PARSER
 *
 * param.sfo binary layout (mixed endian!):
 *   Header (0x14 bytes):
 *     +0x00  magic            4 bytes  (0x00 0x50 0x53 0x46 = "\x00PSF")
 *     +0x04  version          u32 LE
 *     +0x08  key_table_offset u32 LE
 *     +0x0C  data_table_offset u32 LE
 *     +0x10  index_table_entries u32 LE
 *   Index table: N x PSFRawEntry (0x10 bytes each):
 *     +0x00  key_offset   u16 LE
 *     +0x02  param_fmt    u16 BE  (0x0004=binary, 0x0204=text, 0x0404=integer)
 *     +0x04  param_len    u32 LE
 *     +0x08  param_max_len u32 LE
 *     +0x0C  data_offset  u32 LE
 * ========================================================================= */

#define PSF_FMT_BINARY  0x0004U
#define PSF_FMT_TEXT    0x0204U
#define PSF_FMT_INT     0x0404U

typedef struct {
    uint16_t key_offset;
    uint16_t param_fmt;
    uint32_t param_len;
    uint32_t param_max_len;
    uint32_t data_offset;
} psxpkg__psf_entry_t;

/* Copy a NUL-terminated UTF-8 string from the SFO data section */
static void psxpkg__psf_copy_str(const uint8_t* data_base,
                                   uint32_t data_offset, uint32_t param_len,
                                   size_t   data_avail,
                                   char* dst, size_t dsz)
{
    assert(dst && dsz > 0);
    dst[0] = '\0';
    if (data_offset >= (uint32_t)data_avail) return;
    const char* src = (const char*)data_base + data_offset;
    size_t maxcopy = (size_t)param_len < dsz - 1U ? (size_t)param_len : dsz - 1U;
    size_t i = 0;
    while (i < maxcopy && src[i] != '\0') { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* =========================================================================
 * PS4 CONTENT TYPE / DRM DECODE
 * ========================================================================= */

static void psxpkg__ps4_decode_content_type(uint32_t ct, char* out, size_t sz)
{
    const char* s;
    switch (ct) {
        case 0x01U: s = "Game Disc";                     break;
        case 0x04U: s = "App";                           break;
        case 0x06U: s = "Theme";                         break;
        case 0x07U: s = "Game Data";                     break;
        case 0x09U: s = "Mini App";                      break;
        case 0x0AU: s = "Avatar Item";                   break;
        case 0x0BU: s = "Game Sharing";                  break;
        case 0x0DU: s = "License / Activation";          break;
        case 0x0EU: s = "Game Data Package";             break;
        case 0x0FU: s = "Theme Additional Content";      break;
        case 0x10U: s = "PS Store Content";              break;
        case 0x11U: s = "Music";                         break;
        case 0x12U: s = "Video";                         break;
        case 0x14U: s = "PS4 Game";                      break;
        case 0x15U: s = "PS4 Application";               break;
        case 0x16U: s = "PS4 Patch";                     break;
        case 0x17U: s = "PS4 Remaster";                  break;
        case 0x18U: s = "PS4 DLC";                       break;
        case 0x1AU: s = "PS4 Full Game";                 break;
        case 0x1BU: s = "PS4 Patch";                     break;
        case 0x1CU: s = "PS4 DLC";                       break;
        default:    s = "Unknown";                       break;
    }
    psxpkg__scopy(out, s, sz);
}

static void psxpkg__ps4_decode_drm_type(uint32_t dt, char* out, size_t sz)
{
    const char* s;
    switch (dt) {
        case 0x0U: s = "None";         break;
        case 0x1U: s = "PlayStation Network"; break;
        case 0x2U: s = "Local";        break;
        case 0x3U: s = "Free to Play"; break;
        case 0xFU: s = "PS Now";       break;
        default:   s = "Unknown";      break;
    }
    psxpkg__scopy(out, s, sz);
}

static void psxpkg__ps4_decode_flags(uint32_t cf, char* out, size_t sz)
{
    static const struct { uint32_t mask; const char* name; } FLAGS[] = {
        { 0x100000U,  "FIRST_PATCH"       },
        { 0x200000U,  "PATCHGO"           },
        { 0x400000U,  "REMASTER"          },
        { 0x800000U,  "PS_CLOUD"          },
        { 0x2000000U, "GD_AC"             },
        { 0x4000000U, "NON_GAME"          },
        { 0x8000000U, "UNKNOWN_0x8000000" },
        { 0x40000000U,"SUBSEQUENT_PATCH"  },
        { 0x41000000U,"DELTA_PATCH"       },
        { 0x60000000U,"CUMULATIVE_PATCH"  },
    };
    out[0] = '\0';
    size_t pos = 0;
    size_t nf = sizeof(FLAGS) / sizeof(FLAGS[0]);
    for (size_t i = 0; i < nf; i++) {
        if (cf & FLAGS[i].mask) {
            size_t nlen = strlen(FLAGS[i].name);
            if (pos + nlen + 3U < sz) {
                if (pos > 0) { out[pos++] = ','; out[pos++] = ' '; }
                memcpy(out + pos, FLAGS[i].name, nlen);
                pos += nlen;
            }
        }
    }
    out[pos] = '\0';
}

static void psxpkg__ps4_infer_region(const char* tid, char* out, size_t sz)
{
    out[0] = '\0';
    if      (strncmp(tid, "CUSA", 4) == 0) psxpkg__scopy(out, "North America / Europe", sz);
    else if (strncmp(tid, "PCAS", 4) == 0) psxpkg__scopy(out, "Asia", sz);
    else if (strncmp(tid, "PCJS", 4) == 0) psxpkg__scopy(out, "Japan", sz);
    else if (strncmp(tid, "PCSF", 4) == 0) psxpkg__scopy(out, "Europe", sz);
    else if (strncmp(tid, "PCSB", 4) == 0) psxpkg__scopy(out, "Europe", sz);
    else if (strncmp(tid, "PCSC", 4) == 0) psxpkg__scopy(out, "Asia", sz);
    else if (strncmp(tid, "PCSD", 4) == 0) psxpkg__scopy(out, "Asia", sz);
}

/* =========================================================================
 * PS5 MINIMAL JSON PARSER
 * ========================================================================= */

static void psxpkg__json_skipws(const char* j, size_t l, size_t* p)
{ while (*p < l && (uint8_t)j[*p] <= 32U) (*p)++; }

static size_t psxpkg__json_find_key(const char* j, size_t l, size_t begin,
                                      const char* key, size_t klen)
{
    if (!klen || klen + 2U > K_CHUNK) return l;
    char pat[K_CHUNK];
    pat[0] = '"'; memcpy(pat + 1, key, klen); pat[klen + 1] = '"';
    size_t plen = klen + 2U;
    size_t s    = begin;
    while (s + plen <= l) {
        size_t idx = psxpkg__mem_find((const uint8_t*)j + s, l - s,
                                       (const uint8_t*)pat, plen);
        if (idx == (size_t)-1) return l;
        size_t af = s + idx;
        bool ok = (af == 0);
        if (!ok) { char c = j[af-1]; ok = (c==','||c=='{'||c=='\n'||c=='\r'||c=='\t'||c==' '); }
        if (ok) return af + plen;
        s = af + 1;
    }
    return l;
}

static bool psxpkg__json_read_string(const char* j, size_t l, size_t* p,
                                       char* out, size_t osz)
{
    out[0] = '\0';
    if (*p >= l || j[*p] != '"') return false;
    (*p)++;
    size_t op = 0; bool ok = false;
    while (*p < l) {
        char c = j[(*p)++];
        if (c == '"') { ok = true; break; }
        if (c == '\\') {
            if (*p >= l) break;
            char e = j[(*p)++];
            char d = '\0'; bool uni = false;
            switch (e) {
                case '"':  d='"';  break; case '\\': d='\\'; break;
                case '/':  d='/';  break; case 'n':  d='\n'; break;
                case 'r':  d='\r'; break; case 't':  d='\t'; break;
                case 'b':  d='\b'; break; case 'f':  d='\f'; break;
                case 'u':
                    uni = true;
                    if (op + 6 < osz) { out[op++]='\\'; out[op++]='u'; }
                    for (int k=0; k<4 && *p<l; k++) {
                        if (op+1<osz) out[op++]=j[(*p)++]; else (*p)++;
                    }
                    break;
                default: d = e; break;
            }
            if (!uni && op+1<osz) out[op++]=d;
        } else {
            if (op+1<osz) out[op++]=c;
        }
    }
    if (op < osz) out[op] = '\0'; else out[osz-1] = '\0';
    return ok;
}

static bool psxpkg__json_str(const char* j, size_t l, const char* key,
                               char* out, size_t osz)
{
    out[0] = '\0';
    size_t klen = strlen(key);
    if (!klen) return false;
    size_t pos = psxpkg__json_find_key(j, l, 0, key, klen);
    if (pos >= l) return false;
    psxpkg__json_skipws(j, l, &pos);
    if (pos >= l || j[pos] != ':') return false;
    pos++;
    psxpkg__json_skipws(j, l, &pos);
    if (pos >= l || j[pos] != '"') return false;
    return psxpkg__json_read_string(j, l, &pos, out, osz);
}

static bool psxpkg__json_int(const char* j, size_t l, const char* key, int64_t* out)
{
    *out = 0;
    size_t klen = strlen(key);
    if (!klen) return false;
    size_t pos = psxpkg__json_find_key(j, l, 0, key, klen);
    if (pos >= l) return false;
    psxpkg__json_skipws(j, l, &pos);
    if (pos >= l || j[pos] != ':') return false;
    pos++;
    psxpkg__json_skipws(j, l, &pos);
    if (pos >= l) return false;
    bool neg = (j[pos] == '-'); if (neg) pos++;
    if (pos >= l || !isdigit((unsigned char)j[pos])) return false;
    int64_t v = 0;
    while (pos < l && isdigit((unsigned char)j[pos])) { v = v*10 + (j[pos]-'0'); pos++; }
    *out = neg ? -v : v;
    return true;
}

static bool psxpkg__json_obj(const char* j, size_t l, const char* key,
                               size_t* ob, size_t* ol)
{
    size_t klen = strlen(key);
    if (!klen) return false;
    size_t pos = psxpkg__json_find_key(j, l, 0, key, klen);
    if (pos >= l) return false;
    psxpkg__json_skipws(j, l, &pos);
    if (pos >= l || j[pos] != ':') return false;
    pos++;
    psxpkg__json_skipws(j, l, &pos);
    if (pos >= l || j[pos] != '{') return false;
    size_t start = pos + 1; int depth = 1; pos++;
    while (pos < l && depth > 0) {
        char c = j[pos++];
        if (c == '{') depth++;
        else if (c == '}') depth--;
        else if (c == '"') { while (pos < l) { char q=j[pos++]; if(q=='"') break; if(q=='\\') pos++; } }
    }
    if (depth != 0) return false;
    *ob = start; *ol = (pos-1) > start ? (pos-1-start) : 0;
    return true;
}

/* =========================================================================
 * PS5 PARAM.JSON EXTRACTION — RETAIL PATH
 * ========================================================================= */

static psxpkg_error_t psxpkg__ps5_retail_param(
    psxpkg_t* ctx, char** out_json, size_t* out_len)
{
    *out_json = NULL; *out_len = 0;
    if (!psxpkg__valid_tail(ctx->fp, ctx->file_size)) return PSXPKG_ERR_NOT_A_PKG;

    int64_t scan_begin = (ctx->file_size > K_PARAM_RANGE)
                       ? ctx->file_size - K_PARAM_RANGE : 0;

    static const uint8_t M_START[5] = {'.',  'j',  's',  'o',  'n'};
    static const uint8_t M_END  [11]= {'v',  'e',  'r',  's',  'i',
                                        'o',  'n',  '.',  'x',  'm',  'l'};
    int64_t so = -1, eo = -1;
    psxpkg_error_t e;
    e = psxpkg__rfind(ctx->fp, scan_begin, ctx->file_size, M_START, 5, &ctx->mem, &so);
    if (e != PSXPKG_OK) return e;
    e = psxpkg__rfind(ctx->fp, scan_begin, ctx->file_size, M_END, 11, &ctx->mem, &eo);
    if (e != PSXPKG_OK) return e;
    if (so < 0 || eo < 0 || eo <= so) return PSXPKG_ERR_NOT_FOUND;

    int64_t rlen = eo - so;
    if (rlen < 10L || rlen > (int64_t)K_ASSET_MAX) return PSXPKG_ERR_TOO_LARGE;

    uint8_t* raw = (uint8_t*)psxpkg__alloc(&ctx->mem, (size_t)rlen + 1U);
    if (!raw) return PSXPKG_ERR_NO_MEMORY;
    if (!psxpkg__read_at(ctx->fp, so, raw, (size_t)rlen)) {
        psxpkg__free(&ctx->mem, raw); return PSXPKG_ERR_IO;
    }
    raw[rlen] = '\0';

    /* Find first '"' to skip leading binary prefix */
    size_t fq = (size_t)-1;
    for (size_t i = 0; i < (size_t)rlen; i++) { if (raw[i] == '"') { fq = i; break; } }
    if (fq == (size_t)-1) { psxpkg__free(&ctx->mem, raw); return PSXPKG_ERR_CORRUPT; }

    const uint8_t* body = raw + fq;
    size_t         blen = (size_t)rlen - fq;

    /* Find last '}' */
    size_t lb = (size_t)-1;
    for (size_t i = blen; i > 0; i--) { if (body[i-1] == '}') { lb = i-1; break; } }

    size_t  jlen;
    char*   json;
    if (lb != (size_t)-1) {
        jlen = lb + 2U;
        json = (char*)psxpkg__alloc(&ctx->mem, jlen + 1U);
        if (!json) { psxpkg__free(&ctx->mem, raw); return PSXPKG_ERR_NO_MEMORY; }
        json[0] = '{';
        memcpy(json + 1, body, lb + 1U);
    } else {
        jlen = blen + 2U;
        json = (char*)psxpkg__alloc(&ctx->mem, jlen + 1U);
        if (!json) { psxpkg__free(&ctx->mem, raw); return PSXPKG_ERR_NO_MEMORY; }
        json[0] = '{';
        memcpy(json + 1, body, blen);
        json[blen + 1] = '}';
    }
    json[jlen] = '\0';
    psxpkg__free(&ctx->mem, raw);
    *out_json = json; *out_len = jlen;
    return PSXPKG_OK;
}

/* =========================================================================
 * PS5 PARAM.JSON PARSING — COMMON
 * ========================================================================= */

static void psxpkg__ps5_infer_region(const char* tid, char* out, size_t sz)
{
    out[0] = '\0';
    if      (strncmp(tid, "PPSA", 4) == 0) psxpkg__scopy(out, "NA / Europe", sz);
    else if (strncmp(tid, "ECAS", 4) == 0) psxpkg__scopy(out, "Asia", sz);
    else if (strncmp(tid, "ELAS", 4) == 0) psxpkg__scopy(out, "Asia", sz);
    else if (strncmp(tid, "ELJM", 4) == 0) psxpkg__scopy(out, "Japan", sz);
}

static void psxpkg__ps5_parse_json(const char* j, size_t jl, psxpkg_ps5_param_t* p)
{
    psxpkg__json_str(j, jl, "titleId",          p->title_id,          PSXPKG_STR_MAX);
    psxpkg__json_str(j, jl, "contentId",         p->content_id,        PSXPKG_STR_MAX);
    psxpkg__json_str(j, jl, "contentVersion",    p->content_version,   PSXPKG_STR_MAX);
    psxpkg__json_str(j, jl, "masterVersion",     p->master_version,    PSXPKG_STR_MAX);
    psxpkg__json_str(j, jl, "sdkVersion",        p->sdk_version,       PSXPKG_STR_MAX);
    psxpkg__json_str(j, jl, "requiredSystemSoftwareVersion",
                             p->required_firmware, PSXPKG_STR_MAX);
    psxpkg__json_str(j, jl, "applicationDrmType",p->drm_type,          PSXPKG_STR_MAX);
    psxpkg__json_str(j, jl, "versionFileUri",    p->version_file_uri,  PSXPKG_STR_MAX);

    int64_t iv = 0;
    if (psxpkg__json_int(j, jl, "applicationCategoryType", &iv)) p->category_type = (int32_t)iv;
    if (psxpkg__json_int(j, jl, "downloadDataSize",        &iv)) p->download_data_size = iv;

    size_t pb = 0, pl = 0;
    if (psxpkg__json_obj(j, jl, "pubtools", &pb, &pl)) {
        psxpkg__json_str(j+pb, pl, "toolVersion",  p->pubtool_version, PSXPKG_STR_MAX);
        psxpkg__json_str(j+pb, pl, "creationDate", p->creation_date,   PSXPKG_STR_MAX);
    }

    size_t lb = 0, ll = 0;
    if (psxpkg__json_obj(j, jl, "localizedParameters", &lb, &ll)) {
        const char* lj = j + lb;
        psxpkg_ps5_locales_t* t = &p->titles;

        /* Try flat string values first */
        psxpkg__json_str(lj,ll,"en-US",  t->en_us,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"en-GB",  t->en_gb,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"de-DE",  t->de_de,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"fr-FR",  t->fr_fr,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"it-IT",  t->it_it,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"es-ES",  t->es_es,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"es-419", t->es_419,  PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"ja-JP",  t->ja_jp,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"ko-KR",  t->ko_kr,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"pt-BR",  t->pt_br,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"pt-PT",  t->pt_pt,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"zh-Hans",t->zh_hans, PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"zh-Hant",t->zh_hant, PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"ru-RU",  t->ru_ru,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"pl-PL",  t->pl_pl,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"tr-TR",  t->tr_tr,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"nl-NL",  t->nl_nl,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"sv-SE",  t->sv_se,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"fi-FI",  t->fi_fi,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"da-DK",  t->da_dk,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"ar-AE",  t->ar_ae,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"th-TH",  t->th_th,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"cs-CZ",  t->cs_cz,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"hu-HU",  t->hu_hu,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"ro-RO",  t->ro_ro,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"el-GR",  t->el_gr,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"id-ID",  t->id_id,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"vi-VN",  t->vi_vn,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"no-NO",  t->no_no,   PSXPKG_STR_MAX);
        psxpkg__json_str(lj,ll,"fr-CA",  t->fr_ca,   PSXPKG_STR_MAX);

        /* Also try nested {"titleName":"..."} objects */
        const char* locales[] = {
            "en-US","en-GB","de-DE","fr-FR","it-IT","es-ES","es-419",
            "ja-JP","ko-KR","pt-BR","pt-PT","zh-Hans","zh-Hant","ru-RU",
            "pl-PL","tr-TR","nl-NL","sv-SE","fi-FI","da-DK","ar-AE",
            "th-TH","cs-CZ","hu-HU","ro-RO","el-GR","id-ID","vi-VN",
            "no-NO","fr-CA"
        };
        char* fields[] = {
            t->en_us, t->en_gb, t->de_de, t->fr_fr, t->it_it, t->es_es, t->es_419,
            t->ja_jp, t->ko_kr, t->pt_br, t->pt_pt, t->zh_hans, t->zh_hant, t->ru_ru,
            t->pl_pl, t->tr_tr, t->nl_nl, t->sv_se, t->fi_fi, t->da_dk, t->ar_ae,
            t->th_th, t->cs_cz, t->hu_hu, t->ro_ro, t->el_gr, t->id_id, t->vi_vn,
            t->no_no, t->fr_ca
        };
        size_t nloc = sizeof(locales)/sizeof(locales[0]);
        for (size_t li = 0; li < nloc; li++) {
            if (fields[li][0]) continue;
            size_t ob2 = 0, ol2 = 0;
            if (psxpkg__json_obj(lj, ll, locales[li], &ob2, &ol2))
                psxpkg__json_str(lj+ob2, ol2, "titleName", fields[li], PSXPKG_STR_MAX);
        }
    }
    psxpkg__ps5_infer_region(p->title_id, p->region, PSXPKG_STR_MAX);
}

/* =========================================================================
 * PNG UTILITIES
 * ========================================================================= */

static bool psxpkg__png_ihdr(const uint8_t* d, size_t l, uint32_t* w, uint32_t* h)
{
    if (l < 33U || memcmp(d, K_PNG_SIG, 8) != 0) return false;
    *w = psxpkg__be32(d + 16);
    *h = psxpkg__be32(d + 20);
    return true;
}

static psxpkg_image_kind_t psxpkg__png_kind(uint32_t w, uint32_t h)
{
    /* PS4 icons: 512x512 or 320x176; PS5 icons: 512x512 */
    if (w == 512U  && (h == 512U || h == 176U)) return PSXPKG_IMAGE_ICON;
    /* PS4 background: 1920x1080; PS5 background: 3840x2160 */
    if ((w == 1920U && h == 1080U) || (w == 3840U && h == 2160U))
        return PSXPKG_IMAGE_BACKGROUND;
    if (w == 320U  && h == 176U)  return PSXPKG_IMAGE_ICON;
    return PSXPKG_IMAGE_OTHER;
}

static size_t psxpkg__png_min_bytes(psxpkg_image_kind_t k)
{
    if (k == PSXPKG_IMAGE_ICON)       return K_ICON_MIN;
    if (k == PSXPKG_IMAGE_BACKGROUND) return K_BG_MIN;
    return 64U;
}

/* Extract PNG bytes [sig_off..IEND+12) from the file into a heap buffer */
static psxpkg_error_t psxpkg__extract_png(psxpkg_t* ctx, int64_t sig_off,
                                            uint8_t** out, size_t* outsz)
{
    *out = NULL; *outsz = 0;
    int64_t iend = -1;
    psxpkg_error_t e = psxpkg__ffind(ctx->fp, sig_off, ctx->file_size,
                                      K_PNG_IEND, 12, &ctx->mem, &iend);
    if (e != PSXPKG_OK) return e;
    if (iend < 0) return PSXPKG_ERR_NOT_FOUND;
    int64_t plen = iend + 12 - sig_off;
    if (plen <= 0 || plen > (int64_t)PSXPKG_MAX_ASSET_BYTES) return PSXPKG_ERR_TOO_LARGE;
    uint8_t* buf = (uint8_t*)psxpkg__alloc(&ctx->mem, (size_t)plen);
    if (!buf) return PSXPKG_ERR_NO_MEMORY;
    if (!psxpkg__read_at(ctx->fp, sig_off, buf, (size_t)plen)) {
        psxpkg__free(&ctx->mem, buf); return PSXPKG_ERR_IO;
    }
    *out = buf; *outsz = (size_t)plen;
    return PSXPKG_OK;
}

static int psxpkg__cmp_i64(const void* a, const void* b)
{
    int64_t x = *(const int64_t*)a, y = *(const int64_t*)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

/* Deliver a PNG buffer via callback; frees buffer after call */
static psxpkg_error_t psxpkg__deliver_png(psxpkg_t* ctx,
                                            uint8_t* buf, size_t sz,
                                            uint32_t* idx,
                                            psxpkg_image_callback_t cb,
                                            void* ud)
{
    uint32_t w = 0, h = 0;
    if (!psxpkg__png_ihdr(buf, sz, &w, &h)) { psxpkg__free(&ctx->mem, buf); return PSXPKG_OK; }
    psxpkg_image_kind_t kind = psxpkg__png_kind(w, h);
    if (sz < psxpkg__png_min_bytes(kind)) { psxpkg__free(&ctx->mem, buf); return PSXPKG_OK; }

    psxpkg_image_t img;
    img.kind = kind; img.width = w; img.height = h;
    img.data = buf;  img.size = sz; img.index  = (*idx)++;

    bool cont = cb(&img, ud);
    psxpkg__free(&ctx->mem, buf);
    return cont ? PSXPKG_OK : PSXPKG_ERR_ABORTED;
}

/* =========================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * ========================================================================= */

const char* psxpkg_version(void) { return PSXPKG_VERSION_STRING; }

const char* psxpkg_error_string(psxpkg_error_t err)
{
    switch (err) {
        case PSXPKG_OK:                 return "Success";
        case PSXPKG_ERR_INVALID_PARAM:  return "Invalid parameter (NULL or out of range)";
        case PSXPKG_ERR_IO:             return "File I/O error";
        case PSXPKG_ERR_NOT_A_PKG:      return "Not a PS4/PS5 PKG (unrecognised header)";
        case PSXPKG_ERR_NOT_FOUND:      return "Asset not found in this PKG";
        case PSXPKG_ERR_CORRUPT:        return "Data integrity check failed";
        case PSXPKG_ERR_OVERFLOW:       return "Arithmetic overflow";
        case PSXPKG_ERR_NO_MEMORY:      return "Memory allocation failed";
        case PSXPKG_ERR_UNSUPPORTED:    return "Operation not supported for this PKG type";
        case PSXPKG_ERR_TOO_LARGE:      return "Asset exceeds maximum allowed size";
        case PSXPKG_ERR_ABORTED:        return "Extraction aborted by callback";
        case PSXPKG_ERR_WRONG_PLATFORM: return "Function called on wrong platform (PS4/PS5 mismatch)";
        default:                         return "Unknown error";
    }
}

/* ------------------------------------------------------------------------ */
psxpkg_error_t psxpkg_open(const char* path,
                             const psxpkg_allocator_t* allocator,
                             psxpkg_t** out_pkg)
{
    if (!path || !out_pkg) return PSXPKG_ERR_INVALID_PARAM;
    *out_pkg = NULL;

    psxpkg__mem_t mem = psxpkg__mem_resolve(allocator);

    FILE* fp = fopen(path, "rb");
    if (!fp) return PSXPKG_ERR_IO;

    int64_t fsz = psxpkg__file_size(fp);
    if (fsz <= 0) { fclose(fp); return PSXPKG_ERR_IO; }

    psxpkg_type_t t = psxpkg__detect_type(fp, fsz);
    if (t == PSXPKG_TYPE_UNKNOWN) { fclose(fp); return PSXPKG_ERR_NOT_A_PKG; }

    psxpkg_t* ctx = (psxpkg_t*)psxpkg__alloc(&mem, sizeof(psxpkg_t));
    if (!ctx) { fclose(fp); return PSXPKG_ERR_NO_MEMORY; }

    memset(ctx, 0, sizeof(*ctx));
    ctx->fp        = fp;
    ctx->mem       = mem;
    ctx->type      = t;
    ctx->file_size = fsz;

    /* For PS4: eagerly parse the binary header and entry table */
    if (t == PSXPKG_TYPE_PS4) {
        psxpkg_error_t e = psxpkg__ps4_load_header(ctx);
        if (e != PSXPKG_OK) {
            fclose(fp); psxpkg__free(&mem, ctx); return e;
        }
    }

    *out_pkg = ctx;
    return PSXPKG_OK;
}

/* ------------------------------------------------------------------------ */
void psxpkg_close(psxpkg_t* pkg)
{
    if (!pkg) return;
    if (pkg->ps4_entries) psxpkg__free(&pkg->mem, pkg->ps4_entries);
    if (pkg->fp) fclose(pkg->fp);
    psxpkg__free(&pkg->mem, pkg);
}

/* ------------------------------------------------------------------------ */
psxpkg_error_t psxpkg_get_type(const psxpkg_t* pkg, psxpkg_type_t* out_type)
{
    if (!pkg) return PSXPKG_ERR_INVALID_PARAM;
    if (out_type) *out_type = pkg->type;
    return PSXPKG_OK;
}

/* ------------------------------------------------------------------------ */
psxpkg_error_t psxpkg_read_ps4_param(psxpkg_t* pkg, psxpkg_ps4_param_t* out)
{
    if (!pkg || !out) return PSXPKG_ERR_INVALID_PARAM;
    memset(out, 0, sizeof(*out));
    if (pkg->type != PSXPKG_TYPE_PS4) return PSXPKG_ERR_WRONG_PLATFORM;

    /* Read raw param.sfo bytes */
    uint8_t* sfo = NULL; size_t sfosz = 0;
    psxpkg_error_t e = psxpkg__ps4_read_entry(pkg, PS4_ENTRY_PARAM_SFO, &sfo, &sfosz);
    if (e != PSXPKG_OK) return e;
    if (sfosz < 0x14U) { psxpkg__free(&pkg->mem, sfo); return PSXPKG_ERR_CORRUPT; }

    /* Validate PSF magic */
    if (memcmp(sfo, K_PSF_MAGIC, 4) != 0) {
        psxpkg__free(&pkg->mem, sfo); return PSXPKG_ERR_CORRUPT;
    }

    /* Parse PSF header */
    uint32_t key_off  = psxpkg__le32(sfo + 0x08);
    uint32_t data_off = psxpkg__le32(sfo + 0x0C);
    uint32_t n_entries= psxpkg__le32(sfo + 0x10);

    if (n_entries > 256U) { psxpkg__free(&pkg->mem, sfo); return PSXPKG_ERR_CORRUPT; }

    /* Validate offsets */
    uint64_t idx_end = 0x14ULL + (uint64_t)n_entries * 0x10ULL;
    if (key_off < idx_end || data_off < key_off || (uint64_t)data_off > sfosz) {
        psxpkg__free(&pkg->mem, sfo); return PSXPKG_ERR_CORRUPT;
    }

    const uint8_t* key_base  = sfo + key_off;
    const uint8_t* data_base = sfo + data_off;
    size_t  key_avail  = (size_t)(data_off - key_off);
    size_t  data_avail = sfosz - (size_t)data_off;

    /* Process each entry */
    for (uint32_t i = 0; i < n_entries; i++) {
        const uint8_t* ep = sfo + 0x14U + i * 0x10U;
        psxpkg__psf_entry_t ent;
        ent.key_offset  = psxpkg__le16(ep + 0x00);
        ent.param_fmt   = psxpkg__be16(ep + 0x02);  /* BE! */
        ent.param_len   = psxpkg__le32(ep + 0x04);
        ent.param_max_len = psxpkg__le32(ep + 0x08);
        ent.data_offset = psxpkg__le32(ep + 0x0C);

        if (ent.key_offset >= (uint32_t)key_avail) continue;

        const char* key = (const char*)key_base + ent.key_offset;
        /* Ensure key is NUL-terminated within bounds */
        bool key_ok = false;
        for (size_t ki = ent.key_offset; ki < key_avail; ki++) {
            if (key_base[ki] == 0) { key_ok = true; break; }
        }
        if (!key_ok) continue;

        if (ent.data_offset + ent.param_len > (uint32_t)data_avail) continue;

        if (ent.param_fmt == PSF_FMT_TEXT) {
            /* String field: match known keys and copy */
#define SFOMATCH(k, dst) \
    if (strcmp(key, (k)) == 0) { \
        psxpkg__psf_copy_str(data_base, ent.data_offset, ent.param_len, \
                              data_avail, (dst), PSXPKG_STR_MAX); continue; }

            SFOMATCH("TITLE_ID",    out->title_id)
            SFOMATCH("CONTENT_ID",  out->content_id)
            SFOMATCH("TITLE",       out->title)
            SFOMATCH("TITLE_00",    out->title_00)
            SFOMATCH("TITLE_01",    out->title_01)
            SFOMATCH("TITLE_02",    out->title_02)
            SFOMATCH("TITLE_03",    out->title_03)
            SFOMATCH("TITLE_04",    out->title_04)
            SFOMATCH("TITLE_05",    out->title_05)
            SFOMATCH("TITLE_06",    out->title_06)
            SFOMATCH("TITLE_07",    out->title_07)
            SFOMATCH("APP_VER",     out->app_ver)
            SFOMATCH("SYSTEM_VER",  out->system_ver)
            SFOMATCH("VERSION",     out->version)
            SFOMATCH("CATEGORY",    out->category)
#undef SFOMATCH
        } else if (ent.param_fmt == PSF_FMT_INT && ent.param_len >= 4U) {
            int32_t iv = (int32_t)psxpkg__le32(data_base + ent.data_offset);
            if      (strcmp(key, "PARENTAL_LEVEL") == 0) out->parental_level = iv;
            else if (strcmp(key, "ATTRIBUTE")      == 0) out->attribute       = iv;
            else if (strcmp(key, "RESOLUTION")     == 0) out->resolution      = iv;
            else if (strcmp(key, "SOUND_FORMAT")   == 0) out->sound_format    = iv;
        }
    }

    psxpkg__free(&pkg->mem, sfo);

    /* Decode raw header fields */
    out->raw_content_type  = pkg->ps4_raw_content_type;
    out->raw_drm_type      = pkg->ps4_raw_drm_type;
    out->raw_content_flags = pkg->ps4_raw_content_flags;

    psxpkg__ps4_decode_content_type(out->raw_content_type,
                                     out->pkg_content_type, PSXPKG_STR_MAX);
    psxpkg__ps4_decode_drm_type    (out->raw_drm_type,
                                     out->pkg_drm_type,     PSXPKG_STR_MAX);
    psxpkg__ps4_decode_flags       (out->raw_content_flags,
                                     out->pkg_content_flags, PSXPKG_STR_MAX);
    psxpkg__ps4_infer_region       (out->title_id,
                                     out->region,            PSXPKG_STR_MAX);

    /* If TITLE is empty, try TITLE_00 as fallback */
    if (!out->title[0] && out->title_00[0])
        psxpkg__scopy(out->title, out->title_00, PSXPKG_STR_MAX);

    return PSXPKG_OK;
}

/* ------------------------------------------------------------------------ */
psxpkg_error_t psxpkg_read_ps5_param(psxpkg_t* pkg, psxpkg_ps5_param_t* out)
{
    if (!pkg || !out) return PSXPKG_ERR_INVALID_PARAM;
    memset(out, 0, sizeof(*out));

    if (pkg->type == PSXPKG_TYPE_PS4) return PSXPKG_ERR_WRONG_PLATFORM;
    if (pkg->type == PSXPKG_TYPE_PS5_META) return PSXPKG_ERR_UNSUPPORTED;

    /* Retail and Debug share the same JSON extraction path */
    char* json = NULL; size_t jlen = 0;
    psxpkg_error_t e = psxpkg__ps5_retail_param(pkg, &json, &jlen);
    if (e != PSXPKG_OK) return e;
    if (!json || jlen < 3U || json[0] != '{') {
        psxpkg__free(&pkg->mem, json); return PSXPKG_ERR_CORRUPT;
    }
    psxpkg__ps5_parse_json(json, jlen, out);
    psxpkg__free(&pkg->mem, json);
    return PSXPKG_OK;
}

/* ------------------------------------------------------------------------ */
psxpkg_error_t psxpkg_read_images(psxpkg_t*               pkg,
                                    psxpkg_image_callback_t  cb,
                                    void*                    ud)
{
    if (!pkg || !cb) return PSXPKG_ERR_INVALID_PARAM;

    uint32_t delivered = 0;
    psxpkg_error_t err = PSXPKG_OK;

    if (pkg->type == PSXPKG_TYPE_PS4)
    {
        /* ---- PS4: read images directly from entry table ---- */
        /* Ordered list of image entry IDs to try */
        static const uint32_t IMG_IDS[] = {
            PS4_ENTRY_ICON0_PNG,   /* 0x1200 - main icon         */
            PS4_ENTRY_PIC1_PNG,    /* 0x1006 - background pic1   */
            PS4_ENTRY_PIC0_PNG,    /* 0x1220 - pic0              */
            /* icon0_00..icon0_30 (0x1201..0x121F) */
            0x1201U,0x1202U,0x1203U,0x1204U,0x1205U,
            0x1206U,0x1207U,0x1208U,0x1209U,0x120AU,
            0x120BU,0x120CU,0x120DU,0x120EU,0x120FU,
            0x1210U,0x1211U,0x1212U,0x1213U,0x1214U,
            0x1215U,0x1216U,0x1217U,0x1218U,0x1219U,
            0x121AU,0x121BU,0x121CU,0x121DU,0x121EU,0x121FU,
            /* pic1_00..pic1_30 (0x1241..0x125F) */
            0x1241U,0x1242U,0x1243U,0x1244U,0x1245U,
            0x1246U,0x1247U,0x1248U,0x1249U,0x124AU,
            0x124BU,0x124CU,0x124DU,0x124EU,0x124FU,
            0x1250U,0x1251U,0x1252U,0x1253U,0x1254U,
            0x1255U,0x1256U,0x1257U,0x1258U,0x1259U,
            0x125AU,0x125BU,0x125CU,0x125DU,0x125EU,0x125FU,
        };
        size_t nids = sizeof(IMG_IDS) / sizeof(IMG_IDS[0]);

        for (size_t i = 0; i < nids; i++) {
            uint8_t* buf = NULL; size_t bsz = 0;
            psxpkg_error_t re = psxpkg__ps4_read_entry(pkg, IMG_IDS[i], &buf, &bsz);
            if (re != PSXPKG_OK) continue;

            /* Validate PNG signature */
            if (bsz < 8 || memcmp(buf, K_PNG_SIG, 8) != 0) {
                psxpkg__free(&pkg->mem, buf); continue;
            }

            err = psxpkg__deliver_png(pkg, buf, bsz, &delivered, cb, ud);
            if (err == PSXPKG_ERR_ABORTED) return err;
        }
    }
    else
    {
        /* ---- PS5 (META/RETAIL/DEBUG): scan for PNG signatures ---- */
        if (!psxpkg__valid_tail(pkg->fp, pkg->file_size))
            return PSXPKG_ERR_NOT_A_PKG;

        int64_t scan_begin = (pkg->file_size > K_PNG_RANGE)
                           ? pkg->file_size - K_PNG_RANGE : 0;

        int64_t positions[K_MAX_PNGS];
        size_t  found = 0;

        err = psxpkg__rfind_all(pkg->fp, scan_begin, pkg->file_size,
                                 K_PNG_SIG, 8, &pkg->mem,
                                 positions, K_MAX_PNGS, &found);
        if (err != PSXPKG_OK) return err;
        if (!found) return PSXPKG_ERR_NOT_FOUND;

        qsort(positions, found, sizeof(int64_t), psxpkg__cmp_i64);

        for (size_t i = 0; i < found; i++) {
            uint8_t* buf = NULL; size_t bsz = 0;
            if (psxpkg__extract_png(pkg, positions[i], &buf, &bsz) != PSXPKG_OK) continue;
            err = psxpkg__deliver_png(pkg, buf, bsz, &delivered, cb, ud);
            if (err == PSXPKG_ERR_ABORTED) return err;
        }
    }

    return (delivered > 0) ? PSXPKG_OK : PSXPKG_ERR_NOT_FOUND;
}

#endif /* PSXPKG_IMPLEMENTATION */

/*
 * ============================================================================
 * This standalone reference reader is authored by seregonwar and is distributed
 * with the LibProsperoPkg C++ rewrite/fork under the GNU General Public License
 * v3.0 or later. It remains optional reference code until there is a concrete
 * need to integrate it into the native build.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. See the repository
 * LICENSE file for the complete GPL-3.0-or-later terms.
 * ============================================================================
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#endif /* PSXPKG_H */
