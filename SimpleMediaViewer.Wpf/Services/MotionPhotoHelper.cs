using System;
using System.IO;
using System.Text;

namespace SimpleMediaViewer.Wpf.Services;

/// <summary>
/// Google Pixel モーションフォト（Motion Photo）の検出と動画抽出を行うヘルパー
/// </summary>
public static class MotionPhotoHelper
{
    /// <summary>
    /// JPEGファイルがモーションフォトかどうか確認し、埋め込みMP4の先頭オフセットを返す
    /// </summary>
    /// <param name="jpegPath">JPEGファイルパス</param>
    /// <param name="videoOffset">MP4データの先頭オフセット（バイト）</param>
    /// <returns>モーションフォトであれば true</returns>
    public static bool TryGetEmbeddedVideoOffset(string jpegPath, out long videoOffset)
    {
        videoOffset = 0;

        try
        {
            long fileSize = new FileInfo(jpegPath).Length;

            // XMPメタデータからMP4サイズを取得（先頭128KBを読む）
            int readSize = (int)Math.Min(131072, fileSize);
            byte[] header = new byte[readSize];

            using (var fs = new FileStream(jpegPath, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                fs.ReadExactly(header, 0, readSize);
            }

            if (TryGetVideoSizeFromXmp(header, out long videoSize) && videoSize > 0 && videoSize < fileSize)
            {
                videoOffset = fileSize - videoSize;
                return true;
            }
        }
        catch { }

        return false;
    }


    /// <summary>
    /// 埋め込みMP4を一時ファイルに抽出し、そのパスを返す
    /// </summary>
    public static string ExtractEmbeddedVideo(string jpegPath, long videoOffset)
    {
        string tempPath = Path.Combine(Path.GetTempPath(), $"motionphoto_{Guid.NewGuid():N}.mp4");

        using var input = new FileStream(jpegPath, FileMode.Open, FileAccess.Read, FileShare.Read);
        input.Seek(videoOffset, SeekOrigin.Begin);

        using var output = new FileStream(tempPath, FileMode.Create, FileAccess.Write);
        input.CopyTo(output);

        return tempPath;
    }


    // -------------------------------------------------------------------------
    // XMP解析
    // -------------------------------------------------------------------------
    /// <summary>
    /// XMP解析
    /// </summary>
    /// <param name="header"></param>
    /// <param name="videoSize"></param>
    /// <returns></returns>
    private static bool TryGetVideoSizeFromXmp(byte[] header, out long videoSize)
    {
        videoSize = 0;

        // XMPはバイナリに埋め込まれているため Latin-1 で読む（UTF-8だとAPP1マーカー付近で失敗する場合がある）
        string text = Encoding.Latin1.GetString(header);

        // --- 旧形式 ---
        // Camera:MotionPhotoVideoFileSize="NNNN"
        // GCamera:MicroVideoLength="NNNN"
        string[] legacyPatterns =
        [
            "Camera:MotionPhotoVideoFileSize=\"",
            "GCamera:MicroVideoLength=\"",
        ];

        foreach (var pattern in legacyPatterns)
        {
            int idx = text.IndexOf(pattern, StringComparison.Ordinal);
            if (idx < 0) continue;

            int start = idx + pattern.Length;
            int end = text.IndexOf('"', start);
            if (end > start && long.TryParse(text.AsSpan(start, end - start), out videoSize))
                return true;
        }

        // --- 新形式 (Container 形式) ---
        // <Container:Item Item:Mime="video/mp4" Item:Semantic="MotionPhoto"
        //                 Item:Length="NNNN" Item:Padding="N"/>
        // Item:Length と Item:Padding の合計がファイル末尾からのバイト数
        int videoMimeIdx = text.IndexOf("video/mp4", StringComparison.Ordinal);
        if (videoMimeIdx >= 0)
        {
            // "video/mp4" を含む <Container:Item ブロックの開始タグを探す（後ろから）
            int itemTagStart = text.LastIndexOf("<", videoMimeIdx, videoMimeIdx);
            // ブロックの終端 /> を探す（前方）
            int itemTagEnd = text.IndexOf("/>", videoMimeIdx);
            if (itemTagStart >= 0 && itemTagEnd > itemTagStart)
            {
                string block = text.Substring(itemTagStart, itemTagEnd - itemTagStart + 2);

                long length = ParseXmpAttribute(block, "Item:Length");
                long padding = ParseXmpAttribute(block, "Item:Padding"); // 省略時は0

                if (length > 0)
                {
                    videoSize = length + padding;
                    return true;
                }
            }
        }

        return false;
    }


    /// <summary>
    /// XMLテキストブロックから  AttrName="VALUE"  または  AttrName='VALUE'  を取り出す
    /// </summary>
    private static long ParseXmpAttribute(string block, string attrName)
    {
        // 属性名="  または  属性名='
        foreach (var quote in new[] { '"', '\'' })
        {
            string pattern = $"{attrName}={quote}";
            int idx = block.IndexOf(pattern, StringComparison.Ordinal);
            if (idx < 0) continue;

            int start = idx + pattern.Length;
            int end = block.IndexOf(quote, start);
            if (end > start && long.TryParse(block.AsSpan(start, end - start), out long value))
                return value;
        }

        return 0;
    }
}
