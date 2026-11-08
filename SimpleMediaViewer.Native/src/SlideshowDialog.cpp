#include "SlideshowDialog.h"
#include "resource.h"

#include <CommCtrl.h>

#include <algorithm>
#include <iterator>
#include <string>

#pragma comment(lib, "Comctl32.lib")

namespace
{
	constexpr int DEFAULT_INTERVAL_TENTHS = 20;
	constexpr int MIN_INTERVAL_TENTHS = 1;
	constexpr int MAX_INTERVAL_TENTHS = 999999;

	struct SlideshowDialogState
	{
		int intervalTenths = DEFAULT_INTERVAL_TENTHS;
	};


	std::wstring FormatInterval(int intervalTenths)
	{
		return std::to_wstring(intervalTenths / 10) + L"." + std::to_wstring(intervalTenths % 10);
	}


	bool TryParseInterval(const std::wstring& text, int& intervalTenths)
	{
		if (text.empty())
		{
			return false;
		}

		const size_t decimalPoint = text.find(L'.');
		const size_t wholeLength = decimalPoint == std::wstring::npos ? text.length() : decimalPoint;
		if (wholeLength == 0 || wholeLength > 5)
		{
			return false;
		}
		for (size_t index = 0; index < wholeLength; ++index)
		{
			if (text[index] < L'0' || text[index] > L'9')
			{
				return false;
			}
		}
		if (decimalPoint != std::wstring::npos
			&& (decimalPoint + 2 != text.length() || text[decimalPoint + 1] < L'0' || text[decimalPoint + 1] > L'9'))
		{
			return false;
		}

		int wholeSeconds = 0;
		for (size_t index = 0; index < wholeLength; ++index)
		{
			wholeSeconds = wholeSeconds * 10 + (text[index] - L'0');
		}
		const int fractionalTenths = decimalPoint == std::wstring::npos ? 0 : text[decimalPoint + 1] - L'0';
		intervalTenths = wholeSeconds * 10 + fractionalTenths;
		return intervalTenths >= MIN_INTERVAL_TENTHS && intervalTenths <= MAX_INTERVAL_TENTHS;
	}


	INT_PTR CALLBACK SlideshowDialogProcedure(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
	{
		SlideshowDialogState* state = reinterpret_cast<SlideshowDialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
		switch (message)
		{
		case WM_INITDIALOG:
		{
			state = reinterpret_cast<SlideshowDialogState*>(lParam);
			SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
			HWND spin = GetDlgItem(dialog, IDC_SLIDESHOW_SPIN);
			SendMessageW(spin, UDM_SETRANGE32, MIN_INTERVAL_TENTHS, MAX_INTERVAL_TENTHS);
			SendMessageW(spin, UDM_SETPOS32, 0, DEFAULT_INTERVAL_TENTHS);
			SetDlgItemTextW(dialog, IDC_SLIDESHOW_INTERVAL, L"2.0");
			return TRUE;
		}

		case WM_NOTIFY:
		{
			const NMHDR* notification = reinterpret_cast<const NMHDR*>(lParam);
			if (notification->idFrom == IDC_SLIDESHOW_SPIN && notification->code == UDN_DELTAPOS)
			{
				const NMUPDOWN* spinNotification = reinterpret_cast<const NMUPDOWN*>(lParam);
				wchar_t text[32]{};
				GetDlgItemTextW(dialog, IDC_SLIDESHOW_INTERVAL, text, static_cast<int>(std::size(text)));
				int currentIntervalTenths = spinNotification->iPos;
				TryParseInterval(text, currentIntervalTenths);
				const int intervalTenths = std::clamp(currentIntervalTenths + spinNotification->iDelta,
					MIN_INTERVAL_TENTHS, MAX_INTERVAL_TENTHS);
				SendDlgItemMessageW(dialog, IDC_SLIDESHOW_SPIN, UDM_SETPOS32, 0, intervalTenths);
				SetDlgItemTextW(dialog, IDC_SLIDESHOW_INTERVAL, FormatInterval(intervalTenths).c_str());
				SetWindowLongPtrW(dialog, DWLP_MSGRESULT, TRUE);
				return TRUE;
			}
			break;
		}

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
			case IDOK:
			{
				wchar_t text[32]{};
				GetDlgItemTextW(dialog, IDC_SLIDESHOW_INTERVAL, text, static_cast<int>(std::size(text)));
				if (!TryParseInterval(text, state->intervalTenths))
				{
					MessageBoxW(dialog, L"0.1～99999.9秒の範囲で入力してください。", L"スライドショー", MB_OK | MB_ICONWARNING);
					SetFocus(GetDlgItem(dialog, IDC_SLIDESHOW_INTERVAL));
					return TRUE;
				}
				EndDialog(dialog, IDOK);
				return TRUE;
			}
			case IDCANCEL:
				EndDialog(dialog, IDCANCEL);
				return TRUE;
			}
			break;

		case WM_CLOSE:
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}

		return FALSE;
	}
}


std::optional<int> SlideshowDialog::Show(HWND owner)
{
	INITCOMMONCONTROLSEX commonControls{ sizeof(INITCOMMONCONTROLSEX), ICC_UPDOWN_CLASS };
	if (!InitCommonControlsEx(&commonControls))
	{
		return std::nullopt;
	}

	SlideshowDialogState state;
	const INT_PTR result = DialogBoxParamW(
		GetModuleHandleW(nullptr),
		MAKEINTRESOURCEW(IDD_SLIDESHOW_DIALOG),
		owner,
		SlideshowDialogProcedure,
		reinterpret_cast<LPARAM>(&state));
	return result == IDOK ? std::optional<int>(state.intervalTenths) : std::nullopt;
}
