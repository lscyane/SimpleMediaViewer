using CommunityToolkit.Mvvm.Input;
using LibVLCSharp.Shared;
using lscyane.Core.Mvvm;

namespace SimpleMediaViewer.Wpf;


public class FileListDialogViewModel : DialogViewModelBase
{
    public FileManager FileManager { get; private set; } = new FileManager();


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

