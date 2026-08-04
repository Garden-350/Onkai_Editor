// FbxModel.cpp
#include "pch.h"
#include "FbxModel.h"
#include "WICTextureLoader.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>
#include <functional>

using namespace DirectX;
using namespace DirectX::SimpleMath;

namespace
{
    std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(len, 0);
        if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
        return s;
    }
    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(len, 0);
        if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
        return w;
    }
}

bool FbxModel::Load(ID3D11Device* device, const std::wstring& fbxPath, const std::wstring& textureDir)
{
    Assimp::Importer importer;
    std::string path = WideToUtf8(fbxPath);

    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_FlipUVs |
        aiProcess_ConvertToLeftHanded);

    if (!scene || !scene->HasMeshes())
    {
        OutputDebugStringW((L"[FbxModel] Load failed: " + fbxPath +
            L"  reason=" + Utf8ToWide(importer.GetErrorString()) + L"\n").c_str());
        return false;
    }

    std::vector<VertexPositionNormalTexture> vbData;

    // ── ノード階層を再帰的に辿り、各メッシュにワールド変換を適用してから頂点を集める ──
    std::function<void(aiNode*, const aiMatrix4x4&)> processNode =
        [&](aiNode* node, const aiMatrix4x4& parentTransform)
        {
            aiMatrix4x4 nodeTransform = parentTransform * node->mTransformation;

            for (unsigned int i = 0; i < node->mNumMeshes; i++)
            {
                aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

                // 法線変換用(移動成分を除いた行列)
                aiMatrix3x3 normalMat(nodeTransform);

                for (unsigned int f = 0; f < mesh->mNumFaces; f++)
                {
                    const aiFace& face = mesh->mFaces[f];
                    if (face.mNumIndices != 3) continue;

                    for (unsigned int idx = 0; idx < 3; idx++)
                    {
                        unsigned int vi = face.mIndices[idx];

                        aiVector3D p = nodeTransform * mesh->mVertices[vi]; // ← ワールド変換を適用

                        aiVector3D n = mesh->HasNormals() ? mesh->mNormals[vi] : aiVector3D(0, 1, 0);
                        n = normalMat * n; // ← 法線にも回転を適用
                        n.Normalize();

                        aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][vi] : aiVector3D(0, 0, 0);

                        vbData.push_back(VertexPositionNormalTexture(
                            Vector3(p.x, p.y, p.z),
                            Vector3(n.x, n.y, n.z),
                            Vector2(uv.x, uv.y)));
                    }
                }
            }

            for (unsigned int c = 0; c < node->mNumChildren; c++)
                processNode(node->mChildren[c], nodeTransform);
        };

    processNode(scene->mRootNode, aiMatrix4x4()); // ルートから開始(単位行列)

    if (vbData.empty())
    {
        OutputDebugStringW(L"[FbxModel] No vertices parsed from FBX\n");
        return false;
    }

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = (UINT)(sizeof(VertexPositionNormalTexture) * vbData.size());
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vbData.data();

    HRESULT hr = device->CreateBuffer(&vbDesc, &initData, m_vertexBuffer.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        OutputDebugStringW(L"[FbxModel] CreateBuffer failed\n");
        return false;
    }
    m_vertexCount = (UINT)vbData.size();

    // ── テクスチャ読み込み（最初のマテリアルのdiffuseテクスチャだけ拾う）──
    if (scene->HasMaterials() && scene->mNumMaterials > 0)
    {
        aiMaterial* mat = scene->mMaterials[scene->mMeshes[0]->mMaterialIndex];
        aiString texPath;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
        {
            std::filesystem::path p(texPath.C_Str());
            std::wstring texName = p.filename().wstring();
            std::wstring fullPath = textureDir + L"/" + texName;

            HRESULT thr = CreateWICTextureFromFile(device, fullPath.c_str(), nullptr,
                m_texture.ReleaseAndGetAddressOf());
            if (FAILED(thr))
                OutputDebugStringW((L"[FbxModel] Texture load failed: " + fullPath + L"\n").c_str());
        }
    }

    // ── BasicEffect ──（NoteModelと同じ）
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
        OutputDebugStringW(L"[FbxModel] CreateInputLayout failed\n");
        return false;
    }

    OutputDebugStringW((L"[FbxModel] Loaded OK: " + fbxPath +
        L"  verts=" + std::to_wstring(m_vertexCount) + L"\n").c_str());
    return true;
}

void FbxModel::Draw(ID3D11DeviceContext* context, DirectX::CommonStates& states,
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