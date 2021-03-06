
// `basic vertex shader`
source `
struct a2v { float4 Position : POSITION; };
struct v2p { float4 Position : POSITION; };
float4x4 ModelViewMatrix;
void main( in a2v IN, out v2p OUT )
{
	OUT.Position = mul( IN.Position, ModelViewMatrix );
}`
compile_hlsl_before_after ``
compile_hlsl4 ``
compile_glsl ``
compile_glsl_es100 ``

// `partial position output`
source `float2 main(float2 p : POSITION) : POSITION { return p; }`
compile_fail ``

// `partial position output 2`
source `void main(float2 p : POSITION, out float2 op : POSITION){ op = p; }`
compile_fail ``

// `blur pixel shader`
source `
sampler2D texOCOL : register( s0 );
cbuffer core_data : register( b0 )
{
	float4x4 mWorld;
	float4x4 mViewProj;
	float4 viewportSizeInv;
	float4 PPData;
}
void main(float2 itex : TEXCOORD0, out float4 RT0 : COLOR)
{
	float2 hoff = PPData.zw;
	float3 ocol = tex2D(texOCOL, itex).rgb * 0.2270270270;
	ocol += tex2D(texOCOL, itex + hoff*1.3846153846).rgb * 0.3162162162;
	ocol += tex2D(texOCOL, itex + hoff*3.2307692308).rgb * 0.0702702703;
	ocol += tex2D(texOCOL, itex - hoff*1.3846153846).rgb * 0.3162162162;
	ocol += tex2D(texOCOL, itex - hoff*3.2307692308).rgb * 0.0702702703;
	RT0 = float4(ocol, 1);
}`
compile_hlsl_before_after `/T ps_3_0`
compile_hlsl4 `/T ps_4_0`
compile_glsl `-S frag`
compile_glsl_es100 `-S frag`

// `stipple transparency pixel shader`
source `
#define VCOL
#define MOD_BLENDCOLOR 0
float4 gInstanceData[32];
struct VS2PS_c
{
	float4 tex : TEXCOORD0;
	float4 col : COLOR0;
	float2 pos : TEXCOORD1;
};
sampler2D DiffuseTex;
static const float bayer[4][4] =
{
	{ 0, 8, 2, 10 },
	{ 12, 4, 14, 6 },
	{ 3, 11, 1, 9 },
	{ 15, 7, 13, 5 },
};
void main(VS2PS_c i, out float4 RT0 : COLOR0, out float4 RT1 : COLOR1)
{
	float4 diff = tex2D(DiffuseTex, i.tex.xy);
#if defined(VCOL)
	diff *= i.col;
#endif

	// work only on top 0.5-1.0 part of alpha, otherwise stands out too much
	clip(diff.a * 32 - 16 - bayer[i.pos.x % 4][i.pos.y % 4]);
	diff.rgb *= diff.a;
	diff.a = 1;

	RT0 = diff;
	RT1 = float4(0, 0, 0.5, diff.a);

	// POST-LIGHT modifier addons
#ifdef MOD_BLENDCOLOR
	RT0.rgb = lerp(RT0.rgb,
		gInstanceData[MOD_BLENDCOLOR].rgb,
		gInstanceData[MOD_BLENDCOLOR].a);
#endif
}`
compile_hlsl_before_after `/T ps_3_0`
compile_hlsl4 `/T ps_4_0`
in_shader `clip`
compile_glsl `-S frag`
in_shader `discard`

// `raymarching shader`
source `
float2 iResolution;
float sphereSDF(float3 s, float3 p, float r)
{
	return length(s - p) - r;
}
float sceneSDF(float3 s)
{
	return sphereSDF(s, 0, 1);
}
float raymarch(float3 eye, float3 dir, float start, float end)
{
	float depth = start;
	for (int i = 0; i < 100; ++i)
	{
		float dst = sceneSDF(eye + depth * dir);
		if (dst < 0.001)
			return dst;
		depth += dst;
		if (depth >= end)
			return end;
	}
	return end;
}
float3 rayDir(float fov, float2 size, float2 coord)
{
	float2 xy = coord - size / 2;
	float z = size.y / tan(radians(fov) / 2);
	return normalize(float3(xy, -z));
}
void main(out float4 outCol : COLOR0, in float2 coord : TEXCOORD0)
{
	float3 dir = rayDir(45, iResolution, coord);
	float3 eye = {0,0,5};
	float dist = raymarch(eye, dir, 0, 100);
	if (dist >= 100-0.001)
	{
		outCol = 0;
		return;
	}
	outCol = float4(coord.x % 2, coord.y % 2,1,1);
}`
compile_hlsl_before_after `/T ps_3_0`
compile_hlsl4 `/T ps_4_0`
compile_glsl `-S frag`
compile_glsl_es100 `-S frag`
