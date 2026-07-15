//
// NoteModel.cpp
//
#include "pch.h"
#include "NoteModel.h"
#include "WICTextureLoader.h"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <filesystem>

using namespace DirectX;
using namespace DirectX::SimpleMath;
using Microsoft::WRL::ComPtr;

namespace
{
    // 三角形化済みの1頂点分のデータ
    struct RawVertex
    {
        Vector3 pos;
        Vector3 normal;
        Vector2 uv;
    };

    struct FaceIndex { int v = 0, vt = 0, vn = 0; };

    // "1/2/3" "1//3" "1/2" "1" のいずれの書式にも対応
    FaceIndex ParseFaceToken(const std::string& tok)
    {
        FaceIndex fi;
        size_t p1 = tok.find('/');
        if (p1 == std::string::npos) { fi.v = std::stoi(tok); return fi; }

        fi.v = std::stoi(tok.substr(0, p1));

        size_t p2 = tok.find('/', p1 + 1);
        if (p2 == std::string::npos)
        {
            std::string vt = tok.substr(p1 + 1);
            if (!vt.empty()) fi.vt = std::stoi(vt);
            return fi;
        }

        std::string vt = tok.substr(p1 + 1, p2 - p1 - 1);
        if (!vt.empty()) fi.vt = std::stoi(vt);

        std::string vn = tok.substr(p2 + 1);
        if (!vn.empty()) fi.vn = std::stoi(vn);
        return fi;
    }

    // OBJの負インデックス（末尾からの相対参照）を解決
    int ResolveIndex(int idx, size_t count)
    {
        if (idx > 0) return idx - 1;          // 1-based → 0-based
        if (idx < 0) return (int)count + idx; // 相対参照
        return -1;                              // 未指定
    }

    std::wstring GetDirectory(const std::wstring& path)
    {
        size_t pos = path.find_last_of(L"/\\");
        if (pos == std::wstring::npos) return L"";
        return path.substr(0, pos + 1);
    }

    std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(len, 0);
        if (len > 0)
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
        return s;
    }

    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(len, 0);
        if (len > 0)
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
        return w;
    }

    // .mtl ファイルから map_Kd （テクスチャファイル名）だけを抜き出す
    std::string ParseMtlForTexture(const std::string& mtlPath)
    {
        std::ifstream file(mtlPath);
        if (!file.is_open()) return {};

        std::string line;
        while (std::getline(file, line))
        {
            std::istringstream ss(line);
            std::string tag;
            ss >> tag;
            if (tag == "map_Kd")
            {
                std::string rest;
                std::getline(ss, rest);
                // 先頭の空白を除去
                size_t start = rest.find_first_not_of(" \t\r");
                if (start == std::string::npos) continue;
                std::string texName = rest.substr(start);
                // 末尾の\rなどを除去
                while (!texName.empty() && (texName.back() == '\r' || texName.back() == '\n'))
                    texName.pop_back();
                return texName;
            }
        }
        return {};
    }
}

