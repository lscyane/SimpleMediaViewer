using CommunityToolkit.Mvvm.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

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
    private FileMeta[] _allFileMetaList = [];
    private HashSet<string> _selectedExtensions = new(FileAssociationHelper.SupportedExtensions, StringComparer.OrdinalIgnoreCase);

    /// <summary>
    /// Windowsエクスプローラーと同様の自然順比較（StrCmpLogicalW）
    /// </summary>
    private sealed class NaturalStringComparer : IComparer<string>
    {
        public static readonly NaturalStringComparer Instance = new();

        [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
        private static extern int StrCmpLogicalW(string x, string y);

        public int Compare(string? x, string? y) => StrCmpLogicalW(x ?? "", y ?? "");
    }

    // ファイルメタデータキャッシュ（パス → FileInfo相当の情報）
    public record FileMeta(string Path, string FileName, DateTime LastWriteTime, long Length);
    public FileMeta[] FileMetaList { get; private set => SetProperty(ref field, value); } = [];

    /// <summary>
    /// 現在フィルター対象として選択されている拡張子
    /// </summary>
    public IReadOnlyCollection<string> SelectedExtensions => _selectedExtensions;

    /// <summary>
    /// 表示する拡張子を設定する
    /// </summary>
    public void SetSelectedExtensions(IEnumerable<string> extensions)
    {
        _selectedExtensions = new HashSet<string>(extensions, StringComparer.OrdinalIgnoreCase);
        RefreshFileMetaList();
    }


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
    public string CurrentDirectory
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
                    _allFileMetaList = Directory.EnumerateFiles(value)
                        .Where(f => FileAssociationHelper.SupportedExtensions.Contains(Path.GetExtension(f), StringComparer.OrdinalIgnoreCase))
                        .AsParallel()
                        .Select(f =>
                        {
                            var info = new FileInfo(f);
                            return new FileMeta(f, info.Name, info.LastWriteTime, info.Length);
                        })
                        .ToArray();

                    RefreshFileMetaList();
                }
                else
                {
                    _allFileMetaList = [];
                    FileMetaList = [];
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
    /// ファイルリストから次のファイルを取得する
    /// </summary>
    /// <returns></returns>
    public string GetNextFile()
    {
        if (FileMetaList.Length == 0)
        {
            return string.Empty;
        }
        var currentIndex = Array.IndexOf(FileMetaList.Select(f => f.Path).ToArray(), CurrentFilePath);
        if (currentIndex < 0 || currentIndex >= FileMetaList.Length - 1)
        {
            return FileMetaList[0].Path;
        }
        return FileMetaList[currentIndex + 1].Path;
    }


    /// <summary>
    /// ファイルリストから前のファイルを取得する
    /// </summary>
    /// <returns></returns>
    public string GetPreviousFile()
    {
        if (FileMetaList.Length == 0)
        {
            return string.Empty;
        }
        var currentIndex = Array.IndexOf(FileMetaList.Select(f => f.Path).ToArray(), CurrentFilePath);
        if (currentIndex <= 0)
        {
            return FileMetaList[FileMetaList.Length - 1].Path;
        }
        return FileMetaList[currentIndex - 1].Path;
    }


    /// <summary>
    /// ファイルリストをソートする
    /// </summary>
    private void SortFileList()
    {
        if (FileMetaList.Length == 0)
        {
            return;
        }

        // キャッシュ済みメタデータでソート（I/O なし）
        var sorted = SortOrder switch
        {
            FileSortOrder.NameAscending    => FileMetaList.OrderBy(f => f.FileName, NaturalStringComparer.Instance),
            FileSortOrder.NameDescending   => FileMetaList.OrderByDescending(f => f.FileName, NaturalStringComparer.Instance),
            FileSortOrder.DateAscending    => FileMetaList.OrderBy(f => f.LastWriteTime),
            FileSortOrder.DateDescending   => FileMetaList.OrderByDescending(f => f.LastWriteTime),
            FileSortOrder.SizeAscending    => FileMetaList.OrderBy(f => f.Length),
            FileSortOrder.SizeDescending   => FileMetaList.OrderByDescending(f => f.Length),
            FileSortOrder.Random           => FileMetaList.OrderBy(_ => Random.Shared.Next()),
            _                              => FileMetaList.AsEnumerable()
        };

        FileMetaList = sorted.ToArray();
    }


    /// <summary>
    /// 選択された拡張子でファイル一覧を更新する
    /// </summary>
    private void RefreshFileMetaList()
    {
        FileMetaList = _allFileMetaList
            .Where(file => _selectedExtensions.Contains(Path.GetExtension(file.Path)))
            .ToArray();
        SortFileList();
    }
}
