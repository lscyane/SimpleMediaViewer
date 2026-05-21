using CommunityToolkit.Mvvm.Input;
using LibVLCSharp.Shared;
using lscyane.Core.Mvvm;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace SimpleMediaViewer.Wpf;


public class FileListDialogViewModel : DialogViewModelBase, INotifyPropertyChanged
{
    private ObservableCollection<string> _fileList = new ObservableCollection<string>();
    private Action<string>? _onFileSelected;
    private bool _isUpdatingSelection = false;


    /// <summary>
    /// コンストラクタ
    /// </summary>
    public FileListDialogViewModel()
    {
    }


    public FileManager FileManager 
    { 
        get => field;
        private set
        {
            if (field != value)
            {
                // 既存のイベントハンドラを解除
                if (field != null)
                {
                    field.PropertyChanged -= OnFileManagerPropertyChanged;
                }

                field = value;

                // 新しいイベントハンドラを登録
                if (field != null)
                {
                    field.PropertyChanged += OnFileManagerPropertyChanged;
                }

                OnPropertyChanged();
            }
        }
    } = new FileManager();


    public int SelectedIndex
    {
        get => field;
        set
        {
            if (SetProperty(ref field, value))
            {
                // FileManager側の変更による更新の場合は、コールバックを呼ばない
                if (!_isUpdatingSelection)
                {
                    OnSelectedIndexChanged();
                }
            }
        }
    } = -1;


    private void OnSelectedIndexChanged()
    {
        if (SelectedIndex >= 0 && SelectedIndex < FileManager.FileMetaList.Length)
        {
            var selectedFile = FileManager.FileMetaList[SelectedIndex].Path;
            _onFileSelected?.Invoke(selectedFile);
        }
    }


    /// <summary>
    /// FileManageのプロパティ変更イベントハンドラ
    /// </summary>
    /// <param name="sender"></param>
    /// <param name="e"></param>
    private void OnFileManagerPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        // FileManageのCurrentFilePathの変更を監視して、SelectedIndexを更新する
        if (e.PropertyName == nameof(FileManager.CurrentFilePath))
        {
            UpdateSelectedIndex();
        }
    }


    private void UpdateSelectedIndex()
    {
        var currentFile = FileManager.CurrentFilePath;
        if (!string.IsNullOrEmpty(currentFile))
        {
            var index = Array.IndexOf(FileManager.FileMetaList.Select(f => f.Path).ToArray(), currentFile);
            if (index >= 0 && index != SelectedIndex)
            {
                _isUpdatingSelection = true;
                SelectedIndex = index;
                _isUpdatingSelection = false;
            }
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


    /// <inheritdoc/>
    public override void OnDialogPreviewOpen(DialogParameters? parameters)
    {
        this.Title = parameters?.GetValue<string>("Title") ?? throw new ArgumentException();
        this.FileManager = parameters?.GetValue<FileManager>("Manager") ?? throw new ArgumentException();
        this._onFileSelected = parameters?.GetValue<Action<string>?>("OnFileSelected");
    }


    /// <summary>
    /// キャンセルボタン
    /// </summary>
    /// <remarks> 閉じるボタンと同じ挙動にするには、ここでは何も処理せず呼び出し元のResultで処理することを検討 </remarks>
    public RelayCommand<object> CancelCommand => field ??= new RelayCommand<object>(e => 
    {
        CleanupEventHandlers();
        this.OnRequestClose();
    });


    /// <summary>
    /// OKボタン
    /// </summary>
    public RelayCommand<object> OkCommand => field ??= new RelayCommand<object>(e =>
    {
        CleanupEventHandlers();
        var retval = new DialogParameters();
        this.OnRequestClose(true, retval);
    });


    /// <summary>
    /// イベントハンドラのクリーンアップ
    /// </summary>
    private void CleanupEventHandlers()
    {
        if (this.FileManager != null)
        {
            this.FileManager.PropertyChanged -= OnFileManagerPropertyChanged;
        }
    }
}

