using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using SimpleMediaViewer.Wpf.Services;

namespace SimpleMediaViewer.Wpf;

/// <summary>
/// MainWindowのViewModel
/// </summary>
public class MainWindowViewModel : INotifyPropertyChanged, IDisposable
{
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

    public event PropertyChangedEventHandler? PropertyChanged;

    // コマンド
    public ICommand FitCommand { get; }
    public ICommand OriginalCommand { get; }
    public ICommand PlayCommand { get; }
    public ICommand StopCommand { get; }
    public ICommand DropCommand { get; }

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

    public MainWindowViewModel()
    {
        _mediaPlayerService = new MediaPlayerService();
        _webPPlayerService = new WebPPlayerService();
        _gifPlayerService = new GifPlayerService();

        // WebPフレーム変更イベント
        _webPPlayerService.FrameChanged += (s, e) => ImageSource = e;

        // GIFフレーム変更イベント
        _gifPlayerService.FrameChanged += (s, e) => ImageSource = e;

        // コマンドの初期化
        FitCommand = new RelayCommand(ExecuteFit);
        OriginalCommand = new RelayCommand(ExecuteOriginal);
        PlayCommand = new RelayCommand(ExecutePlay);
        StopCommand = new RelayCommand(ExecuteStop);
        DropCommand = new RelayCommand<string>(async (path) => await ExecuteDropAsync(path));
    }

    private void ExecuteFit()
    {
        ImageStretch = Stretch.Uniform;
        VideoStretch = Stretch.Uniform;
    }

    private void ExecuteOriginal()
    {
        ImageStretch = Stretch.None;
        VideoStretch = Stretch.None;
    }

    private void ExecutePlay()
    {
        if (ImageViewVisibility == Visibility.Visible)
        {
            if (_isWebPPlaying && !_webPPlayerService.IsPlaying)
            {
                _webPPlayerService.Resume();
            }
            else if (_isGifPlaying && !_gifPlayerService.IsPlaying)
            {
                _gifPlayerService.Resume();
            }
        }
        else
        {
            _mediaPlayerService.Play();
        }
    }

    private void ExecuteStop()
    {
        _webPPlayerService.Stop();
        _gifPlayerService.Stop();
        _mediaPlayerService.Stop();
    }

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
        catch (Exception ex)
        {
            MessageBox.Show($"GIF読み込みエラー: {ex.Message}", "エラー", MessageBoxButton.OK, MessageBoxImage.Error);
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

    protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }

    public void Dispose()
    {
        _webPPlayerService?.Dispose();
        _gifPlayerService?.Dispose();
        _mediaPlayerService?.Dispose();
    }
}
