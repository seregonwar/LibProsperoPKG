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
using LibProsperoPkg.PKG;

namespace LibProsperoPkg.Native;

internal static unsafe class NativeExports
{
    [ThreadStatic]
    private static string? _lastError;

    private static readonly byte* VersionPtr = AllocUtf8("LibProsperoPkg 1.0.0");

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
