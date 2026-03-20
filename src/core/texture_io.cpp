#include "texture_io.h"

#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>
#include <GL/gl.h>

#include <fstream>
#include <cstring>
#include <algorithm>
#include "mesh_io.h"  // WriteU16BE, ReadU16BE
#include "meta_io.h"  // NebulaAssets::LoadNebTexSaturnNpotMode

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

bool LoadImageWIC(const wchar_t* path, UINT& outW, UINT& outH, std::vector<unsigned char>& outPixelsBGRA)
{
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hr;

    auto Cleanup = [&]()
    {
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (factory) factory->Release();
    };

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))))
        return false;

    if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder)))
    {
        Cleanup();
        return false;
    }

    if (FAILED(decoder->GetFrame(0, &frame)))
    {
        Cleanup();
        return false;
    }

    if (FAILED(factory->CreateFormatConverter(&converter)))
    {
        Cleanup();
        return false;
    }

    if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        Cleanup();
        return false;
    }

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    std::vector<unsigned char> pixels(w * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data())))
    {
        Cleanup();
        return false;
    }

    outW = w;
    outH = h;
    outPixelsBGRA = std::move(pixels);
    Cleanup();
    return true;
}

GLuint LoadTextureWIC(const wchar_t* path)
{
    UINT w = 0, h = 0;
    std::vector<unsigned char> pixels;
    if (!LoadImageWIC(path, w, h, pixels))
        return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels.data());

    return tex;
}

bool SaveVmuMonoPng(const std::filesystem::path& outPath, const std::array<uint8_t, 48 * 32>& mono)
{
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;

    auto Cleanup = [&]() {
        if (props) props->Release();
        if (frame) frame->Release();
        if (encoder) encoder->Release();
        if (stream) stream->Release();
        if (factory) factory->Release();
        if (SUCCEEDED(hr)) CoUninitialize();
    };

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
    {
        Cleanup();
        return false;
    }
    if (FAILED(factory->CreateStream(&stream)))
    {
        Cleanup();
        return false;
    }

    std::wstring wpath = outPath.wstring();
    if (FAILED(stream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE)))
    {
        Cleanup();
        return false;
    }
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)))
    {
        Cleanup();
        return false;
    }
    if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache)))
    {
        Cleanup();
        return false;
    }
    if (FAILED(encoder->CreateNewFrame(&frame, &props)))
    {
        Cleanup();
        return false;
    }
    if (FAILED(frame->Initialize(props)))
    {
        Cleanup();
        return false;
    }

    UINT w = 48, h = 32;
    if (FAILED(frame->SetSize(w, h)))
    {
        Cleanup();
        return false;
    }
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&fmt)))
    {
        Cleanup();
        return false;
    }

    std::vector<uint8_t> bgra((size_t)w * (size_t)h * 4u, 255u);
    for (UINT y = 0; y < h; ++y)
    {
        for (UINT x = 0; x < w; ++x)
        {
            const size_t pi = ((size_t)y * (size_t)w + (size_t)x);
            const uint8_t on = mono[pi] ? 0u : 255u;
            const size_t i = pi * 4u;
            bgra[i + 0] = on;
            bgra[i + 1] = on;
            bgra[i + 2] = on;
            bgra[i + 3] = 255u;
        }
    }

    if (FAILED(frame->WritePixels(h, w * 4u, (UINT)bgra.size(), bgra.data())))
    {
        Cleanup();
        return false;
    }
    if (FAILED(frame->Commit()) || FAILED(encoder->Commit()))
    {
        Cleanup();
        return false;
    }

    Cleanup();
    return true;
#else
    (void)outPath;
    (void)mono;
    return false;
#endif
}

