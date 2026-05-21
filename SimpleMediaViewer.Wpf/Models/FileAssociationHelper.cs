using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace SimpleMediaViewer.Wpf;

public static class FileAssociationHelper
{
    private const string AppId = "SimpleMediaViewer";

    // Windows 標準の画像サムネイルプロバイダー (PhotoMetadataHandler.dll)
    // jpegfile\shellex\{e357fccd} と同じ値。WIC デコーダーを経由して画像をサムネイル化する。
    private const string PhotoThumbnailProviderClsid = "{C7657C4A-9F68-40fa-A4DF-96BC08EB3551}";

    // サムネイルハンドラーの shellex キー名
    private const string ThumbnailShellexGuid = "{e357fccd-a995-4576-b01f-234630154e96}";

    /// <summary>サポートする拡張子一覧</summary>
    public static readonly IReadOnlyList<string> SupportedExtensions =
        [".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".webm", ".mp4", ".avi", ".mkv", ".mov", ".flv", ".wmv"];


    // -------------------------------------------------------------------------
    // AppId (ProgID) 登録 --- 起動時に呼ぶ
    // -------------------------------------------------------------------------

    /// <summary>
    /// 未登録であれば ProgID とサムネイルハンドラーを登録します。登録済みの場合は何もしません。
    /// また、アプリ外で関連付けされた拡張子のうち、拡張子レベルの WIC サムネイルハンドラーが
    /// 未登録のものがあれば補完登録します。
    /// </summary>
    public static void RegisterAppIdIfNeeded()
    {
        if (!IsAppIdRegistered()) RegisterAppId();
        RegisterMissingExtensionThumbnailHandlers();
    }

    /// <summary>
    /// 関連付け済みかつ拡張子レベルの有効なサムネイルハンドラーが未登録の拡張子に対して
    /// ハンドラーを補完登録します。エクスプローラー等アプリ外で関連付けられたケースに対応します。
    /// </summary>
    public static void RegisterMissingExtensionThumbnailHandlers()
    {
        bool changed = false;
        foreach (var (ext, clsid) in ExtensionThumbnailProviders)
        {
            if (!IsAssociationRegistered(ext)) continue;
            if (IsEffectiveThumbnailHandlerRegistered(ext, clsid)) continue;

            // 拡張子レベルに登録
            using (var thumbKey = Registry.CurrentUser.CreateSubKey($@"SOFTWARE\Classes\{ext}\{ThumbnailHandlerShellexKey}"))
            {
                thumbKey.SetValue("", clsid);
            }

            // HKCU の .webp デフォルト ProgID にも登録（MassiGra 等が変更した場合に対応）
            RegisterProgIdThumbnailHandlerIfNeeded(ext, clsid);

            changed = true;
        }

        if (changed) SHChangeNotify();
    }

    /// <summary>
    /// 拡張子の HKCU デフォルト ProgID の shellex にサムネイルハンドラーが未登録であれば登録します。
    /// </summary>
    private static void RegisterProgIdThumbnailHandlerIfNeeded(string extension, string clsid)
    {
        var progId = Registry.CurrentUser.OpenSubKey($@"SOFTWARE\Classes\{extension}")
            ?.GetValue("") as string;
        if (string.IsNullOrEmpty(progId)) return;

        using var progIdKey = Registry.CurrentUser.OpenSubKey(
            $@"SOFTWARE\Classes\{progId}\shellex\{ThumbnailShellexGuid}");
        var existing = progIdKey?.GetValue("") as string;
        if (!string.IsNullOrEmpty(existing)) return;

        using var writeKey = Registry.CurrentUser.CreateSubKey(
            $@"SOFTWARE\Classes\{progId}\shellex\{ThumbnailShellexGuid}");
        writeKey.SetValue("", clsid);
    }

    /// <summary>
    /// 指定した拡張子に対して <paramref name="expectedClsid"/> と同じ有効なサムネイルハンドラーが
    /// HKCU または信頼済み HKLM エントリとして登録されているかどうかを確認します。
    /// HKLM に非対応ハンドラー（Icaros 等）があっても false を返し、HKCU への上書きを許可します。
    /// </summary>
    private static bool IsEffectiveThumbnailHandlerRegistered(string extension, string expectedClsid)
    {
        // HKCU に自アプリの CLSID が登録済みなら OK
        using (var hkcuKey = Registry.CurrentUser.OpenSubKey(
            $@"SOFTWARE\Classes\{extension}\{ThumbnailHandlerShellexKey}"))
        {
            if (hkcuKey?.GetValue("") is string clsid && clsid.Equals(expectedClsid, StringComparison.OrdinalIgnoreCase))
                return true;
        }

        // HKLM に信頼できる既知ハンドラーがあれば上書き不要
        using (var hklmKey = Registry.LocalMachine.OpenSubKey(
            $@"SOFTWARE\Classes\{extension}\{ThumbnailHandlerShellexKey}"))
        {
            if (hklmKey?.GetValue("") is string clsid && TrustedWebpThumbnailHandlers.Contains(clsid))
                return true;
        }

        return false;
    }


    /// <summary>
    /// ProgID が登録済みかどうかを確認します。
    /// </summary>
    public static bool IsAppIdRegistered()
    {
        using var appKey = Registry.CurrentUser.OpenSubKey($@"SOFTWARE\Classes\{AppId}");
        if (appKey is null) return false;

        using var cmdKey = appKey.OpenSubKey(@"shell\open\command");
        var value = cmdKey?.GetValue("") as string;
        return !string.IsNullOrEmpty(value);
    }


    /// <summary>
    /// ProgID・アイコン・開くコマンド・サムネイルハンドラーを登録します。
    /// </summary>
    public static void RegisterAppId()
    {
        string exePath = Environment.ProcessPath ?? Assembly.GetExecutingAssembly().Location;

        // ProgID 登録
        using var appKey = Registry.CurrentUser.CreateSubKey($@"SOFTWARE\Classes\{AppId}");
        appKey.SetValue("", "SimpleMediaViewer");

        // アイコン登録
        using (var iconKey = appKey.CreateSubKey("DefaultIcon"))
        {
            iconKey.SetValue("", $"\"{exePath}\",0");
        }

        // 開くコマンド登録
        using (var cmdKey = appKey.CreateSubKey(@"shell\open\command"))
        {
            cmdKey.SetValue("", $"\"{exePath}\" \"%1\"");
        }

        // サムネイルハンドラー (Photo Thumbnail Provider — jpegfile\shellex と同じ設定)
        using (var thumbKey = appKey.CreateSubKey($@"shellex\{ThumbnailShellexGuid}"))
        {
            thumbKey.SetValue("", PhotoThumbnailProviderClsid);
        }

        SHChangeNotify();
    }


    /// <summary>
    /// ProgID の登録をすべて解除します。
    /// </summary>
    public static void UnregisterAppId()
    {
        // 念のため全拡張子の OpenWithProgids からも削除
        foreach (var ext in SupportedExtensions)
        {
            using var extKey = Registry.CurrentUser.OpenSubKey($@"SOFTWARE\Classes\{ext}\OpenWithProgids", writable: true);
            extKey?.DeleteValue(AppId, throwOnMissingValue: false);
        }

        Registry.CurrentUser.DeleteSubKeyTree($@"SOFTWARE\Classes\{AppId}", throwOnMissingSubKey: false);

        SHChangeNotify();
    }


    // -------------------------------------------------------------------------
    // ファイル関連付け --- ユーザー操作で呼ぶ
    // -------------------------------------------------------------------------

    /// <summary>
    /// 指定した拡張子が関連付け済みかどうかを確認します。
    /// </summary>
    /// <param name="extension">例: ".jpg"</param>
    public static bool IsAssociationRegistered(string extension)
    {
        using var extKey = Registry.CurrentUser.OpenSubKey($@"SOFTWARE\Classes\{extension}\OpenWithProgids");
        if (extKey is null) return false;

        var value = extKey.GetValue(AppId);
        return value is not null;
    }


    // 拡張子レベルで shellex にサムネイルハンドラーを個別登録が必要な拡張子と使用する CLSID
    // .webp は ProgID に Photo Thumbnail Provider が登録されないため拡張子レベルで補完する
    private static readonly IReadOnlyDictionary<string, string> ExtensionThumbnailProviders =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            { ".webp", PhotoThumbnailProviderClsid },
        };

