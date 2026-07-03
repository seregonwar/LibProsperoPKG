// C ABI surface for the shared-library builds produced by the native workflows.
//
// This file is NOT part of the normal managed build. It lives outside the project
// directory so the SDK never compiles it into the class library or its package. The
// native workflows copy it into the project directory and enable AOT/shared-library
// output before publishing, so the exported entry points only exist in the .so/.dylib
// artifacts.
//
// Every export uses UnmanagedCallersOnly so the AOT compiler emits a plain C symbol.
// Strings cross the boundary as UTF-8 (NUL-terminated `const char*`); output strings are
// copied into caller-provided buffers, so the caller owns all memory. Enum arguments are
// passed as 32-bit integers whose values are documented in libprosperopkg.h.

using System;
using System.Runtime.InteropServices;
using System.Text;
using LibProsperoPkg;
using LibProsperoPkg.Content;
using LibProsperoPkg.PFS.Compression;
using LibProsperoPkg.PKG;

namespace LibProsperoPkg.Native;

internal static unsafe class NativeExports
{
    [ThreadStatic]
    private static string? _lastError;

    private static readonly byte* VersionPtr = AllocUtf8("LibProsperoPkg 1.3.0");

    /// <summary>Returns a pointer to a static, NUL-terminated version string.</summary>
    [UnmanagedCallersOnly(EntryPoint = "lpp_version")]
    public static byte* Version() => VersionPtr;

    /// <summary>Copies the most recent error message for the current thread into a caller buffer.</summary>
    /// <returns>The number of UTF-8 bytes written (excluding the terminator), or a negative value on error.</returns>
    [UnmanagedCallersOnly(EntryPoint = "lpp_last_error")]
    public static int LastError(byte* buffer, int capacity)
        => WriteUtf8(_lastError ?? "", buffer, capacity);

    /// <summary>Returns 1 when the argument is a valid 36-character content id, otherwise 0.</summary>
    [UnmanagedCallersOnly(EntryPoint = "lpp_is_valid_content_id")]
    public static int IsValidContentId(byte* contentId)
        => ProsperoPackageBuilder.IsValidContentId(Utf8ToString(contentId)) ? 1 : 0;

    /// <summary>Returns 1 when the argument looks like a PPSAxxxxx title id, otherwise 0.</summary>
    [UnmanagedCallersOnly(EntryPoint = "lpp_is_valid_title_id")]
    public static int IsValidTitleId(byte* titleId)
        => ProsperoPackageBuilder.IsValidTitleId(Utf8ToString(titleId)) ? 1 : 0;

    /// <summary>
    /// Composes a 36-character content id from a publisher prefix, a title id and a label,
    /// writing the result into a caller buffer as UTF-8.
    /// </summary>
    /// <returns>The number of bytes written, or a negative value on error (see lpp_last_error).</returns>
    [UnmanagedCallersOnly(EntryPoint = "lpp_compose_content_id")]
    public static int ComposeContentId(byte* publisher, byte* titleId, byte* label, byte* outBuffer, int capacity)
    {
        try
        {
            string id = ProsperoPackageBuilder.ComposeContentId(
                Utf8ToString(publisher), Utf8ToString(titleId), Utf8ToString(label));
            return WriteUtf8(id, outBuffer, capacity);
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            return -1;
        }
    }

    /// <summary>
    /// Builds a package from a prepared source folder. The integer arguments select the build
    /// mode, output format and inner-image codec (values documented in libprosperopkg.h). The
    /// output path is written into <paramref name="outPath"/> as UTF-8.
    /// </summary>
    /// <returns>0 on success; a negative value on failure (call lpp_last_error for the message).</returns>
    [UnmanagedCallersOnly(EntryPoint = "lpp_build_package")]
    public static int BuildPackage(
        byte* sourceFolder,
        byte* outputFolder,
        byte* contentId,
        byte* passcode,
        byte* title,
        byte* titleId,
        byte* version,
        int mode,
        int outputFormat,
        int innerCompression,
        byte* outPath,
        int outPathCapacity)
    {
        try
        {
            var options = new ProsperoBuildOptions
            {
                SourceFolder = Utf8ToString(sourceFolder) ?? "",
                OutputFolder = Utf8ToString(outputFolder) ?? "",
                ContentId = Utf8ToString(contentId) ?? "",
                Passcode = Fallback(Utf8ToString(passcode), new string('0', 32)),
                Title = Utf8ToString(title) ?? "",
                TitleId = Utf8ToString(titleId) ?? "",
                Version = Fallback(Utf8ToString(version), "01.00"),
                Mode = ToEnum<ProsperoPackageMode>(mode),
                OutputFormat = ToEnum<ProsperoOutputFormat>(outputFormat),
                InnerCompression = ToEnum<ProsperoInnerCompression>(innerCompression),
            };

            ProsperoBuildResult result = ProsperoPackageBuilder.Build(options);
            int written = WriteUtf8(result.OutputPath, outPath, outPathCapacity);
            return written < 0 ? written : 0;
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            return -1;
        }
    }

