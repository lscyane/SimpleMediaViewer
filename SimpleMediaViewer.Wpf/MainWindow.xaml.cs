using LibVLCSharp.Shared;
using System.IO;
using System.Windows;
using System.Windows.Media.Imaging;

namespace SimpleMediaViewer.Wpf
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        private LibVLCSharp.Shared.LibVLC _libVLC;
        private LibVLCSharp.Shared.MediaPlayer _mediaPlayer;
        private WebPAnimationPlayer? _webPPlayer;
        private string? _currentFilePath;

        public MainWindow()
        {
            this.DataContext = new Wpf.MainWindowViewModel();
            InitializeComponent();
            LibVLCSharp.Shared.Core.Initialize();

            // LibVLCとMediaPlayerの初期化
            _libVLC = new LibVLCSharp.Shared.LibVLC();
            _mediaPlayer = new LibVLCSharp.Shared.MediaPlayer(_libVLC);
            VideoView.MediaPlayer = _mediaPlayer;

            // メディア終了時のイベント
            _mediaPlayer.EndReached += MediaPlayer_EndReached;

            // WebPアニメーションプレーヤーの初期化
            _webPPlayer = new WebPAnimationPlayer();
            _webPPlayer.FrameChanged += WebPPlayer_FrameChanged;
        }

        protected override void OnClosed(EventArgs e)
        {
            base.OnClosed(e);

            // リソースの解放
            _webPPlayer?.Stop();
            _webPPlayer?.Dispose();
            _mediaPlayer?.Stop();
            _mediaPlayer?.Dispose();
            _libVLC?.Dispose();
        }

        private void WebPPlayer_FrameChanged(object? sender, BitmapSource e)
        {
            ImageView.Source = e;
        }

        // D&D中のカーソル制御
        private void Window_DragOver(object sender, DragEventArgs e)
        {
            // ファイルのみ受け付ける
            e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop)
                ? DragDropEffects.Copy
                : DragDropEffects.None;

            e.Handled = true;
        }

        // ドロップ時処理
        private async void Window_Drop(object sender, DragEventArgs e)
        {
            if (!e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                return;
            }

            // 最初の1ファイルのみ使用
            var path = ((string[])e.Data.GetData(DataFormats.FileDrop)!).FirstOrDefault();
            if (path == null || !File.Exists(path))
            {
                return;
            }

            // 既存再生停止
            _webPPlayer?.Stop();
            _mediaPlayer?.Stop();

            _currentFilePath = path;

            // ファイル拡張子チェック
            var extension = Path.GetExtension(path).ToLowerInvariant();
            if (extension == ".webp")
            {
                // WebPファイルの場合はImageSharpで処理
                await LoadWebPAsync(path);
            }
            else if (IsImageFile(extension))
            {
                // 静止画の場合
                LoadStaticImage(path);
            }
            else
            {
                // 動画の場合はLibVLCで処理
                LoadVideo(path);
            }
        }

        private async Task LoadWebPAsync(string path)
        {
            try
            {
                await _webPPlayer!.LoadAsync(path);

                // ビューの表示切り替え
                VideoViewbox.Visibility = Visibility.Collapsed;
                ImageViewbox.Visibility = Visibility.Visible;

                // アニメーションフレームが複数ある場合は自動再生
                if (_webPPlayer.FrameCount > 1)
                {
                    _webPPlayer.Play(loop: true);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"WebP読み込みエラー: {ex.Message}", "エラー", MessageBoxButton.OK, MessageBoxImage.Error);
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

                ImageView.Source = bmp;

                // ビューの表示切り替え
                VideoViewbox.Visibility = Visibility.Collapsed;
                ImageViewbox.Visibility = Visibility.Visible;
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
                var media = new LibVLCSharp.Shared.Media(_libVLC, new Uri(path));

                // ビューの表示切り替え
                ImageViewbox.Visibility = Visibility.Collapsed;
                VideoViewbox.Visibility = Visibility.Visible;

                _mediaPlayer?.Play(media);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"動画読み込みエラー: {ex.Message}", "エラー", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private bool IsImageFile(string extension)
        {
            var imageExtensions = new[] { ".jpg", ".jpeg", ".png", ".bmp", ".gif", ".tiff" };
            return imageExtensions.Contains(extension);
        }

        // ウィンドウいっぱいに表示（アスペクト比維持）
        private void Fit_Click(object sender, RoutedEventArgs e)
        {
            ImageViewbox.Stretch = System.Windows.Media.Stretch.Uniform;
            VideoViewbox.Stretch = System.Windows.Media.Stretch.Uniform;
        }

        // 等倍表示（1:1ピクセル）
        private void Original_Click(object sender, RoutedEventArgs e)
        {
            ImageViewbox.Stretch = System.Windows.Media.Stretch.None;
            VideoViewbox.Stretch = System.Windows.Media.Stretch.None;
        }

        // 再生
        private void Play_Click(object sender, RoutedEventArgs e)
        {
            if (_webPPlayer?.IsPlaying == false && ImageViewbox.Visibility == Visibility.Visible)
            {
                _webPPlayer?.Resume();
            }
            else
            {
                _mediaPlayer?.Play();
            }
        }

        // 停止
        private void Stop_Click(object sender, RoutedEventArgs e)
        {
            _webPPlayer?.Stop();
            _mediaPlayer?.Stop();
        }

        // 再生終了時に自動ループ
        private void MediaPlayer_EndReached(object? sender, EventArgs e)
        {
            Dispatcher.Invoke(() =>
            {
                _mediaPlayer?.Stop();
                _mediaPlayer?.Play();
            });
        }
    }
}
