//
// NoteModel.h
//
// .obj / .mtl を読み込んでBasicEffectで描画するだけの軽量クラス。
// DirectX::Model::Draw(context, states, world, view, proj) と同じ
// シグネチャの Draw() を持つので、既存の
//     m_noteModel->Draw(context, *m_states, world, m_view, m_proj);
// をそのまま使い回せる。
//
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <wrl/client.h>
#include <d3d11.h>

#include <CommonStates.h>
#include <Effects.h>
#include <VertexTypes.h>
#include <SimpleMath.h>

class NoteModel
{
public:
    // objPath      : OBJファイルのパス（例: L"asset/Model/Dai/Daiza.obj"）
    // textureDir   : mtl内のテクスチャファイル名を探すフォルダ（省略時はobjと同じフォルダ）
    bool Load(ID3D11Device* device, const std::wstring& objPath,
        const std::wstring& textureDir = L"");

    // DirectX::Model::Draw と同じ引数構成
    void Draw(ID3D11DeviceContext* context, DirectX::CommonStates& states,
        const DirectX::SimpleMath::Matrix& world,
        const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& proj,
        bool wireframe = false);

    // レーンカラーなどでティントしたい場合に使う（未設定なら白＝ティントなし）
    void SetTintColor(const DirectX::SimpleMath::Vector3& color) { m_tintColor = color; }

    bool IsLoaded() const { return m_vertexCount > 0; }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_texture;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>         m_inputLayout;
    std::unique_ptr<DirectX::BasicEffect>             m_effect;
    UINT m_vertexCount = 0;
    DirectX::SimpleMath::Vector3 m_tintColor = { 1.0f, 1.0f, 1.0f };
};