    /// <summary>
    /// Detects the package type of the file at <paramref name="path"/>.
    /// </summary>
    /// <returns>
    /// The package type as an integer (0 = Meta, 1 = FullRetail, 2 = FullDebug), or -1 when the
    /// file is not a recognized package or cannot be read (call lpp_last_error for details).
    /// </returns>
    [UnmanagedCallersOnly(EntryPoint = "lpp_detect_package_type")]
    public static int DetectPackageType(byte* path)
    {
        try
        {
            ProsperoPkgType? type = ProsperoPkgReader.DetectType(Utf8ToString(path) ?? "");
            return type is null ? -1 : (int)type.Value;
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            return -1;
        }
    }

    /// <summary>
    /// Lays a prepared folder out into an inner-PFS image. <paramref name="form"/> selects the
    /// image form (0 = Plaintext, 1 = Encrypted, 2 = Compressed zlib, 3 = KrakenCompressed). The
    /// written image path is copied into <paramref name="outPath"/> as UTF-8.
    /// </summary>
    /// <returns>0 on success; a negative value on failure (call lpp_last_error for the message).</returns>
    [UnmanagedCallersOnly(EntryPoint = "lpp_build_inner_image")]
    public static int BuildInnerImage(
        byte* sourceFolder,
        byte* outputPath,
        byte* contentId,
        byte* passcode,
        int form,
        byte* outPath,
        int outPathCapacity)
    {
        try
        {
            string result = ProsperoPackageBuilder.BuildInnerImage(
                Utf8ToString(sourceFolder) ?? "",
                Utf8ToString(outputPath) ?? "",
                Utf8ToString(contentId) ?? "",
                Fallback(Utf8ToString(passcode), new string('0', 32)),
                ToEnum<InnerImageForm>(form));
            int written = WriteUtf8(result, outPath, outPathCapacity);
            return written < 0 ? written : 0;
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            return -1;
        }
    }

    /// <summary>
    /// AES-XTS-encrypts a prepared plaintext inner-PFS image in place, using keys derived from the
    /// content id and passcode.
    /// </summary>
    /// <returns>0 on success; a negative value on failure (call lpp_last_error for the message).</returns>
    [UnmanagedCallersOnly(EntryPoint = "lpp_encrypt_pfs_image")]
    public static int EncryptPfsImage(byte* pfsImagePath, byte* contentId, byte* passcode)
    {
        try
        {
            ProsperoPackageBuilder.EncryptPfsImage(
                Utf8ToString(pfsImagePath) ?? "",
                Utf8ToString(contentId) ?? "",
                Fallback(Utf8ToString(passcode), new string('0', 32)));
            return 0;
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            return -1;
        }
    }

    /// <summary>
    /// Packs a plaintext PFS image into a PFSv3 PFSC container using Kraken. A non-positive
    /// <paramref name="level"/> or <paramref name="blockSize"/> selects the default (7 / 262144).
    /// </summary>
    /// <returns>0 on success; a negative value on failure (call lpp_last_error for the message).</returns>
    [UnmanagedCallersOnly(EntryPoint = "lpp_pack_pfs_image")]
    public static int PackPfsImage(byte* inputImagePath, byte* outputPath, int level, int blockSize)
    {
        try
        {
            ProsperoCompressedPfsImage.PackFile(
                Utf8ToString(inputImagePath) ?? "",
                Utf8ToString(outputPath) ?? "",
                level > 0 ? level : ProsperoCompressedPfsImage.DefaultLevel,
                blockSize > 0 ? blockSize : ProsperoCompressedPfsImage.DefaultBlockSize);
            return 0;
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            return -1;
        }
    }

