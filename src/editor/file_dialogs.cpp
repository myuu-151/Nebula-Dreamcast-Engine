#include "file_dialogs.h"

#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#include <ShObjIdl.h>

#include <string>
#include <vector>
#include <filesystem>
#include <cstring>

std::string PickFolderDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::string result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        std::string utf8(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                        result = utf8;
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
#else
    return "";
#endif
}

std::string PickProjectFileDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::string result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        COMDLG_FILTERSPEC filters[] = { { L"Nebula Project", L"*.nebproj" } };
        pfd->SetFileTypes(1, filters);
        pfd->SetDefaultExtension(L"nebproj");

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        std::string utf8(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                        result = utf8;
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
#else
    return "";
#endif
}

std::string PickFbxFileDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::string result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        COMDLG_FILTERSPEC filters[] = {
            { L"FBX Files", L"*.fbx" },
            { L"All Files", L"*.*" }
        };
        pfd->SetFileTypes(2, filters);
        pfd->SetDefaultExtension(L"fbx");

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        std::string utf8(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                        result = utf8;
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
#else
    return "";
#endif
}

std::string PickPngFileDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::string result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        COMDLG_FILTERSPEC filters[] = {
            { L"PNG Files", L"*.png" },
            { L"All Files", L"*.*" }
        };
        pfd->SetFileTypes(2, filters);
        pfd->SetDefaultExtension(L"png");

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        std::string utf8(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                        result = utf8;
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
#else
    return "";
#endif
}

std::string PickVmuFrameDataDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::string result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        COMDLG_FILTERSPEC filters[] = {
            { L"VMU Frame Data", L"*.vmuanim" },
            { L"All Files", L"*.*" }
        };
        pfd->SetFileTypes(2, filters);
        pfd->SetDefaultExtension(L"vmuanim");

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item)) && item)
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0)
                    {
                        std::string utf8(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                        result = utf8;
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return result;
#else
    return "";
#endif
}

std::vector<std::string> PickImportAssetDialog(const char* title)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::vector<std::string> results;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd))))
    {
        DWORD options = 0;
        pfd->GetOptions(&options);
        pfd->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_ALLOWMULTISELECT);
        if (title) pfd->SetTitle(std::wstring(title, title + strlen(title)).c_str());

        COMDLG_FILTERSPEC filters[] = {
            { L"FBX/NEBANIM/VTXA/PNG", L"*.fbx;*.nebanim;*.vtxa;*.png" },
            { L"NEBANIM Files", L"*.nebanim" },
            { L"FBX Files", L"*.fbx" },
            { L"VTXA Files", L"*.vtxa" },
            { L"PNG Files", L"*.png" },
            { L"All Files", L"*.*" }
        };
        pfd->SetFileTypes(6, filters);
        pfd->SetDefaultExtension(L"fbx");

        if (SUCCEEDED(pfd->Show(nullptr)))
        {
            IFileOpenDialog* pOpen = nullptr;
            if (SUCCEEDED(pfd->QueryInterface(IID_PPV_ARGS(&pOpen))) && pOpen)
            {
                IShellItemArray* items = nullptr;
                if (SUCCEEDED(pOpen->GetResults(&items)) && items)
                {
                    DWORD count = 0;
                    items->GetCount(&count);
                    for (DWORD i = 0; i < count; ++i)
                    {
                        IShellItem* item = nullptr;
                        if (SUCCEEDED(items->GetItemAt(i, &item)) && item)
                        {
                            PWSTR path = nullptr;
                            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                            {
                                int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                                if (len > 0)
                                {
                                    std::string utf8(len - 1, '\0');
                                    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
                                    results.push_back(utf8);
                                }
                                CoTaskMemFree(path);
                            }
                            item->Release();
                        }
                    }
                    items->Release();
                }
                pOpen->Release();
            }
        }
        pfd->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    return results;
#else
    return {};
#endif
}
