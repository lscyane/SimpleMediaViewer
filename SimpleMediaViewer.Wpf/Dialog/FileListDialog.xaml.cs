using System.Windows;
using System.Windows.Input;

namespace SimpleMediaViewer.Wpf.Dialog;

/// <summary>
/// FileListDialog.xaml の相互作用ロジック
/// </summary>
public partial class FileListDialog : Window
{
    public FileListDialog()
    {
        this.DataContext = new FileListDialogViewModel();
        InitializeComponent();
    }
}

