#include "common.hlsl"

[maxvertexcount(6)]
void main(line PS_IN input[2], inout TriangleStream<PS_IN> OutputStream)
{
    PS_IN output;
    float thickness = LineWidth; // 線の太さ、適宜調整

    // 線の方向を計算
    float2 dir = normalize(input[1].Position.xy - input[0].Position.xy);

    // 正規化された方向に直交するベクトルを計算（線の幅のため）
    float2 normal = float2(-dir.y, dir.x) * thickness;

    // 四角形の頂点を計算（2つの三角形を形成）
    float4 pos1 = input[0].Position + float4(normal, 0.0, 0.0);
    float4 pos2 = input[0].Position - float4(normal, 0.0, 0.0);
    float4 pos3 = input[1].Position + float4(normal, 0.0, 0.0);
    float4 pos4 = input[1].Position - float4(normal, 0.0, 0.0);

    // 最初の三角形
    output.Position = pos1;
    output.Diffuse = input[0].Diffuse;
    output.TexCoord.xy = 0.0f;
    OutputStream.Append(output);

    output.Position = pos2;
    output.Diffuse = input[0].Diffuse;
    output.TexCoord.xy = 0.0f;
    OutputStream.Append(output);
    
    output.Position = pos3;
    output.Diffuse = input[0].Diffuse;
    output.TexCoord.xy = 0.0f;
    OutputStream.Append(output);

    // 二番目の三角形
    output.Position = pos2;
    output.Diffuse = input[1].Diffuse;
    output.TexCoord.xy = 0.0f;
    OutputStream.Append(output);
    
    output.Position = pos4;
    output.Diffuse = input[1].Diffuse;
    output.TexCoord.xy = 0.0f;
    OutputStream.Append(output);

    output.Position = pos3;
    output.Diffuse = input[1].Diffuse;
    output.TexCoord.xy = 0.0f;
    OutputStream.Append(output);
    
    OutputStream.RestartStrip();
}