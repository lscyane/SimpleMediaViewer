using LibVLCSharp.Shared;
using System.IO;
using System.Windows;
using System.Windows.Input;

namespace SimpleMediaViewer.Wpf
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        private readonly MainWindowViewModel _viewModel;

        public MainWindow()
        {
            InitializeComponent();
            LibVLCSharp.Shared.Core.Initialize();

            _viewModel = new MainWindowViewModel();
            DataContext = _viewModel;

            // VideoViewとMediaPlayerの接続
            VideoView.MediaPlayer = _viewModel.MediaPlayerService.Player;
        }

        protected override void OnClosed(EventArgs e)
        {
            base.OnClosed(e);
            _viewModel?.Dispose();
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