    /// <summary>
    /// Unpacks a PFSv3 PFSC container back into a plaintext PFS image.
    /// </summary>
    /// <returns>The number of bytes written on success; -1 on failure (call lpp_last_error).</returns>
    [UnmanagedCallersOnly(EntryPoint = "lpp_unpack_pfs_image")]
    public static long UnpackPfsImage(byte* inputPath, byte* outputPath)
    {
        try
        {
            return ProsperoCompressedPfsImage.UnpackFile(
                Utf8ToString(inputPath) ?? "",
                Utf8ToString(outputPath) ?? "");
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            return -1;
        }
    }

    /// <summary>Returns 1 when the buffer holds a SELF container, otherwise 0.</summary>
    [UnmanagedCallersOnly(EntryPoint = "lpp_is_self")]
    public static int IsSelf(byte* data, int length)
        => ProsperoFself.IsSelf(AsSpan(data, length)) ? 1 : 0;

    /// <summary>Returns 1 when the buffer holds a 64-bit ELF, otherwise 0.</summary>
    [UnmanagedCallersOnly(EntryPoint = "lpp_is_elf")]
    public static int IsElf(byte* data, int length)
        => ProsperoFself.IsElf(AsSpan(data, length)) ? 1 : 0;

    /// <summary>Returns 1 when the buffer holds a UCP archive, otherwise 0.</summary>
    [UnmanagedCallersOnly(EntryPoint = "lpp_is_ucp")]
    public static int IsUcp(byte* data, int length)
        => ProsperoUcp.IsUcp(AsSpan(data, length)) ? 1 : 0;

    /// <summary>
    /// Generates a fake-self from a 64-bit ELF. Pass <paramref name="outBuffer"/> = NULL or
    /// <paramref name="capacity"/> = 0 to query the required size (returned as a positive value
    /// without writing).
    /// </summary>
    /// <returns>
    /// The number of bytes written (or required, in query mode); -1 on failure or when a non-zero
    /// buffer is too small (call lpp_last_error for the message).
    /// </returns>
    [UnmanagedCallersOnly(EntryPoint = "lpp_make_fself")]
    public static int MakeFself(byte* elf, int elfLength, byte* outBuffer, int capacity)
    {
        try
        {
            if (elf is null || elfLength <= 0)
            {
                _lastError = "empty ELF input";
                return -1;
            }

            byte[] result = ProsperoFself.MakeFself(new ReadOnlySpan<byte>(elf, elfLength).ToArray());
            if (outBuffer is null || capacity <= 0)
                return result.Length;

            if (capacity < result.Length)
            {
                _lastError = $"buffer too small (need {result.Length})";
                return -1;
            }

            result.AsSpan().CopyTo(new Span<byte>(outBuffer, capacity));
            return result.Length;
        }
        catch (Exception ex)
        {
            _lastError = ex.Message;
            return -1;
        }
    }

    private static ReadOnlySpan<byte> AsSpan(byte* data, int length)
        => data is null || length <= 0 ? default : new ReadOnlySpan<byte>(data, length);

    private static string Fallback(string? value, string fallback)
        => string.IsNullOrEmpty(value) ? fallback : value;

    private static TEnum ToEnum<TEnum>(int value) where TEnum : struct, Enum
    {
        var result = (TEnum)Enum.ToObject(typeof(TEnum), value);
        if (!Enum.IsDefined(result))
            throw new ArgumentOutOfRangeException(nameof(value), value, $"Undefined {typeof(TEnum).Name} value.");
        return result;
    }

    private static string? Utf8ToString(byte* p)
        => p is null ? null : Marshal.PtrToStringUTF8((IntPtr)p);

    private static int WriteUtf8(string value, byte* buffer, int capacity)
    {
        if (buffer is null || capacity <= 0)
            return -1;

        int needed = Encoding.UTF8.GetByteCount(value);
        if (needed + 1 > capacity)
            return -(needed + 1); // caller must retry with at least this many bytes

        var span = new Span<byte>(buffer, capacity);
        int written = Encoding.UTF8.GetBytes(value, span);
        span[written] = 0;
        return written;
    }

    private static byte* AllocUtf8(string value)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(value);
        byte* p = (byte*)NativeMemory.Alloc((nuint)(bytes.Length + 1));
        for (int i = 0; i < bytes.Length; i++)
            p[i] = bytes[i];
        p[bytes.Length] = 0;
        return p;
    }
}
