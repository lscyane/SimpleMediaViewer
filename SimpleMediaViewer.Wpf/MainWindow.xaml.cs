using LibVLCSharp.Shared;
using System.IO;
using System.Windows;

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

            var path = ((string[])e.Data.GetData(DataFormats.FileDrop)!).FirstOrDefault();
            await _viewModel.ExecuteDropAsync(path);
        }
    }
}