bool NoteModel::Load(ID3D11Device* device, const std::wstring& objPath, const std::wstring& textureDir)
{
    std::ifstream file(objPath);
    if (!file.is_open())
    {
        OutputDebugStringW((L"[NoteModel] OBJ open failed: " + objPath + L"\n").c_str());
        return false;
    }

    std::vector<Vector3> positions;
    std::vector<Vector2> texcoords;
    std::vector<Vector3> normals;
    std::vector<RawVertex> outVertices; // 三角形化済み最終頂点列（インデックスバッファは使わず単純化）

    std::wstring objDir = GetDirectory(objPath);
    std::string mtlFileName;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "v")
        {
            float x, y, z;
            ss >> x >> y >> z;
            positions.emplace_back(x, y, z);
        }
        else if (tag == "vt")
        {
            float u, v;
            ss >> u >> v;
            texcoords.emplace_back(u, 1.0f - v); // OBJは左下原点 → DirectXは左上原点なのでVを反転
        }
        else if (tag == "vn")
        {
            float x, y, z;
            ss >> x >> y >> z;
            normals.emplace_back(x, y, z);
        }
        else if (tag == "mtllib")
        {
            std::string rest;
            std::getline(ss, rest);
            size_t start = rest.find_first_not_of(" \t\r");
            if (start != std::string::npos)
                mtlFileName = rest.substr(start);
        }
        else if (tag == "f")
        {
            std::vector<FaceIndex> face;
            std::string tok;
            while (ss >> tok)
                face.push_back(ParseFaceToken(tok));

            if (face.size() < 3) continue;

            auto MakeVertex = [&](const FaceIndex& fi) -> RawVertex
                {
                    RawVertex rv{};
                    int pi = ResolveIndex(fi.v, positions.size());
                    if (pi >= 0 && pi < (int)positions.size()) rv.pos = positions[pi];

                    if (fi.vn != 0)
                    {
                        int ni = ResolveIndex(fi.vn, normals.size());
                        if (ni >= 0 && ni < (int)normals.size()) rv.normal = normals[ni];
                    }
                    if (fi.vt != 0)
                    {
                        int ti = ResolveIndex(fi.vt, texcoords.size());
                        if (ti >= 0 && ti < (int)texcoords.size()) rv.uv = texcoords[ti];
                    }
                    return rv;
                };

            // ファン三角形分割：(0,1,2) (0,2,3) (0,3,4) ...
            for (size_t i = 1; i + 1 < face.size(); i++)
            {
                RawVertex a = MakeVertex(face[0]);
                RawVertex b = MakeVertex(face[i]);
                RawVertex c = MakeVertex(face[i + 1]);

                // 法線がOBJ側に無い場合は面法線を計算
                if (face[0].vn == 0)
                {
                    Vector3 n = (b.pos - a.pos).Cross(c.pos - a.pos);
                    n.Normalize();
                    a.normal = b.normal = c.normal = n;
                }

                outVertices.push_back(a);
                outVertices.push_back(b);
                outVertices.push_back(c);
            }
        }
    }

    if (outVertices.empty())
    {
        OutputDebugStringW(L"[NoteModel] No vertices parsed from OBJ\n");
        return false;
    }

    // ── VertexPositionNormalTexture 形式に詰め替え ──
    std::vector<VertexPositionNormalTexture> vbData;
    vbData.reserve(outVertices.size());
    for (auto& v : outVertices)
        vbData.push_back(VertexPositionNormalTexture(v.pos, v.normal, v.uv));

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = (UINT)(sizeof(VertexPositionNormalTexture) * vbData.size());
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vbData.data();

    HRESULT hr = device->CreateBuffer(&vbDesc, &initData, m_vertexBuffer.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[NoteModel] CreateBuffer failed\n");
        return false;
    }
    m_vertexCount = (UINT)vbData.size();

    // ── mtlを読んでテクスチャをロード ──
    if (!mtlFileName.empty())
    {
        std::wstring mtlPath = objDir + Utf8ToWide(mtlFileName);
        std::string texName = ParseMtlForTexture(WideToUtf8(mtlPath));
        if (!texName.empty())
        {
            std::wstring dir = textureDir.empty() ? objDir : textureDir;
            std::wstring texPath = dir + Utf8ToWide(texName);

            HRESULT thr = CreateWICTextureFromFile(device, texPath.c_str(), nullptr,
                m_texture.ReleaseAndGetAddressOf());
            if (FAILED(thr))
            {
                OutputDebugStringW((L"[NoteModel] Texture load failed: " + texPath + L"\n").c_str());
            }
        }
    }

    // ── BasicEffect（テクスチャ＋ライティング）を用意 ──
    m_effect = std::make_unique<BasicEffect>(device);
    m_effect->SetVertexColorEnabled(false);
    m_effect->SetLightingEnabled(true);
    m_effect->SetPerPixelLighting(true);
    m_effect->EnableDefaultLighting();
    if (m_texture)
    {
        m_effect->SetTextureEnabled(true);
        m_effect->SetTexture(m_texture.Get());
    }

    void const* shaderByteCode;
    size_t byteCodeLength;
    m_effect->GetVertexShaderBytecode(&shaderByteCode, &byteCodeLength);

    hr = device->CreateInputLayout(
        VertexPositionNormalTexture::InputElements,
        VertexPositionNormalTexture::InputElementCount,
        shaderByteCode, byteCodeLength,
        m_inputLayout.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[NoteModel] CreateInputLayout failed\n");
        return false;
    }

    OutputDebugStringW((L"[NoteModel] Loaded OK: " + objPath +
        L"  verts=" + std::to_wstring(m_vertexCount) + L"\n").c_str());
    return true;
}

void NoteModel::Draw(ID3D11DeviceContext* context, DirectX::CommonStates& states,
    const Matrix& world, const Matrix& view, const Matrix& proj, bool wireframe)
{
    if (!m_effect || m_vertexCount == 0) return;

    m_effect->SetWorld(world);
    m_effect->SetView(view);
    m_effect->SetProjection(proj);
    m_effect->SetDiffuseColor(m_tintColor);

    context->OMSetBlendState(states.Opaque(), nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(states.DepthDefault(), 0);
    context->RSSetState(wireframe ? states.Wireframe() : states.CullClockwise());

    m_effect->Apply(context);
    context->IASetInputLayout(m_inputLayout.Get());

    UINT stride = sizeof(VertexPositionNormalTexture);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->Draw(m_vertexCount, 0);
}
