using System;
using System.Collections.Generic;
using System.Text;

namespace SimpleMediaViewer.Wpf;

public class FileManager
{
    private string CurrentDirectory 
    { 
        get;
        set
        {
            if (value != field)
            {
                field = value;
                if (System.IO.Directory.Exists(value))
                {
                    var supportedExtensions = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
                    {
                        ".mp4", ".jpg", ".jpeg", ".png", ".gif", ".webp"
                    };
                    this.CurrentFileList = System.IO.Directory.GetFiles(value)
                        .Where(file => supportedExtensions.Contains(System.IO.Path.GetExtension(file)))
                        .ToArray();
                }
                else
                {
                    this.CurrentFileList = Array.Empty<string>();
                }
            }
        }
    } = string.Empty;

    public string CurrentFilePath
    { 
        get;
        set
        {
            var dir = System.IO.Path.GetDirectoryName(value);
            if (dir != this.CurrentDirectory)
            {
                this.CurrentDirectory = dir ?? string.Empty;
            }

            if (value != field)
            {
                field = value;
            }
        }
    } = string.Empty;

    private string[] CurrentFileList { get; set; } = Array.Empty<string>();


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
}
