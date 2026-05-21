using CommunityToolkit.Mvvm.ComponentModel;
using System.IO;

namespace SimpleMediaViewer.Wpf;

/// <summary>
/// ファイルの並び順
/// </summary>
public enum FileSortOrder
{
    /// <summary>名前 昇順</summary>
    NameAscending,
    /// <summary>名前 降順</summary>
    NameDescending,
    /// <summary>更新日時 昇順</summary>
    DateAscending,
    /// <summary>更新日時 降順</summary>
    DateDescending,
    /// <summary>ファイルサイズ 昇順</summary>
    SizeAscending,
    /// <summary>ファイルサイズ 降順</summary>
    SizeDescending,
    /// <summary> ランダム  </summary>
    Random,
}


public class FileManager : ObservableObject
{
    private FileSortOrder _sortOrder = FileSortOrder.NameAscending;

    // ファイルメタデータキャッシュ（パス → FileInfo相当の情報）
    private record FileMetaCache(string Path, string FileName, DateTime LastWriteTime, long Length);
    private FileMetaCache[] _fileMetaCache = [];


    /// <summary>
    /// 現在のソート順
    /// </summary>
    public FileSortOrder SortOrder
    {
        get => _sortOrder;
        set
        {
            if ((SetProperty(ref _sortOrder, value)) || (value == FileSortOrder.Random))
            {
                // ソート順が変更されたらファイルリストを再ソート
                SortFileList();
            }
        }
    }


    /// <summary>
    /// 現在開いているファイルのディレクトリ
    /// </summary>
    private string CurrentDirectory
    { 
        get;
        set
        {
            if (value != field)
            {
                // カレントディレクトリが変更された場合、ファイルリストを更新
                if (System.IO.Directory.Exists(value))
                {
                    // FileInfo を一度だけ取得してキャッシュ（並列で高速化）
                    _fileMetaCache = Directory.EnumerateFiles(value)
                        .Where(f => FileAssociationHelper.SupportedExtensions.Contains(Path.GetExtension(f)))
                        .AsParallel()
                        .Select(f =>
                        {
                            var info = new FileInfo(f);
                            return new FileMetaCache(f, info.Name, info.LastWriteTime, info.Length);
                        })
                        .ToArray();

                    // 取得後にソート
                    SortFileList();
                }
                else
                {
                    _fileMetaCache = [];
                    CurrentFileList = [];
                }

                SetProperty(ref field, value);
            }
        }
    } = string.Empty;


    /// <summary>
    /// 現在開いているファイル
    /// </summary>
    public string CurrentFilePath
    { 
        get;
        set
        {
            // カレントファイルが変更された場合、カレントディレクトリも更新
            var dir = System.IO.Path.GetDirectoryName(value);
            if (dir != this.CurrentDirectory)
            {
                this.CurrentDirectory = dir ?? string.Empty;
            }

            SetProperty(ref field, value);
        }
    } = string.Empty;


    /// <summary>
    /// 現在開いているディレクトリ内のファイルリスト
    /// </summary>
    public string[] CurrentFileList { get; private set; } = [];


    /// <summary>
    /// ファイルリストから次のファイルを取得する
    /// </summary>
    /// <returns></returns>
    public string GetNextFile()
    {
        if (CurrentFileList.Length == 0)
        {
            return string.Empty;
        }
        var currentIndex = Array.IndexOf(CurrentFileList, CurrentFilePath);
        if (currentIndex < 0 || currentIndex >= CurrentFileList.Length - 1)
        {
            return CurrentFileList[0];
        }
        return CurrentFileList[currentIndex + 1];
    }


    /// <summary>
    /// ファイルリストから前のファイルを取得する
    /// </summary>
    /// <returns></returns>
    public string GetPreviousFile()
    {
        if (CurrentFileList.Length == 0)
        {
            return string.Empty;
        }
        var currentIndex = Array.IndexOf(CurrentFileList, CurrentFilePath);
        if (currentIndex <= 0)
        {
            return CurrentFileList[CurrentFileList.Length - 1];
        }
        return CurrentFileList[currentIndex - 1];
    }


    /// <summary>
    /// ファイルリストをソートする
    /// </summary>
    private void SortFileList()
    {
        if (_fileMetaCache.Length == 0)
        {
            CurrentFileList = [];
            return;
        }

        // キャッシュ済みメタデータでソート（I/O なし）
        var sorted = SortOrder switch
        {
            FileSortOrder.NameAscending    => _fileMetaCache.OrderBy(f => f.FileName, StringComparer.OrdinalIgnoreCase),
            FileSortOrder.NameDescending   => _fileMetaCache.OrderByDescending(f => f.FileName, StringComparer.OrdinalIgnoreCase),
            FileSortOrder.DateAscending    => _fileMetaCache.OrderBy(f => f.LastWriteTime),
            FileSortOrder.DateDescending   => _fileMetaCache.OrderByDescending(f => f.LastWriteTime),
            FileSortOrder.SizeAscending    => _fileMetaCache.OrderBy(f => f.Length),
            FileSortOrder.SizeDescending   => _fileMetaCache.OrderByDescending(f => f.Length),
            FileSortOrder.Random           => _fileMetaCache.OrderBy(_ => Random.Shared.Next()),
            _                              => _fileMetaCache.AsEnumerable()
        };

        CurrentFileList = sorted.Select(f => f.Path).ToArray();
        OnPropertyChanged(nameof(CurrentFileList));
    }
}