    // サムネイルハンドラー shellex キーのパス
    private const string ThumbnailHandlerShellexKey = $@"shellex\{ThumbnailShellexGuid}";

    // 信頼できるサードパーティ製 WebP サムネイルハンドラーの CLSID 一覧（これらが HKLM にある場合は上書きしない）
    // Icaros ({c5aec3ec}) は WebP 静止画をサポートしていないため除外する
    private static readonly IReadOnlySet<string> TrustedWebpThumbnailHandlers = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        // 現時点では信頼できる既知ハンドラーなし。Icaros は WebP 非対応のため含めない。
    };


    /// <summary>
    /// 指定した拡張子のファイル関連付けを登録します。
    /// </summary>
    /// <param name="extensions">登録する拡張子。省略時は <see cref="SupportedExtensions"/> 全件。</param>
    public static void RegisterAssociation(IEnumerable<string>? extensions = null)
    {
        foreach (var ext in extensions ?? SupportedExtensions)
        {
            using var extKey = Registry.CurrentUser.CreateSubKey($@"SOFTWARE\Classes\{ext}\OpenWithProgids");
            extKey.SetValue(AppId, Array.Empty<byte>(), RegistryValueKind.None);

            // 拡張子レベルでサムネイルハンドラーを直接登録
            // HKLM に有効なハンドラーがない場合は HKCU に上書きして補完する
            if (ExtensionThumbnailProviders.TryGetValue(ext, out var clsid) && !IsEffectiveThumbnailHandlerRegistered(ext, clsid))
            {
                using var thumbKey = Registry.CurrentUser.CreateSubKey($@"SOFTWARE\Classes\{ext}\{ThumbnailHandlerShellexKey}");
                thumbKey.SetValue("", clsid);

                RegisterProgIdThumbnailHandlerIfNeeded(ext, clsid);
            }
        }

        SHChangeNotify();
    }


    /// <summary>
    /// 指定した拡張子のファイル関連付けを解除します。
    /// </summary>
    /// <param name="extensions">解除する拡張子。省略時は <see cref="SupportedExtensions"/> 全件。</param>
    public static void UnregisterAssociation(IEnumerable<string>? extensions = null)
    {
        foreach (var ext in extensions ?? SupportedExtensions)
        {
            using var extKey = Registry.CurrentUser.OpenSubKey($@"SOFTWARE\Classes\{ext}\OpenWithProgids", writable: true);
            extKey?.DeleteValue(AppId, throwOnMissingValue: false);

            // 自アプリが HKCU に登録したサムネイルハンドラーのみ削除
            if (ExtensionThumbnailProviders.TryGetValue(ext, out var ownClsid))
            {
                RemoveOwnThumbnailHandler(ext, ownClsid);
            }
        }

        SHChangeNotify();
    }


    /// <summary>
    /// 自アプリが HKCU に登録したサムネイルハンドラーを拡張子レベルおよび ProgID レベルから削除します。
    /// </summary>
    private static void RemoveOwnThumbnailHandler(string extension, string ownClsid)
    {
        // 拡張子レベル
        using (var key = Registry.CurrentUser.OpenSubKey(
            $@"SOFTWARE\Classes\{extension}\{ThumbnailHandlerShellexKey}"))
        {
            if (key?.GetValue("") is string clsid && clsid.Equals(ownClsid, StringComparison.OrdinalIgnoreCase))
            {
                Registry.CurrentUser.DeleteSubKeyTree(
                    $@"SOFTWARE\Classes\{extension}\{ThumbnailHandlerShellexKey}",
                    throwOnMissingSubKey: false);
            }
        }

        // ProgID レベル（自アプリが書いたものだけ削除）
        var progId = Registry.CurrentUser.OpenSubKey($@"SOFTWARE\Classes\{extension}")
            ?.GetValue("") as string;
        if (string.IsNullOrEmpty(progId)) return;

        using var progIdKey = Registry.CurrentUser.OpenSubKey(
            $@"SOFTWARE\Classes\{progId}\shellex\{ThumbnailShellexGuid}");
        if (progIdKey?.GetValue("") is string progIdClsid
            && progIdClsid.Equals(ownClsid, StringComparison.OrdinalIgnoreCase))
        {
            Registry.CurrentUser.DeleteSubKeyTree(
                $@"SOFTWARE\Classes\{progId}\shellex\{ThumbnailShellexGuid}",
                throwOnMissingSubKey: false);
        }
    }


    /// <summary>
    /// シェルに対してファイル関連付けの変更を通知します。
    /// </summary>
    /// <param name="wEventId"></param>
    /// <param name="uFlags"></param>
    /// <param name="dwItem1"></param>
    /// <param name="dwItem2"></param>
    [System.Runtime.InteropServices.DllImport("shell32.dll")]
    private static extern void SHChangeNotify(
        int wEventId = 0x08000000, // SHCNE_ASSOCCHANGED
        uint uFlags = 0,
        nint dwItem1 = 0,
        nint dwItem2 = 0);
}