bool ExportNebTexturePNG(const std::filesystem::path& pngPath, const std::filesystem::path& outPath, std::string& warning)
{
    UINT w = 0, h = 0;
    std::vector<unsigned char> pixels;
    std::wstring wpath = pngPath.wstring();
    if (!LoadImageWIC(wpath.c_str(), w, h, pixels))
        return false;

    std::ofstream out(outPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    const char magic[4] = { 'N','E','B','T' };
    uint16_t format = 1; // RGB555
    uint16_t flags = 0;
    out.write(magic, 4);
    WriteU16BE(out, (uint16_t)w);
    WriteU16BE(out, (uint16_t)h);
    WriteU16BE(out, format);
    WriteU16BE(out, flags);

    bool clampWarn = false;
    for (UINT i = 0; i < w * h; ++i)
    {
        uint8_t b = pixels[i * 4 + 0];
        uint8_t g = pixels[i * 4 + 1];
        uint8_t r = pixels[i * 4 + 2];
        uint16_t r5 = (uint16_t)(r >> 3);
        uint16_t g5 = (uint16_t)(g >> 3);
        uint16_t b5 = (uint16_t)(b >> 3);
        uint16_t rgb555 = (uint16_t)((r5 << 10) | (g5 << 5) | b5);
        WriteU16BE(out, rgb555);
    }

    if (clampWarn)
    {
        warning = "Color clamp";
    }
    return true;
}

// ---------------------------------------------------------------------------
// TGA conversion (merged from tga_convert.cpp)
// ---------------------------------------------------------------------------

static bool IsPowerOfTwoU16(uint16_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

static uint16_t NextPow2U16(uint16_t v)
{
    if (v <= 1) return 1;
    uint16_t p = 1;
    while (p < v && p < 32768) p = (uint16_t)(p << 1);
    return p;
}

static bool WriteTga24TopLeft(const std::filesystem::path& outPath, uint16_t w, uint16_t h, const std::vector<unsigned char>& bgr)
{
    std::ofstream out(outPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    unsigned char hdr[18] = {};
    hdr[2] = 2; // uncompressed true-color
    hdr[12] = (unsigned char)(w & 0xFF);
    hdr[13] = (unsigned char)((w >> 8) & 0xFF);
    hdr[14] = (unsigned char)(h & 0xFF);
    hdr[15] = (unsigned char)((h >> 8) & 0xFF);
    hdr[16] = 24;
    hdr[17] = 0x20; // top-left origin
    out.write((const char*)hdr, 18);
    out.write((const char*)bgr.data(), (std::streamsize)bgr.size());
    return true;
}

bool GetNebTexSaturnPadUvScale(const std::filesystem::path& nebtexPath, float& outU, float& outV)
{
    outU = 1.0f;
    outV = 1.0f;
    std::ifstream in(nebtexPath, std::ios::binary | std::ios::in);
    if (!in.is_open()) return false;

    char magic[4];
    if (!in.read(magic, 4)) return false;
    if (!(magic[0] == 'N' && magic[1] == 'E' && magic[2] == 'B' && magic[3] == 'T')) return false;

    uint16_t w = 0, h = 0, format = 0, flags = 0;
    if (!ReadU16BE(in, w) || !ReadU16BE(in, h) || !ReadU16BE(in, format) || !ReadU16BE(in, flags)) return false;
    if (w == 0 || h == 0) return false;

    if (NebulaAssets::LoadNebTexSaturnNpotMode(nebtexPath) != 0)
        return true; // resample mode keeps full normalized UV domain

    uint16_t outW = std::max<uint16_t>(8, NextPow2U16(w));
    uint16_t outH = std::max<uint16_t>(8, NextPow2U16(h));
    if (outW == 0 || outH == 0) return false;

    outU = (float)w / (float)outW;
    outV = (float)h / (float)outH;
    return true;
}

bool ConvertNebTexToTga24(const std::filesystem::path& nebtexPath, const std::filesystem::path& tgaOutPath, std::string& warn)
{
    std::ifstream in(nebtexPath, std::ios::binary | std::ios::in);
    if (!in.is_open()) return false;

    char magic[4];
    if (!in.read(magic, 4)) return false;
    if (!(magic[0] == 'N' && magic[1] == 'E' && magic[2] == 'B' && magic[3] == 'T')) return false;

    uint16_t w = 0, h = 0, format = 0, flags = 0;
    if (!ReadU16BE(in, w) || !ReadU16BE(in, h) || !ReadU16BE(in, format) || !ReadU16BE(in, flags)) return false;
    if (format != 1) return false;

    if (w > 256 || h > 256)
    {
        warn = "Saturn texture constraint warning: texture exceeds 256x256 and cannot be auto-padded safely.";
        return false;
    }

    std::vector<unsigned char> bgr((size_t)w * (size_t)h * 3);
    for (uint32_t i = 0; i < (uint32_t)w * (uint32_t)h; ++i)
    {
        uint16_t rgb;
        if (!ReadU16BE(in, rgb)) return false;
        uint8_t r5 = (rgb >> 10) & 0x1F;
        uint8_t g5 = (rgb >> 5) & 0x1F;
        uint8_t b5 = (rgb) & 0x1F;
        uint8_t r = (r5 << 3) | (r5 >> 2);
        uint8_t g = (g5 << 3) | (g5 >> 2);
        uint8_t b = (b5 << 3) | (b5 >> 2);
        bgr[i * 3 + 0] = b;
        bgr[i * 3 + 1] = g;
        bgr[i * 3 + 2] = r;
    }

    // Pad during packaging: enforce >=8, power-of-two, <=256.
    uint16_t outW = std::max<uint16_t>(8, NextPow2U16(w));
    uint16_t outH = std::max<uint16_t>(8, NextPow2U16(h));
    if (outW > 256 || outH > 256)
    {
        warn = "Saturn texture constraint warning: texture cannot be padded to safe size within 256x256.";
        return false;
    }

    if (outW != w || outH != h)
    {
        int npotMode = NebulaAssets::LoadNebTexSaturnNpotMode(nebtexPath); // 0=pad, 1=resample
        if (npotMode == 0)
        {
            std::vector<unsigned char> padded((size_t)outW * (size_t)outH * 3, 0);
            for (uint16_t y = 0; y < h; ++y)
            {
                const unsigned char* src = &bgr[(size_t)y * (size_t)w * 3];
                unsigned char* dst = &padded[(size_t)y * (size_t)outW * 3];
                memcpy(dst, src, (size_t)w * 3);
            }
            bgr.swap(padded);
            if (!warn.empty()) warn += " | ";
            warn += "Auto-padded texture: " + std::to_string(w) + "x" + std::to_string(h) + " -> " + std::to_string(outW) + "x" + std::to_string(outH);
        }
        else
        {
            std::vector<unsigned char> resized((size_t)outW * (size_t)outH * 3, 0);
            for (uint16_t y = 0; y < outH; ++y)
            {
                uint16_t sy = (uint16_t)((uint32_t)y * (uint32_t)h / (uint32_t)outH);
                if (sy >= h) sy = (uint16_t)(h - 1);
                for (uint16_t x = 0; x < outW; ++x)
                {
                    uint16_t sx = (uint16_t)((uint32_t)x * (uint32_t)w / (uint32_t)outW);
                    if (sx >= w) sx = (uint16_t)(w - 1);
                    const unsigned char* src = &bgr[((size_t)sy * (size_t)w + (size_t)sx) * 3];
                    unsigned char* dst = &resized[((size_t)y * (size_t)outW + (size_t)x) * 3];
                    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
                }
            }
            bgr.swap(resized);
            if (!warn.empty()) warn += " | ";
            warn += "Auto-resized texture: " + std::to_string(w) + "x" + std::to_string(h) + " -> " + std::to_string(outW) + "x" + std::to_string(outH);
        }
    }

    return WriteTga24TopLeft(tgaOutPath, outW, outH, bgr);
}

bool ConvertTgaToJoSafeTga24(const std::filesystem::path& tgaInPath, const std::filesystem::path& tgaOutPath, std::string& warn)
{
    std::ifstream in(tgaInPath, std::ios::binary | std::ios::in);
    if (!in.is_open()) return false;

    unsigned char hdr[18] = {};
    if (!in.read((char*)hdr, 18)) return false;
    const uint8_t idLen = hdr[0];
    const uint8_t imageType = hdr[2];
    const uint16_t w = (uint16_t)hdr[12] | ((uint16_t)hdr[13] << 8);
    const uint16_t h = (uint16_t)hdr[14] | ((uint16_t)hdr[15] << 8);
    const uint8_t bpp = hdr[16];
    const uint8_t desc = hdr[17];

    if (imageType != 2 || (bpp != 24 && bpp != 32))
    {
        warn = "Texture constraint warning: only uncompressed 24/32-bit TGA is supported for staging.";
        return false;
    }
    if (w == 0 || h == 0 || w > 256 || h > 256)
    {
        warn = "Texture constraint warning: TGA dimensions must be within 1..256 for staging.";
        return false;
    }

    if (idLen > 0) in.seekg(idLen, std::ios::cur);

    const size_t srcStride = (size_t)(bpp / 8) * (size_t)w;
    std::vector<unsigned char> src((size_t)h * srcStride);
    if (!in.read((char*)src.data(), (std::streamsize)src.size())) return false;

    std::vector<unsigned char> bgr((size_t)w * (size_t)h * 3);
    const bool originTop = (desc & 0x20) != 0;
    for (uint16_t y = 0; y < h; ++y)
    {
        uint16_t sy = originTop ? y : (uint16_t)(h - 1 - y);
        const unsigned char* srow = &src[(size_t)sy * srcStride];
        unsigned char* drow = &bgr[(size_t)y * (size_t)w * 3];
        for (uint16_t x = 0; x < w; ++x)
        {
            drow[(size_t)x * 3 + 0] = srow[(size_t)x * (bpp / 8) + 0];
            drow[(size_t)x * 3 + 1] = srow[(size_t)x * (bpp / 8) + 1];
            drow[(size_t)x * 3 + 2] = srow[(size_t)x * (bpp / 8) + 2];
        }
    }

    uint16_t outW = std::max<uint16_t>(8, NextPow2U16(w));
    uint16_t outH = std::max<uint16_t>(8, NextPow2U16(h));
    if (outW > 256 || outH > 256)
    {
        warn = "Texture constraint warning: TGA cannot be padded to safe size within 256x256.";
        return false;
    }

    if (outW != w || outH != h)
    {
        std::vector<unsigned char> resized((size_t)outW * (size_t)outH * 3, 0);
        for (uint16_t y = 0; y < outH; ++y)
        {
            uint16_t sy = (uint16_t)((uint32_t)y * (uint32_t)h / (uint32_t)outH);
            if (sy >= h) sy = (uint16_t)(h - 1);
            for (uint16_t x = 0; x < outW; ++x)
            {
                uint16_t sx = (uint16_t)((uint32_t)x * (uint32_t)w / (uint32_t)outW);
                if (sx >= w) sx = (uint16_t)(w - 1);
                const unsigned char* src = &bgr[((size_t)sy * (size_t)w + (size_t)sx) * 3];
                unsigned char* dst = &resized[((size_t)y * (size_t)outW + (size_t)x) * 3];
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
            }
        }
        bgr.swap(resized);
        if (!warn.empty()) warn += " | ";
        warn += "Auto-resized TGA: " + std::to_string(w) + "x" + std::to_string(h) + " -> " + std::to_string(outW) + "x" + std::to_string(outH);
    }

    return WriteTga24TopLeft(tgaOutPath, outW, outH, bgr);
}
