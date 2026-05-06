using CommunityToolkit.Mvvm.Input;
using LibVLCSharp.Shared;
using lscyane.Core.Mvvm;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace SimpleMediaViewer.Wpf;


public class FileListDialogViewModel : DialogViewModelBase, INotifyPropertyChanged
{
    private FileManager _fileManager = new FileManager();
    private ObservableCollection<string> _fileList = new ObservableCollection<string>();
    private int _selectedIndex = -1;
    private Action<string>? _onFileSelected;

    public FileManager FileManager 
    { 
        get => _fileManager;
        private set
        {
            if (_fileManager != value)
            {
                _fileManager = value;
                OnPropertyChanged();
                UpdateFileList();
            }
        }
    }

    public ObservableCollection<string> FileList
    {
        get => _fileList;
        private set
        {
            if (_fileList != value)
            {
                _fileList = value;
                OnPropertyChanged();
            }
        }
    }

    public int SelectedIndex
    {
        get => _selectedIndex;
        set
        {
            if (_selectedIndex != value)
            {
                _selectedIndex = value;
                OnPropertyChanged();
                OnSelectedIndexChanged();
            }
        }
    }

    public new event PropertyChangedEventHandler? PropertyChanged;

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    private void UpdateFileList()
    {
        FileList.Clear();
        foreach (var file in FileManager.CurrentFileList)
        {
            FileList.Add(System.IO.Path.GetFileName(file));
        }

        // 現在のファイルを選択状態にする
        var currentFile = FileManager.CurrentFilePath;
        if (!string.IsNullOrEmpty(currentFile))
        {
            var index = Array.IndexOf(FileManager.CurrentFileList, currentFile);
            if (index >= 0)
            {
                SelectedIndex = index;
            }
        }
    }

    private void OnSelectedIndexChanged()
    {
        if (SelectedIndex >= 0 && SelectedIndex < FileManager.CurrentFileList.Length)
        {
            var selectedFile = FileManager.CurrentFileList[SelectedIndex];
            _onFileSelected?.Invoke(selectedFile);
        }
    }


    #region Window位置・サイズ関係プロパティ
    public double WindowLeft
    {
        get => Properties.EnvSettings.Default.FileListDialogLeft;
        set => Properties.EnvSettings.Default.FileListDialogLeft = value;
    }

    public double WindowTop
    {
        get => Properties.EnvSettings.Default.FileListDialogTop;
        set => Properties.EnvSettings.Default.FileListDialogTop = value;
    }

    public double WindowWidth
    {
        get => Properties.EnvSettings.Default.FileListDialogWidth;
        set => Properties.EnvSettings.Default.FileListDialogWidth = value;
    }

    public double WindowHeight
    {
        get => Properties.EnvSettings.Default.FileListDialogHeight;
        set => Properties.EnvSettings.Default.FileListDialogHeight = value;
    }
    #endregion


    /// <summary>
    /// コンストラクタ
    /// </summary>
    public FileListDialogViewModel()
    {
    }


    /// <inheritdoc/>
    public override void OnDialogPreviewOpen(DialogParameters? parameters)
    {
        this.Title = parameters?.GetValue<string>("Title") ?? throw new ArgumentException();
        this.FileManager = parameters?.GetValue<FileManager>("Manager") ?? throw new ArgumentException();
        this._onFileSelected = parameters?.GetValue<Action<string>?>("OnFileSelected");
        UpdateFileList();
    }


    /// <summary>
    /// キャンセルボタン
    /// </summary>
    /// <remarks> 閉じるボタンと同じ挙動にするには、ここでは何も処理せず呼び出し元のResultで処理することを検討 </remarks>
    public RelayCommand<object> CancelCommand => field ??= new RelayCommand<object>(e => this.OnRequestClose());


    /// <summary>
    /// OKボタン
    /// </summary>
    public RelayCommand<object> OkCommand => field ??= new RelayCommand<object>(e =>
    {
        var retval = new DialogParameters();
        this.OnRequestClose(true, retval);
    });
}

