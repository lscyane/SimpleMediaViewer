using CommunityToolkit.Mvvm.ComponentModel;
using LibVLCSharp.Shared;
using lscyane.Core.Mvvm;
using SimpleMediaViewer.Wpf.Services;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace SimpleMediaViewer.Wpf;


public interface IMainWindowViewModel;


/// <summary>
/// MainWindowのViewModel
/// </summary>
public class MainWindowViewModel : ObservableObject, IMainWindowViewModel, IDisposable
{
    private readonly IDialogService _DialogService;
    public FileManager FileManager { get; private set; } = new FileManager();

    private readonly MediaPlayerService _mediaPlayerService;
    private readonly WebPPlayerService _webPPlayerService;
    private readonly GifPlayerService _gifPlayerService;
    private string? _currentFilePath;
    private Visibility _videoViewVisibility = Visibility.Collapsed;
    private Visibility _imageViewVisibility = Visibility.Visible;
    private Stretch _imageStretch = Stretch.Uniform;
    private Stretch _videoStretch = Stretch.Uniform;
    private BitmapSource? _imageSource;
    private bool _isGifPlaying = false;
    private bool _isWebPPlaying = false;


    /// <summary>
    /// コンストラクタ
    /// </summary>
    /// <param name="dialogService"></param>
    public MainWindowViewModel(IDialogService dialogService)
    {
        this._DialogService = dialogService;

        _mediaPlayerService = new MediaPlayerService();
        _webPPlayerService = new WebPPlayerService();
        _gifPlayerService = new GifPlayerService();

        // WebPフレーム変更イベント
        _webPPlayerService.FrameChanged += (s, e) => ImageSource = e;

        // GIFフレーム変更イベント
        _gifPlayerService.FrameChanged += (s, e) => ImageSource = e;
    }


    #region Window位置・サイズ関係プロパティ
    public double WindowLeft
    {
        get => Properties.EnvSettings.Default.WindowLeft;
        set => Properties.EnvSettings.Default.WindowLeft = value;
    }

    public double WindowTop
    {
        get => Properties.EnvSettings.Default.WindowTop;
        set => Properties.EnvSettings.Default.WindowTop = value;
    }

    public double WindowWidth
    {
        get => Properties.EnvSettings.Default.WindowWidth;
        set => Properties.EnvSettings.Default.WindowWidth = value;
    }

    public double WindowHeight
    {
        get => Properties.EnvSettings.Default.WindowHeight;
        set => Properties.EnvSettings.Default.WindowHeight = value;
    }
    #endregion

    // プロパティ
    public MediaPlayerService MediaPlayerService => _mediaPlayerService;

    public Visibility VideoViewVisibility
    {
        get => _videoViewVisibility;
        set => SetProperty(ref _videoViewVisibility, value);
    }

    public Visibility ImageViewVisibility
    {
        get => _imageViewVisibility;
        set => SetProperty(ref _imageViewVisibility, value);
    }

    public Stretch ImageStretch
    {
        get => _imageStretch;
        set => SetProperty(ref _imageStretch, value);
    }

    public Stretch VideoStretch
    {
        get => _videoStretch;
        set => SetProperty(ref _videoStretch, value);
    }

    public BitmapSource? ImageSource
    {
        get => _imageSource;
        set => SetProperty(ref _imageSource, value);
    }


    // コマンド
    public RelayCommand FitCommand => field ??= new RelayCommand(() =>
    {
        ImageStretch = Stretch.Uniform;
        VideoStretch = Stretch.Uniform;
    });

    public RelayCommand OriginalCommand => field ??= new RelayCommand(() =>
    {
        ImageStretch = Stretch.None;
        VideoStretch = Stretch.None;
    });

    public RelayCommand PlayPauseCommand => field ??= new RelayCommand(() =>
    {
        if (ImageViewVisibility == Visibility.Visible)
        {
            // 画像/アニメーションの場合
            if (_isWebPPlaying)
            {
                if (_webPPlayerService.IsPlaying)
                {
                    _webPPlayerService.Stop();
                }
                else
                {
                    _webPPlayerService.Resume();
                }
            }
            else if (_isGifPlaying)
            {
                if (_gifPlayerService.IsPlaying)
                {
                    _gifPlayerService.Stop();
                }
                else
                {
                    _gifPlayerService.Resume();
                }
            }
        }
        else
        {
            // 動画の場合
            var state = _mediaPlayerService.Player.State;
            if (state == LibVLCSharp.Shared.VLCState.Playing)
            {
                _mediaPlayerService.Player.Pause();
            }
            else if (state == LibVLCSharp.Shared.VLCState.Paused || state == LibVLCSharp.Shared.VLCState.Stopped)
            {
                _mediaPlayerService.Play();
            }
        }
    });

    public RelayCommand<string> DropCommand => field ??= new RelayCommand<string>(async path =>
    {
        await ExecuteDropAsync(path);
    });

    public RelayCommand NextFileCommand => field ??= new RelayCommand(async () =>
    {
        var nextFile = FileManager.GetNextFile();
        if (!string.IsNullOrEmpty(nextFile))
        {
            await ExecuteDropAsync(nextFile);
        }
    });

