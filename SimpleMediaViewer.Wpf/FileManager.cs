using CommunityToolkit.Mvvm.ComponentModel;
using System;
using System.Collections.Generic;
using System.Text;

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
                    var supportedExtensions = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
                    {
                        ".mp4", ".jpg", ".jpeg", ".png", ".gif", ".webp"
                    };
                    this.CurrentFileList = System.IO.Directory.GetFiles(value)
                        .Where(file => supportedExtensions.Contains(System.IO.Path.GetExtension(file)))
                        .ToArray();

                    // 取得後にソート
                    SortFileList();
                }
                else
                {
                    this.CurrentFileList = Array.Empty<string>();
                }

                SetProperty(ref field, value);
            }
        }
    } = string.Empty;

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


    public string[] CurrentFileList { get; private set; } = Array.Empty<string>();


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
        if (CurrentFileList.Length == 0)
        {
            return;
        }

        var sorted = SortOrder switch
        {
            FileSortOrder.NameAscending => CurrentFileList.OrderBy(f => System.IO.Path.GetFileName(f)).ToArray(),
            FileSortOrder.NameDescending => CurrentFileList.OrderByDescending(f => System.IO.Path.GetFileName(f)).ToArray(),
            FileSortOrder.DateAscending => CurrentFileList.OrderBy(f => System.IO.File.GetLastWriteTime(f)).ToArray(),
            FileSortOrder.DateDescending => CurrentFileList.OrderByDescending(f => System.IO.File.GetLastWriteTime(f)).ToArray(),
            FileSortOrder.SizeAscending => CurrentFileList.OrderBy(f => new System.IO.FileInfo(f).Length).ToArray(),
            FileSortOrder.SizeDescending => CurrentFileList.OrderByDescending(f => new System.IO.FileInfo(f).Length).ToArray(),
            FileSortOrder.Random => CurrentFileList.OrderBy(_ => Random.Shared.Next()).ToArray(),
            _ => CurrentFileList
        };

        CurrentFileList = sorted;
        OnPropertyChanged(nameof(CurrentFileList));
    }
}
