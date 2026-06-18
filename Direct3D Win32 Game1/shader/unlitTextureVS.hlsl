#include "common.hlsl"

PS_IN main(in VS_IN In)
{
    PS_IN Out;
	
	matrix wvp;
	wvp = mul(World, View);
	wvp = mul(wvp, Projection);

	Out.Position = mul(In.Position, wvp);
	Out.TexCoord = In.TexCoord;
	Out.Diffuse = In.Diffuse * Material.Diffuse;
	
    return Out;
}

