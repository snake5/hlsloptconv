
// `one initialized variable`
source `float4 main( float4 p : POSITION ) : POSITION { float a = 1; return a; }`
compile_hlsl_before_after ``
compile_glsl ``

// `two initialized variables`
source `float4 main( float4 p : POSITION ) : POSITION { float a = 1; float b = 2; return a + b; }`
compile_hlsl_before_after ``
compile_glsl ``

// `two initialized variables in same decl`
source `float4 main( float4 p : POSITION ) : POSITION { float a = 1, b = 2; return a + b; }`
compile_hlsl_before_after ``
compile_glsl ``

// `initializer list (basic)`
source `float4 main() : POSITION { float4 x = { 1, 2, 3, 4 }; return x; }`
compile_hlsl_before_after ``
compile_glsl ``

// `initializer list (elements)`
source `float4 main( float2 p : POSITION ) : POSITION { float4 x = { 3, p, 4 }; return x; }`
compile_hlsl_before_after ``
compile_glsl ``

// `initializer list (brace spam)`
source `float4 main( float2 p : POSITION ) : POSITION { float4 x = { 3, {{p}}, {{{4},}}, }; return x; }`
compile_hlsl_before_after ``
compile_glsl ``

// `numeric type ctors (basic)`
source `float4 main() : POSITION { return float4( 1, 2, 3, 4 ) + float( 5 ); }`
compile_hlsl_before_after ``
compile_glsl ``

// `numeric type ctors (elements)`
source `float4 main( float2 p : POSITION ) : POSITION { return float4( 3, p, 4 ); }`
compile_hlsl_before_after ``
compile_glsl ``

// `array type`
source `float4 main() : POSITION { float4 arr[2]; arr[0] = 0; arr[1] = 1; return arr[0] + arr[1]; }`
compile_hlsl_before_after ``
compile_glsl ``

// `array with initializer`
source `float4 main() : POSITION { float4 arr[2] = { float4(0,2,4,6), float4(1,2,3,4) }; return arr[0] + arr[1]; }`
compile_hlsl_before_after ``
compile_glsl ``

// `scalar swizzle 1`
source `void main( out float4 OUT : POSITION ){ OUT = 1.0.xxxx; }`
compile_hlsl_before_after ``
compile_glsl ``

// `bad scalar swizzle 1`
source `void main( out float4 OUT : POSITION ){ OUT = 1.0.xxxy; }`
compile_fail ``

// `bad read swizzle 1`
source `void main( in float4 IN : POSITION, out float4 OUT : POSITION ){ OUT = IN.xyzq; }`
compile_fail ``

// `bad read swizzle 2`
source `void main( in float4 IN : POSITION, out float4 OUT : POSITION ){ OUT = IN.xyrg; }`
compile_fail ``

// `bad write swizzle 1`
source `void main( in float4 IN : POSITION, out float4 OUT : POSITION ){ OUT.xyzq = IN; }`
compile_fail ``

// `bad write swizzle 2`
source `void main( in float4 IN : POSITION, out float4 OUT : POSITION ){ OUT.xyzy = IN; }`
compile_fail ``

// `piecewise variable init`
source `float4 main() : POSITION
{
	float4 o;
	o.x = 1.0; o.yz = 0.0; o.w = 1.0;
	return o;
}`
compile_hlsl_before_after ``
compile_glsl ``

// `piecewise variable init 2`
source `float4 main() : POSITION
{
	float4 o;
	o.x = 1.0;
	return o.x;
}`
compile_hlsl_before_after ``
compile_glsl ``

// `piecewise variable init fail`
source `float4 main() : POSITION
{
	float4 o;
	o.xz = 1.0; o.w = 1.0;
	return o;
}`
compile_fail ``

// `piecewise variable init fail 2`
source `float4 main() : POSITION
{
	float4 o;
	o.xz = 1.0; o.w = 1.0;
	return o.y;
}`
compile_fail ``

// `piecewise variable init fail 3`
source `float4 main() : POSITION
{
	float arr[2];
//	arr[0] = 0;
	arr[1] = 1;
	return arr[0] + arr[1];
}`
compile_fail ``

// `matrix RW swizzle x1`
source `float4 main() : POSITION
{
	float4x4 o;
	o._m22 = 0.0;
	return o._33;
}`
compile_hlsl_before_after ``
compile_glsl ``

// `matrix RW swizzle x4`
source `float4 main() : POSITION
{
	float4x4 o;
	o._m00_m21_m12_m33 = 0.0;
	return o._11_32_23_44;
}`
compile_hlsl_before_after ``
compile_glsl ``

// `struct I/O 1`
source `
struct v2p { float4 Position : POSITION; };
void main( out v2p OUT )
{
	OUT.Position = 0.0;
}`
compile_hlsl_before_after ``
compile_glsl ``

// `struct I/O 2`
source `
struct a2v { float4 Position : POSITION; };
struct v2p { float4 Position : POSITION; };
float4x4 ModelViewMatrix;
void main( in a2v IN, out v2p OUT )
{
	OUT.Position = IN.Position;
}`
compile_hlsl_before_after ``
compile_glsl ``

// `struct I/O 3a`
source `
struct vdata { float4 Position : POSITION; };
float4x4 ModelViewMatrix;
void main( in vdata IN, out vdata OUT )
{
	OUT = IN;
}`
compile_hlsl_before_after ``
compile_glsl ``

// `struct I/O 3b`
source `
struct vdata { float4 Position : POSITION; };
struct vdataw { vdata data; };
float4x4 ModelViewMatrix;
void main( in vdataw IN, out vdataw OUT )
{
	OUT = IN;
}`
compile_hlsl_before_after ``
compile_glsl ``

// `explicit uniform`
source `
uniform float4 outval;
float4 main() : POSITION { return outval; }`
compile_hlsl_before_after ``
compile_glsl ``

// `uniform array`
source `
uniform float4 outval[4];
float4 main() : POSITION { return outval[1]; }`
compile_hlsl_before_after ``
compile_glsl ``

// `explicit constant`
source `
static const float4 outval1 = {1,2,3,4};
static const float4 outval2 = float4(5,7,9,11);
static const float4 outval3 = outval1 + outval2;
float4 main() : POSITION { return outval1 + outval2 * outval3; }`
compile_hlsl_before_after ``
compile_glsl ``

// `bad explicit constant`
source `
const float4 outval;
float4 main() : POSITION { return outval; }`
compile_fail ``

// `basic cbuffer`
source `
cbuffer mybuf
{
	float4 outval;
}
float4 main() : POSITION { return outval; }`
compile_hlsl_before_after ``
compile_glsl ``

// `cbuffer w/ registers`
source `
cbuffer mybuf : register(b3)
{
	float2 outval : packoffset(c3.z);
}
float4 main() : POSITION { return outval.xyxy; }`
compile_hlsl_before_after ``
compile_glsl ``

// `samplers`
source `
sampler1D s1;
sampler2D s2;
sampler3D s3;
samplerCUBE sc;
float4 main() : POSITION { return 0; }`
compile_hlsl_before_after ``
compile_glsl ``

// `samplers w/ registers`
source `
sampler1D s1 : register(s0);
sampler2D s2 : register(s3);
sampler3D s3 : register(s1);
samplerCUBE sc : register(s6);
float4 main() : POSITION { return 0; }`
compile_hlsl_before_after ``
compile_glsl ``
