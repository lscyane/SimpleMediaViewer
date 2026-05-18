using LibVLCSharp.Shared;
using lscyane.Wpf.Services;
using System.IO;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media.Animation;

namespace SimpleMediaViewer.Wpf
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        private readonly MainWindowViewModel _viewModel;


        /// <summary>
        /// コンストラクタ
        /// </summary>
        public MainWindow()
        {
            // ダイアログサービスの設定
            lscyane.Wpf.Services.DialogService.Main = new lscyane.Wpf.Services.DialogService(this);
            lscyane.Wpf.Services.DialogService.Main.RegisterDialog<Dialog.FileListDialog, FileListDialogViewModel>();

            InitializeComponent();
            LibVLCSharp.Shared.Core.Initialize();

            this._viewModel = new MainWindowViewModel(DialogService.Main);
            this.DataContext = _viewModel;

            // VideoViewとMediaPlayerの接続
            VideoView.MediaPlayer = _viewModel.MediaPlayerService.Player;

            // モーションフォト動画終了 → フェードイン
            _viewModel.MotionPhotoVideoEnded += OnMotionPhotoVideoEnded;

            // 通常画像表示切り替え時は Opacity を即座に 1 にする
            _viewModel.PropertyChanged += (s, e) =>
            {
                if (e.PropertyName == nameof(MainWindowViewModel.ImageViewVisibility))
                {
                    if (_viewModel.ImageViewVisibility == Visibility.Visible)
                    {
                        // モーションフォトのフェード中は触らない
                        if (!_isFading)
                            ImageViewbox.Opacity = 1.0;
                    }
                }
            };
        }


        protected override void OnClosed(EventArgs e)
        {
            base.OnClosed(e);
            _viewModel?.Dispose();
        }

        private bool _isFading = false;

        private void OnMotionPhotoVideoEnded(object? sender, EventArgs e)
        {
            // 動画を止めて画像ビューに切り替え → フェードイン
            _viewModel.VideoViewVisibility = Visibility.Collapsed;
            _viewModel.ImageViewVisibility = Visibility.Visible;
            ImageViewbox.Opacity = 0.0;

            _isFading = true;
            var anim = new DoubleAnimation(0.0, 1.0, new Duration(TimeSpan.FromMilliseconds(800)))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            };
            anim.Completed += (_, _) =>
            {
                _isFading = false;
                _viewModel.MediaPlayerService.Stop();
            };
            ImageViewbox.BeginAnimation(UIElement.OpacityProperty, anim);
        }


        /// <summary>
        /// 初期ファイルを読み込む
        /// </summary>
        /// <param name="filePath">ファイルパス</param>
        public async Task LoadInitialFileAsync(string? filePath)
        {
            if (!string.IsNullOrEmpty(filePath) && File.Exists(filePath))
            {
                await _viewModel.ExecuteDropAsync(filePath);
            }
        }


        // D&D中のカーソル制御
        private void Window_DragOver(object sender, DragEventArgs e)
        {
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

            // ウィンドウをアクティブ化
            Activate();
            if (WindowState == WindowState.Minimized)
            {
                WindowState = WindowState.Normal;
            }

            var path = ((string[])e.Data.GetData(DataFormats.FileDrop)!).FirstOrDefault();
            await _viewModel.ExecuteDropAsync(path);
        }


        // キーボード操作
        private void Window_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Right)
            {
                _viewModel.NextFileCommand.Execute(null);
                e.Handled = true;
            }
            else if (e.Key == Key.Left)
            {
                _viewModel.PreviousFileCommand.Execute(null);
                e.Handled = true;
            }
            else if (e.Key == Key.Space)
            {
                _viewModel.PlayPauseCommand.Execute(null);
                e.Handled = true;
            }
        }
    }
}