    public RelayCommand PreviousFileCommand => field ??= new RelayCommand(async () =>
    {
        var previousFile = FileManager.GetPreviousFile();
        if (!string.IsNullOrEmpty(previousFile))
        {
            await ExecuteDropAsync(previousFile);
        }
    });


    public async Task ExecuteDropAsync(string? path)
    {
        if (path == null || !File.Exists(path))
        {
            return;
        }

        FileManager.CurrentFilePath = path;

        // 既存再生停止
        _webPPlayerService.Stop();
        _gifPlayerService.Stop();
        _mediaPlayerService.Stop();

        // フラグをリセット
        _isWebPPlaying = false;
        _isGifPlaying = false;

        _currentFilePath = path;

        // ファイル拡張子チェック
        var extension = Path.GetExtension(path).ToLowerInvariant();
        if (extension == ".webp")
        {
            await LoadWebPAsync(path);
        }
        else if ((extension == ".gif") || (extension == ".png"))
        {
            await LoadGifAsync(path);
        }
        else if (IsImageFile(extension))
        {
            LoadStaticImage(path);
        }
        else
        {
            LoadVideo(path);
        }
    }


    private async Task LoadWebPAsync(string path)
    {
        try
        {
            await _webPPlayerService.LoadAsync(path);

            // ビューの表示切り替え
            VideoViewVisibility = Visibility.Collapsed;
            ImageViewVisibility = Visibility.Visible;

            // アニメーションフレームが複数ある場合は自動再生
            if (_webPPlayerService.FrameCount > 1)
            {
                _isWebPPlaying = true;
                _webPPlayerService.Play(loop: true);
            }
            else
            {
                // 静止画WebPの場合は最初のフレームを表示
                ImageSource = _webPPlayerService.FirstFrame;
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show($"WebP読み込みエラー: {ex.Message}", "エラー", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }


    private async Task LoadGifAsync(string path)
    {
        try
        {
            await _gifPlayerService.LoadAsync(path);

            // ビューの表示切り替え
            VideoViewVisibility = Visibility.Collapsed;
            ImageViewVisibility = Visibility.Visible;

            // アニメーションフレームが複数ある場合は自動再生
            if (_gifPlayerService.FrameCount > 1)
            {
                _isGifPlaying = true;
                _gifPlayerService.Play(loop: true);
            }
        }
        catch (OperationCanceledException)
        {
            // 読み込みがキャンセルされた場合は無視
        }
        catch (Exception ex)
        {
            MessageBox.Show($"GIF/PNG読み込みエラー: {ex.Message}", "エラー", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }


    private void LoadStaticImage(string path)
    {
        try
        {
            var bmp = new BitmapImage();
            bmp.BeginInit();
            bmp.UriSource = new Uri(path);
            bmp.CacheOption = BitmapCacheOption.OnLoad;
            bmp.EndInit();
            bmp.Freeze();

            ImageSource = bmp;

            // ビューの表示切り替え
            VideoViewVisibility = Visibility.Collapsed;
            ImageViewVisibility = Visibility.Visible;
        }
        catch (Exception ex)
        {
            MessageBox.Show($"画像読み込みエラー: {ex.Message}", "エラー", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }


    private void LoadVideo(string path)
    {
        try
        {
            // ビューの表示切り替え
            ImageViewVisibility = Visibility.Collapsed;
            VideoViewVisibility = Visibility.Visible;

            _mediaPlayerService.Play(path);
        }
        catch (Exception ex)
        {
            MessageBox.Show($"動画読み込みエラー: {ex.Message}", "エラー", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }


    private bool IsImageFile(string extension)
    {
        var imageExtensions = new[] { ".jpg", ".jpeg", ".png", ".bmp", ".tiff" };
        return imageExtensions.Contains(extension);
    }


    public void Dispose()
    {
        _webPPlayerService?.Dispose();
        _gifPlayerService?.Dispose();
        _mediaPlayerService?.Dispose();
    }


    public RelayCommand ShowListDialogCommand => field ??= new RelayCommand(() =>
    {
        if (this.isShowingListDialog) return;

        // FileListDialogを表示
        this.isShowingListDialog = true;
        var dialog = new DialogParameters();
        dialog.Add("Title", "FileList");
        dialog.Add("Manager", FileManager);
        dialog.Add("OnFileSelected", new Action<string>(async (selectedFile) =>
        {
            await ExecuteDropAsync(selectedFile);
        }));
        this._DialogService.Show(typeof(FileListDialogViewModel), dialog, (result, param) =>
        {
            this.isShowingListDialog = false;
        });
    });
    private bool isShowingListDialog = false;

    public RelayCommand<object> SetSortOrderCommand => field ??= new RelayCommand<object>(param =>
    {
        if (param != null && int.TryParse(param.ToString(), out int sortOrderValue))
        {
            if (Enum.IsDefined(typeof(FileSortOrder), sortOrderValue))
            {
                FileManager.SortOrder = (FileSortOrder)sortOrderValue;
            }
        }
    });

}
