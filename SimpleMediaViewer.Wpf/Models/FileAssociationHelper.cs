using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;

namespace SimpleMediaViewer.Wpf;

public static class FileAssociationHelper
{
    private const string AppId = "SimpleMediaViewer";

    // Windows 組み込みの画像サムネイルプロバイダー (Windows Imaging Component)
    private const string WicThumbnailProviderClsid = "{e357fccd-a995-4576-b01f-234630154e96}";

    /// <summary>サポートする拡張子一覧</summary>
    public static readonly IReadOnlyList<string> SupportedExtensions =
        [".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".mp4", ".avi", ".mkv", ".mov"];


    // -------------------------------------------------------------------------
    // AppId (ProgID) 登録 --- 起動時に呼ぶ
    // -------------------------------------------------------------------------

    /// <summary>
    /// 未登録であれば ProgID とサムネイルハンドラーを登録します。登録済みの場合は何もしません。
    /// </summary>
    public static void RegisterAppIdIfNeeded()
    {
        if (IsAppIdRegistered()) return;
        RegisterAppId();
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

        using var appKey = Registry.CurrentUser.CreateSubKey($@"SOFTWARE\Classes\{AppId}");
        appKey.SetValue("", "SimpleMediaViewer");

        using (var iconKey = appKey.CreateSubKey("DefaultIcon"))
        {
            iconKey.SetValue("", $"\"{exePath}\",0");
        }

        using (var cmdKey = appKey.CreateSubKey(@"shell\open\command"))
        {
            cmdKey.SetValue("", $"\"{exePath}\" \"%1\"");
        }

        // サムネイルハンドラー (WIC)
        using (var thumbKey = appKey.CreateSubKey($@"shellex\{{e357fccd-a995-4576-b01f-234630154e96}}"))
        {
            thumbKey.SetValue("", WicThumbnailProviderClsid);
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
        }

        SHChangeNotify();
    }


    [System.Runtime.InteropServices.DllImport("shell32.dll")]
    private static extern void SHChangeNotify(
        int wEventId = 0x08000000, // SHCNE_ASSOCCHANGED
        uint uFlags = 0,
        nint dwItem1 = 0,
        nint dwItem2 = 0);
}