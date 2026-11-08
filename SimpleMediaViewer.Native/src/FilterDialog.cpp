#include "FilterDialog.h"
#include "CONST.hpp"
#include "FileManager.h"
#include "resource.h"

#include <vector>

namespace
{
	struct FilterDialogState
	{
		FileManager* fileManager;
	};


	/// <summary>
	/// 拡張子フィルターダイアログの入力を処理します。
	/// </summary>
	INT_PTR CALLBACK FilterDialogProcedure(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
	{
		FilterDialogState* state = reinterpret_cast<FilterDialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
		switch (message)
		{
			// WM_INITDIALOG メッセージでダイアログの初期化を行います。
			case WM_INITDIALOG:
			{
				state = reinterpret_cast<FilterDialogState*>(lParam);
				SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
				const size_t rowCount = (SUPPORTED_EXTENSIONS.size() + 1) / 2;
				const HFONT dialogFont = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
				for (size_t index = 0; index < SUPPORTED_EXTENSIONS.size(); ++index)
				{
					const size_t column = index / rowCount;
					const size_t row = index % rowCount;
					RECT bounds{
						8 + static_cast<LONG>(column) * 138,
						44 + static_cast<LONG>(row) * 15,
						143 + static_cast<LONG>(column) * 138,
						58 + static_cast<LONG>(row) * 15
					};
					MapDialogRect(dialog, &bounds);
					const int controlId = IDC_FILTER_EXTENSION_BASE + static_cast<int>(index);
					HWND checkBox = CreateWindowExW(
						0,
						L"BUTTON",
						SUPPORTED_EXTENSIONS[index].c_str(),
						WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
						bounds.left,
						bounds.top,
						bounds.right - bounds.left,
						bounds.bottom - bounds.top,
						dialog,
						reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
						GetModuleHandleW(nullptr),
						nullptr);
					if (checkBox == nullptr)
					{
						EndDialog(dialog, IDCANCEL);
						return FALSE;
					}

					SendMessageW(checkBox, WM_SETFONT, reinterpret_cast<WPARAM>(dialogFont), TRUE);
					SendMessageW(checkBox, BM_SETCHECK,
						state->fileManager->IsExtensionSelected(SUPPORTED_EXTENSIONS[index]) ? BST_CHECKED : BST_UNCHECKED,
						0);
				}
				return TRUE;
			}

			// WM_COMMAND メッセージでボタンのクリックなどの入力を処理します。
			case WM_COMMAND:
				switch (LOWORD(wParam))
				{
					// 「すべて選択」「すべて解除」「OK」「キャンセル」ボタンのクリックを処理します。
					case IDC_FILTER_SELECT_ALL:
					case IDC_FILTER_CLEAR_ALL:
					{
						const LRESULT checkState = LOWORD(wParam) == IDC_FILTER_SELECT_ALL ? BST_CHECKED : BST_UNCHECKED;
						for (size_t index = 0; index < SUPPORTED_EXTENSIONS.size(); ++index)
						{
							SendDlgItemMessageW(dialog, IDC_FILTER_EXTENSION_BASE + static_cast<int>(index), BM_SETCHECK, checkState, 0);
						}
						return TRUE;
					}

					// 「OK」「キャンセル」ボタンのクリックを処理します。
					case IDOK:
					{
						std::vector<std::wstring> selectedExtensions;
						for (size_t index = 0; index < SUPPORTED_EXTENSIONS.size(); ++index)
						{
							const int controlId = IDC_FILTER_EXTENSION_BASE + static_cast<int>(index);
							if (IsDlgButtonChecked(dialog, controlId) == BST_CHECKED)
							{
								selectedExtensions.push_back(SUPPORTED_EXTENSIONS[index]);
							}
						}
						state->fileManager->SetSelectedExtensions(selectedExtensions);
						EndDialog(dialog, IDOK);
						return TRUE;
					}

					// 「キャンセル」ボタンのクリックを処理します。
					case IDCANCEL:
						EndDialog(dialog, IDCANCEL);
						return TRUE;
				}
				break;

			// WM_CLOSE メッセージでダイアログを閉じます。
			case WM_CLOSE:
				EndDialog(dialog, IDCANCEL);
				return TRUE;
		}

		return FALSE;
	}
}


/// <summary>
/// 拡張子フィルターダイアログを表示します。
/// </summary>
bool FilterDialog::Show(HWND owner, FileManager& fileManager)
{
	FilterDialogState state{ &fileManager };
	return DialogBoxParamW(
		GetModuleHandleW(nullptr),
		MAKEINTRESOURCEW(IDD_FILTER_DIALOG),
		owner,
		FilterDialogProcedure,
		reinterpret_cast<LPARAM>(&state)) == IDOK;
}
