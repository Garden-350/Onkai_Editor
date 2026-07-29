// FbxModel.h
#pragma once
#include <memory>
#include <string>
#include "SimpleMath.h"
#include "Effects.h"
#include "PrimitiveBatch.h"
#include "VertexTypes.h"
#include "CommonStates.h"

class FbxModel
{
public:
    bool Load(ID3D11Device* device, const std::wstring& fbxPath, const std::wstring& textureDir);
    void Draw(ID3D11DeviceContext* context, DirectX::CommonStates& states,
        const DirectX::SimpleMath::Matrix& world,
        const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& proj,
        bool wireframe = false);
    void SetTintColor(DirectX::SimpleMath::Vector3 c) { m_tintColor = c; }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_texture;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    std::unique_ptr<DirectX::BasicEffect> m_effect;
    UINT m_vertexCount = 0;
    DirectX::SimpleMath::Vector3 m_tintColor = { 1,1,1 };
};
