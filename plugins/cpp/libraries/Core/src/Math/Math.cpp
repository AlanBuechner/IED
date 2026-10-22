#include "Math.h"
#include <cmath>

namespace Math
{

	// --- Internal helpers (port of GLM's _noise.hpp) ----------------------

	namespace NoiseDetail
	{
		// Modulo 289 - avoids truncation effects in permutation
		template <typename T>
		inline T mod289(T x)
		{
			return x - Floor(x * (static_cast<T>(1.0) / static_cast<T>(289.0))) * static_cast<T>(289.0);
		}

		// Permutation function: p(p(p(x)))
		template <typename T>
		inline T permute(T x)
		{
			return mod289((x * static_cast<T>(34) + static_cast<T>(1)) * x);
		}

		// Taylor approximation of 1/sqrt(r) for normalizing gradient vectors
		template <typename T>
		inline T taylorInvSqrt(T r)
		{
			return static_cast<T>(1.79284291400159) - static_cast<T>(0.85373472095314) * r;
		}

		// Smoothstep fade curve: 6t^5 - 15t^4 + 10t^3
		template <typename T>
		inline T fade(T t)
		{
			return t * t * t * (t * (t * static_cast<T>(6) - static_cast<T>(15)) + static_cast<T>(10));
		}

		// GLM-style mix: lerp with clamped t
		template <typename T>
		inline T mix(T a, T b, T t)
		{
			return (a + (b - a) * t);
		}
	} // namespace NoiseDetail

	// =====================================================================
	// Classic Perlin noise (ported from GLM gtc_noise, no GLM dependency)
	// =====================================================================

	float Perlin(float p)
	{
		Vector2F pos(static_cast<float>(p), 0.f);
		return Perlin(pos);
	}

	float Perlin(const Vector2F& p)
	{
		using namespace NoiseDetail;

		Vector4F Pi = Floor(Vector4F(p.x, p.y, p.x, p.y)) + Vector4F(0.f, 0.f, 1.f, 1.f);
		Vector4F Pf = Fract(Vector4F(p.x, p.y, p.x, p.y)) - Vector4F(0.f, 0.f, 1.f, 1.f);

		Pi.x = mod289(Pi.x); Pi.y = mod289(Pi.y);
		Pi.z = mod289(Pi.z); Pi.w = mod289(Pi.w);

		Vector4F ix(Pi.x, Pi.z, Pi.x, Pi.z);
		Vector4F iy(Pi.y, Pi.y, Pi.w, Pi.w);
		Vector4F fx(Pf.x, Pf.z, Pf.x, Pf.z);
		Vector4F fy(Pf.y, Pf.y, Pf.w, Pf.w);

		// Component-wise permute
		Vector4F i;
		i.x = permute(permute(ix.x) + iy.x);
		i.y = permute(permute(ix.y) + iy.y);
		i.z = permute(permute(ix.z) + iy.z);
		i.w = permute(permute(ix.w) + iy.w);

		Vector4F gx = static_cast<float>(2) * Fract(i / static_cast<float>(41)) - static_cast<float>(1);
		Vector4F gy = Abs(gx) - static_cast<float>(0.5);
		Vector4F tx = Floor(gx + static_cast<float>(0.5));
		gx = gx - tx;

		Vector2F g00(gx.x, gy.x);
		Vector2F g10(gx.y, gy.y);
		Vector2F g01(gx.z, gy.z);
		Vector2F g11(gx.w, gy.w);

		float norm0 = taylorInvSqrt(g00.x * g00.x + g00.y * g00.y);
		float norm1 = taylorInvSqrt(g01.x * g01.x + g01.y * g01.y);
		float norm2 = taylorInvSqrt(g10.x * g10.x + g10.y * g10.y);
		float norm3 = taylorInvSqrt(g11.x * g11.x + g11.y * g11.y);

		g00 *= norm0; g01 *= norm1; g10 *= norm2; g11 *= norm3;

		float n00 = g00.x * fx.x + g00.y * fy.x;
		float n10 = g10.x * fx.y + g10.y * fy.y;
		float n01 = g01.x * fx.z + g01.y * fy.z;
		float n11 = g11.x * fx.w + g11.y * fy.w;

		Vector2F fade_xy(fade(Pf.x), fade(Pf.y));
		Vector2F n_x(mix(n00, n10, fade_xy.x), mix(n01, n11, fade_xy.x));
		float n_xy = mix(n_x.x, n_x.y, fade_xy.y);

		return static_cast<float>(2.3) * n_xy;
	}

	float Perlin(const Vector3F& p)
	{
		using namespace NoiseDetail;

		Vector3F Pi0 = Floor(p);
		Vector3F Pi1 = Pi0 + Vector3F(1.f, 1.f, 1.f);

		Pi0.x = mod289(Pi0.x); Pi0.y = mod289(Pi0.y); Pi0.z = mod289(Pi0.z);
		Pi1.x = mod289(Pi1.x); Pi1.y = mod289(Pi1.y); Pi1.z = mod289(Pi1.z);

		Vector3F Pf0 = Fract(p);
		Vector3F Pf1 = Pf0 - Vector3F(1.f, 1.f, 1.f);

		Vector4F ix(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
		Vector4F iy(Pi0.y, Pi0.y, Pi1.y, Pi1.y);

		float iz0 = Pi0.z;
		float iz1 = Pi1.z;

		Vector4F ixy;
		ixy.x = permute(permute(ix.x) + iy.x);
		ixy.y = permute(permute(ix.y) + iy.y);
		ixy.z = permute(permute(ix.z) + iy.z);
		ixy.w = permute(permute(ix.w) + iy.w);

		Vector4F ixy0;
		ixy0.x = permute(ixy.x + iz0);
		ixy0.y = permute(ixy.y + iz0);
		ixy0.z = permute(ixy.z + iz0);
		ixy0.w = permute(ixy.w + iz0);

		Vector4F ixy1;
		ixy1.x = permute(ixy.x + iz1);
		ixy1.y = permute(ixy.y + iz1);
		ixy1.z = permute(ixy.z + iz1);
		ixy1.w = permute(ixy.w + iz1);

		Vector4F gx0 = ixy0 * static_cast<float>(1.0 / 7.0);
		Vector4F gy0 = Fract(Floor(gx0) * static_cast<float>(1.0 / 7.0)) - static_cast<float>(0.5);
		gx0 = Fract(gx0);
		Vector4F gz0 = Vector4F(0.5f, 0.5f, 0.5f, 0.5f) - Abs(gx0) - Abs(gy0);

		float sz0_x = Step(static_cast<float>(0), gz0.x);
		float sz0_y = Step(static_cast<float>(0), gz0.y);
		float sz0_z = Step(static_cast<float>(0), gz0.z);
		float sz0_w = Step(static_cast<float>(0), gz0.w);

		gx0.x -= sz0_x * (Step(static_cast<float>(0), gx0.x) - 0.5f);
		gy0.x -= sz0_x * (Step(static_cast<float>(0), gy0.x) - 0.5f);
		gx0.y -= sz0_y * (Step(static_cast<float>(0), gx0.y) - 0.5f);
		gy0.y -= sz0_y * (Step(static_cast<float>(0), gy0.y) - 0.5f);
		gx0.z -= sz0_z * (Step(static_cast<float>(0), gx0.z) - 0.5f);
		gy0.z -= sz0_z * (Step(static_cast<float>(0), gy0.z) - 0.5f);
		gx0.w -= sz0_w * (Step(static_cast<float>(0), gx0.w) - 0.5f);
		gy0.w -= sz0_w * (Step(static_cast<float>(0), gy0.w) - 0.5f);

		Vector4F gx1 = ixy1 * static_cast<float>(1.0 / 7.0);
		Vector4F gy1 = Fract(Floor(gx1) * static_cast<float>(1.0 / 7.0)) - static_cast<float>(0.5);
		gx1 = Fract(gx1);
		Vector4F gz1 = Vector4F(0.5f, 0.5f, 0.5f, 0.5f) - Abs(gx1) - Abs(gy1);

		float sz1_x = Step(static_cast<float>(0), gz1.x);
		float sz1_y = Step(static_cast<float>(0), gz1.y);
		float sz1_z = Step(static_cast<float>(0), gz1.z);
		float sz1_w = Step(static_cast<float>(0), gz1.w);

		gx1.x -= sz1_x * (Step(static_cast<float>(0), gx1.x) - 0.5f);
		gy1.x -= sz1_x * (Step(static_cast<float>(0), gy1.x) - 0.5f);
		gx1.y -= sz1_y * (Step(static_cast<float>(0), gx1.y) - 0.5f);
		gy1.y -= sz1_y * (Step(static_cast<float>(0), gy1.y) - 0.5f);
		gx1.z -= sz1_z * (Step(static_cast<float>(0), gx1.z) - 0.5f);
		gy1.z -= sz1_z * (Step(static_cast<float>(0), gy1.z) - 0.5f);
		gx1.w -= sz1_w * (Step(static_cast<float>(0), gx1.w) - 0.5f);
		gy1.w -= sz1_w * (Step(static_cast<float>(0), gy1.w) - 0.5f);

		Vector3F g000(gx0.x, gy0.x, gz0.x);
		Vector3F g100(gx0.y, gy0.y, gz0.y);
		Vector3F g010(gx0.z, gy0.z, gz0.z);
		Vector3F g110(gx0.w, gy0.w, gz0.w);
		Vector3F g001(gx1.x, gy1.x, gz1.x);
		Vector3F g101(gx1.y, gy1.y, gz1.y);
		Vector3F g011(gx1.z, gy1.z, gz1.z);
		Vector3F g111(gx1.w, gy1.w, gz1.w);

		float nrm0 = taylorInvSqrt(g000.x * g000.x + g000.y * g000.y + g000.z * g000.z);
		float nrm1 = taylorInvSqrt(g010.x * g010.x + g010.y * g010.y + g010.z * g010.z);
		float nrm2 = taylorInvSqrt(g100.x * g100.x + g100.y * g100.y + g100.z * g100.z);
		float nrm3 = taylorInvSqrt(g110.x * g110.x + g110.y * g110.y + g110.z * g110.z);
		g000 *= nrm0; g010 *= nrm1; g100 *= nrm2; g110 *= nrm3;

		float nrm4 = taylorInvSqrt(g001.x * g001.x + g001.y * g001.y + g001.z * g001.z);
		float nrm5 = taylorInvSqrt(g011.x * g011.x + g011.y * g011.y + g011.z * g011.z);
		float nrm6 = taylorInvSqrt(g101.x * g101.x + g101.y * g101.y + g101.z * g101.z);
		float nrm7 = taylorInvSqrt(g111.x * g111.x + g111.y * g111.y + g111.z * g111.z);
		g001 *= nrm4; g011 *= nrm5; g101 *= nrm6; g111 *= nrm7;

		float n000 = g000.x * Pf0.x + g000.y * Pf0.y + g000.z * Pf0.z;
		float n100 = g100.x * Pf1.x + g100.y * Pf0.y + g100.z * Pf0.z;
		float n010 = g010.x * Pf0.x + g010.y * Pf1.y + g010.z * Pf0.z;
		float n110 = g110.x * Pf1.x + g110.y * Pf1.y + g110.z * Pf0.z;
		float n001 = g001.x * Pf0.x + g001.y * Pf0.y + g001.z * Pf1.z;
		float n101 = g101.x * Pf1.x + g101.y * Pf0.y + g101.z * Pf1.z;
		float n011 = g011.x * Pf0.x + g011.y * Pf1.y + g011.z * Pf1.z;
		float n111 = g111.x * Pf1.x + g111.y * Pf1.y + g111.z * Pf1.z;

		Vector3F fade_xyz(fade(Pf0.x), fade(Pf0.y), fade(Pf0.z));
		Vector4F n_z(mix(n000, n001, fade_xyz.z), mix(n100, n101, fade_xyz.z),
			mix(n010, n011, fade_xyz.z), mix(n110, n111, fade_xyz.z));
		Vector2F n_yz(mix(n_z.x, n_z.y, fade_xyz.y), mix(n_z.z, n_z.w, fade_xyz.y));
		float n_xyz = mix(n_yz.x, n_yz.y, fade_xyz.x);

		return static_cast<float>(2.2) * n_xyz;
	}

	float Perlin(const Vector4F& p)
	{
		using namespace NoiseDetail;

		Vector4F Pi0 = Floor(p);
		Vector4F Pi1 = Pi0 + Vector4F(1.f, 1.f, 1.f, 1.f);

		Pi0.x = mod289(Pi0.x); Pi0.y = mod289(Pi0.y);
		Pi0.z = mod289(Pi0.z); Pi0.w = mod289(Pi0.w);
		Pi1.x = mod289(Pi1.x); Pi1.y = mod289(Pi1.y);
		Pi1.z = mod289(Pi1.z); Pi1.w = mod289(Pi1.w);

		Vector4F Pf0 = Fract(p);
		Vector4F Pf1 = Pf0 - Vector4F(1.f, 1.f, 1.f, 1.f);

		Vector4F ix(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
		Vector4F iy(Pi0.y, Pi0.y, Pi1.y, Pi1.y);

		float iz0 = Pi0.z; float iz1 = Pi1.z;
		float iw0 = Pi0.w; float iw1 = Pi1.w;

		Vector4F ixy;
		ixy.x = permute(permute(ix.x) + iy.x);
		ixy.y = permute(permute(ix.y) + iy.y);
		ixy.z = permute(permute(ix.z) + iy.z);
		ixy.w = permute(permute(ix.w) + iy.w);

		Vector4F ixy0, ixy1;
		ixy0.x = permute(ixy.x + iz0); ixy0.y = permute(ixy.y + iz0);
		ixy0.z = permute(ixy.z + iz0); ixy0.w = permute(ixy.w + iz0);
		ixy1.x = permute(ixy.x + iz1); ixy1.y = permute(ixy.y + iz1);
		ixy1.z = permute(ixy.z + iz1); ixy1.w = permute(ixy.w + iz1);

		Vector4F ixy00, ixy01, ixy10, ixy11;
		ixy00.x = permute(ixy0.x + iw0); ixy00.y = permute(ixy0.y + iw0);
		ixy00.z = permute(ixy0.z + iw0); ixy00.w = permute(ixy0.w + iw0);
		ixy01.x = permute(ixy0.x + iw1); ixy01.y = permute(ixy0.y + iw1);
		ixy01.z = permute(ixy0.z + iw1); ixy01.w = permute(ixy0.w + iw1);
		ixy10.x = permute(ixy1.x + iw0); ixy10.y = permute(ixy1.y + iw0);
		ixy10.z = permute(ixy1.z + iw0); ixy10.w = permute(ixy1.w + iw0);
		ixy11.x = permute(ixy1.x + iw1); ixy11.y = permute(ixy1.y + iw1);
		ixy11.z = permute(ixy1.z + iw1); ixy11.w = permute(ixy1.w + iw1);

		// Gradient generation for 4D (using grad4 logic)
		auto grad4 = [](float j, const Vector4F& ip) -> Vector4F {
			Vector3F pXYZ = Floor(Fract(Vector3F(j, j, j)) * Vector3F(ip.x, ip.y, ip.z)) * ip.w - Vector3F(1.f, 1.f, 1.f);
			float pW = 1.5f - (Abs(pXYZ.x) + Abs(pXYZ.y) + Abs(pXYZ.z));
			Vector4F s;
			s.x = Step(0.f, pXYZ.x); s.y = Step(0.f, pXYZ.y);
			s.z = Step(0.f, pXYZ.z); s.w = Step(0.f, pW);
			pXYZ.x = pXYZ.x + ((2.f * s.x - 1.f) * s.w);
			pXYZ.y = pXYZ.y + ((2.f * s.y - 1.f) * s.w);
			pXYZ.z = pXYZ.z + ((2.f * s.z - 1.f) * s.w);
			return Vector4F(pXYZ.x, pXYZ.y, pXYZ.z, pW);
		};

		Vector4F ip(1.f / 294.f, 1.f / 49.f, 1.f / 7.f, 0.f);

		Vector4F g0000 = grad4(ixy00.x, ip);
		Vector4F g1000 = grad4(ixy00.y, ip);
		Vector4F g0100 = grad4(ixy00.z, ip);
		Vector4F g1100 = grad4(ixy00.w, ip);
		Vector4F g0010 = grad4(ixy10.x, ip);
		Vector4F g1010 = grad4(ixy10.y, ip);
		Vector4F g0110 = grad4(ixy10.z, ip);
		Vector4F g1110 = grad4(ixy10.w, ip);
		Vector4F g0001 = grad4(ixy01.x, ip);
		Vector4F g1001 = grad4(ixy01.y, ip);
		Vector4F g0101 = grad4(ixy01.z, ip);
		Vector4F g1101 = grad4(ixy01.w, ip);
		Vector4F g0011 = grad4(ixy11.x, ip);
		Vector4F g1011 = grad4(ixy11.y, ip);
		Vector4F g0111 = grad4(ixy11.z, ip);
		Vector4F g1111 = grad4(ixy11.w, ip);

		auto dot4 = [](const Vector4F& a, const Vector4F& b) -> float {
			return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		};

		float n0 = taylorInvSqrt(dot4(g0000, g0000));
		float n1 = taylorInvSqrt(dot4(g0100, g0100));
		float n2 = taylorInvSqrt(dot4(g1000, g1000));
		float n3 = taylorInvSqrt(dot4(g1100, g1100));
		g0000 *= n0; g0100 *= n1; g1000 *= n2; g1100 *= n3;

		n0 = taylorInvSqrt(dot4(g0001, g0001));
		n1 = taylorInvSqrt(dot4(g0101, g0101));
		n2 = taylorInvSqrt(dot4(g1001, g1001));
		n3 = taylorInvSqrt(dot4(g1101, g1101));
		g0001 *= n0; g0101 *= n1; g1001 *= n2; g1101 *= n3;

		n0 = taylorInvSqrt(dot4(g0010, g0010));
		n1 = taylorInvSqrt(dot4(g0110, g0110));
		n2 = taylorInvSqrt(dot4(g1010, g1010));
		n3 = taylorInvSqrt(dot4(g1110, g1110));
		g0010 *= n0; g0110 *= n1; g1010 *= n2; g1110 *= n3;

		n0 = taylorInvSqrt(dot4(g0011, g0011));
		n1 = taylorInvSqrt(dot4(g0111, g0111));
		n2 = taylorInvSqrt(dot4(g1011, g1011));
		n3 = taylorInvSqrt(dot4(g1111, g1111));
		g0011 *= n0; g0111 *= n1; g1011 *= n2; g1111 *= n3;

		auto dotVec = [](const Vector4F& a, const Vector4F& b) -> float {
			return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		};

		float n0000 = dotVec(g0000, Pf0);
		float n1000 = g1000.x * Pf1.x + g1000.y * Pf0.y + g1000.z * Pf0.z + g1000.w * Pf0.w;
		float n0100 = g0100.x * Pf0.x + g0100.y * Pf1.y + g0100.z * Pf0.z + g0100.w * Pf0.w;
		float n1100 = g1100.x * Pf1.x + g1100.y * Pf1.y + g1100.z * Pf0.z + g1100.w * Pf0.w;
		float n0010 = g0010.x * Pf0.x + g0010.y * Pf0.y + g0010.z * Pf1.z + g0010.w * Pf0.w;
		float n1010 = g1010.x * Pf1.x + g1010.y * Pf0.y + g1010.z * Pf1.z + g1010.w * Pf0.w;
		float n0110 = g0110.x * Pf0.x + g0110.y * Pf1.y + g0110.z * Pf1.z + g0110.w * Pf0.w;
		float n1110 = g1110.x * Pf1.x + g1110.y * Pf1.y + g1110.z * Pf1.z + g1110.w * Pf0.w;
		float n0001 = g0001.x * Pf0.x + g0001.y * Pf0.y + g0001.z * Pf0.z + g0001.w * Pf1.w;
		float n1001 = g1001.x * Pf1.x + g1001.y * Pf0.y + g1001.z * Pf0.z + g1001.w * Pf1.w;
		float n0101 = g0101.x * Pf0.x + g0101.y * Pf1.y + g0101.z * Pf0.z + g0101.w * Pf1.w;
		float n1101 = g1101.x * Pf1.x + g1101.y * Pf1.y + g1101.z * Pf0.z + g1101.w * Pf1.w;
		float n0011 = g0011.x * Pf0.x + g0011.y * Pf0.y + g0011.z * Pf1.z + g0011.w * Pf1.w;
		float n1011 = g1011.x * Pf1.x + g1011.y * Pf0.y + g1011.z * Pf1.z + g1011.w * Pf1.w;
		float n0111 = g0111.x * Pf0.x + g0111.y * Pf1.y + g0111.z * Pf1.z + g0111.w * Pf1.w;
		float n1111 = dotVec(g1111, Pf1);

		Vector4F fade_xyzw(fade(Pf0.x), fade(Pf0.y), fade(Pf0.z), fade(Pf0.w));

		Vector4F n_0w(mix(n0000, n0001, fade_xyzw.w), mix(n1000, n1001, fade_xyzw.w),
			mix(n0100, n0101, fade_xyzw.w), mix(n1100, n1101, fade_xyzw.w));
		Vector4F n_1w(mix(n0010, n0011, fade_xyzw.w), mix(n1010, n1011, fade_xyzw.w),
			mix(n0110, n0111, fade_xyzw.w), mix(n1110, n1111, fade_xyzw.w));
		Vector4F n_zw(mix(n_0w.x, n_1w.x, fade_xyzw.z), mix(n_0w.y, n_1w.y, fade_xyzw.z),
			mix(n_0w.z, n_1w.z, fade_xyzw.z), mix(n_0w.w, n_1w.w, fade_xyzw.z));
		Vector2F n_yzw(mix(n_zw.x, n_zw.y, fade_xyzw.y), mix(n_zw.z, n_zw.w, fade_xyzw.y));
		float n_xyzw = mix(n_yzw.x, n_yzw.y, fade_xyzw.x);

		return static_cast<float>(2.2) * n_xyzw;
	}

	// --- Periodic Perlin noise --------------------------------------------

	float Perlin(float p, float rep)
	{
		Vector2F pos(static_cast<float>(p), 0.f);
		Vector2F repVec(rep, 1.f);
		return Perlin(pos, repVec);
	}

	float Perlin(const Vector2F& p, const Vector2F& rep)
	{
		using namespace NoiseDetail;

		Vector4F Pi = Floor(Vector4F(p.x, p.y, p.x, p.y)) + Vector4F(0.f, 0.f, 1.f, 1.f);
		Vector4F Pf = Fract(Vector4F(p.x, p.y, p.x, p.y)) - Vector4F(0.f, 0.f, 1.f, 1.f);

		// Modulo period first, then mod289
		Pi.x = mod289(Mod(Pi.x, rep.x)); Pi.y = mod289(Mod(Pi.y, rep.y));
		Pi.z = mod289(Mod(Pi.z, rep.x)); Pi.w = mod289(Mod(Pi.w, rep.y));

		Vector4F ix(Pi.x, Pi.z, Pi.x, Pi.z);
		Vector4F iy(Pi.y, Pi.y, Pi.w, Pi.w);
		Vector4F fx(Pf.x, Pf.z, Pf.x, Pf.z);
		Vector4F fy(Pf.y, Pf.y, Pf.w, Pf.w);

		Vector4F i;
		i.x = permute(permute(ix.x) + iy.x);
		i.y = permute(permute(ix.y) + iy.y);
		i.z = permute(permute(ix.z) + iy.z);
		i.w = permute(permute(ix.w) + iy.w);

		Vector4F gx = static_cast<float>(2) * Fract(i / static_cast<float>(41)) - static_cast<float>(1);
		Vector4F gy = Abs(gx) - static_cast<float>(0.5);
		Vector4F tx = Floor(gx + static_cast<float>(0.5));
		gx = gx - tx;

		Vector2F g00(gx.x, gy.x);
		Vector2F g10(gx.y, gy.y);
		Vector2F g01(gx.z, gy.z);
		Vector2F g11(gx.w, gy.w);

		float norm0 = taylorInvSqrt(g00.x * g00.x + g00.y * g00.y);
		float norm1 = taylorInvSqrt(g01.x * g01.x + g01.y * g01.y);
		float norm2 = taylorInvSqrt(g10.x * g10.x + g10.y * g10.y);
		float norm3 = taylorInvSqrt(g11.x * g11.x + g11.y * g11.y);

		g00 *= norm0; g01 *= norm1; g10 *= norm2; g11 *= norm3;

		float n00 = g00.x * fx.x + g00.y * fy.x;
		float n10 = g10.x * fx.y + g10.y * fy.y;
		float n01 = g01.x * fx.z + g01.y * fy.z;
		float n11 = g11.x * fx.w + g11.y * fy.w;

		Vector2F fade_xy(fade(Pf.x), fade(Pf.y));
		Vector2F n_x(mix(n00, n10, fade_xy.x), mix(n01, n11, fade_xy.x));
		float n_xy = mix(n_x.x, n_x.y, fade_xy.y);

		return static_cast<float>(2.3) * n_xy;
	}

	float Perlin(const Vector3F& p, const Vector3F& rep)
	{
		using namespace NoiseDetail;

		Vector3F Pi0 = Mod(Floor(p), rep);
		Vector3F Pi1 = Mod(Pi0 + Vector3F(1.f, 1.f, 1.f), rep);

		Pi0.x = mod289(Pi0.x); Pi0.y = mod289(Pi0.y); Pi0.z = mod289(Pi0.z);
		Pi1.x = mod289(Pi1.x); Pi1.y = mod289(Pi1.y); Pi1.z = mod289(Pi1.z);

		Vector3F Pf0 = Fract(p);
		Vector3F Pf1 = Pf0 - Vector3F(1.f, 1.f, 1.f);

		Vector4F ix(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
		Vector4F iy(Pi0.y, Pi0.y, Pi1.y, Pi1.y);

		float iz0 = Pi0.z; float iz1 = Pi1.z;

		Vector4F ixy;
		ixy.x = permute(permute(ix.x) + iy.x);
		ixy.y = permute(permute(ix.y) + iy.y);
		ixy.z = permute(permute(ix.z) + iy.z);
		ixy.w = permute(permute(ix.w) + iy.w);

		Vector4F ixy0, ixy1;
		ixy0.x = permute(ixy.x + iz0); ixy0.y = permute(ixy.y + iz0);
		ixy0.z = permute(ixy.z + iz0); ixy0.w = permute(ixy.w + iz0);
		ixy1.x = permute(ixy.x + iz1); ixy1.y = permute(ixy.y + iz1);
		ixy1.z = permute(ixy.z + iz1); ixy1.w = permute(ixy.w + iz1);

		Vector4F gx0 = ixy0 * static_cast<float>(1.0 / 7.0);
		Vector4F gy0 = Fract(Floor(gx0) * static_cast<float>(1.0 / 7.0)) - static_cast<float>(0.5);
		gx0 = Fract(gx0);
		Vector4F gz0 = Vector4F(0.5f, 0.5f, 0.5f, 0.5f) - Abs(gx0) - Abs(gy0);

		float sz0_x = Step(0.f, gz0.x); float sz0_y = Step(0.f, gz0.y);
		float sz0_z = Step(0.f, gz0.z); float sz0_w = Step(0.f, gz0.w);
		gx0.x -= sz0_x * (Step(0.f, gx0.x) - 0.5f); gy0.x -= sz0_x * (Step(0.f, gy0.x) - 0.5f);
		gx0.y -= sz0_y * (Step(0.f, gx0.y) - 0.5f); gy0.y -= sz0_y * (Step(0.f, gy0.y) - 0.5f);
		gx0.z -= sz0_z * (Step(0.f, gx0.z) - 0.5f); gy0.z -= sz0_z * (Step(0.f, gy0.z) - 0.5f);
		gx0.w -= sz0_w * (Step(0.f, gx0.w) - 0.5f); gy0.w -= sz0_w * (Step(0.f, gy0.w) - 0.5f);

		Vector4F gx1 = ixy1 * static_cast<float>(1.0 / 7.0);
		Vector4F gy1 = Fract(Floor(gx1) * static_cast<float>(1.0 / 7.0)) - static_cast<float>(0.5);
		gx1 = Fract(gx1);
		Vector4F gz1 = Vector4F(0.5f, 0.5f, 0.5f, 0.5f) - Abs(gx1) - Abs(gy1);

		float sz1_x = Step(0.f, gz1.x); float sz1_y = Step(0.f, gz1.y);
		float sz1_z = Step(0.f, gz1.z); float sz1_w = Step(0.f, gz1.w);
		gx1.x -= sz1_x * (Step(0.f, gx1.x) - 0.5f); gy1.x -= sz1_x * (Step(0.f, gy1.x) - 0.5f);
		gx1.y -= sz1_y * (Step(0.f, gx1.y) - 0.5f); gy1.y -= sz1_y * (Step(0.f, gy1.y) - 0.5f);
		gx1.z -= sz1_z * (Step(0.f, gx1.z) - 0.5f); gy1.z -= sz1_z * (Step(0.f, gy1.z) - 0.5f);
		gx1.w -= sz1_w * (Step(0.f, gx1.w) - 0.5f); gy1.w -= sz1_w * (Step(0.f, gy1.w) - 0.5f);

		Vector3F g000(gx0.x, gy0.x, gz0.x);
		Vector3F g100(gx0.y, gy0.y, gz0.y);
		Vector3F g010(gx0.z, gy0.z, gz0.z);
		Vector3F g110(gx0.w, gy0.w, gz0.w);
		Vector3F g001(gx1.x, gy1.x, gz1.x);
		Vector3F g101(gx1.y, gy1.y, gz1.y);
		Vector3F g011(gx1.z, gy1.z, gz1.z);
		Vector3F g111(gx1.w, gy1.w, gz1.w);

		auto dot3 = [](const Vector3F& a, const Vector3F& b) -> float {
			return a.x * b.x + a.y * b.y + a.z * b.z;
		};

		float nrm0 = taylorInvSqrt(dot3(g000, g000));
		float nrm1 = taylorInvSqrt(dot3(g010, g010));
		float nrm2 = taylorInvSqrt(dot3(g100, g100));
		float nrm3 = taylorInvSqrt(dot3(g110, g110));
		g000 *= nrm0; g010 *= nrm1; g100 *= nrm2; g110 *= nrm3;

		nrm0 = taylorInvSqrt(dot3(g001, g001));
		nrm1 = taylorInvSqrt(dot3(g011, g011));
		nrm2 = taylorInvSqrt(dot3(g101, g101));
		nrm3 = taylorInvSqrt(dot3(g111, g111));
		g001 *= nrm0; g011 *= nrm1; g101 *= nrm2; g111 *= nrm3;

		float n000 = dot3(g000, Pf0);
		float n100 = g100.x * Pf1.x + g100.y * Pf0.y + g100.z * Pf0.z;
		float n010 = g010.x * Pf0.x + g010.y * Pf1.y + g010.z * Pf0.z;
		float n110 = g110.x * Pf1.x + g110.y * Pf1.y + g110.z * Pf0.z;
		float n001 = g001.x * Pf0.x + g001.y * Pf0.y + g001.z * Pf1.z;
		float n101 = g101.x * Pf1.x + g101.y * Pf0.y + g101.z * Pf1.z;
		float n011 = g011.x * Pf0.x + g011.y * Pf1.y + g011.z * Pf1.z;
		float n111 = dot3(g111, Pf1);

		Vector3F fade_xyz(fade(Pf0.x), fade(Pf0.y), fade(Pf0.z));
		Vector4F n_z(mix(n000, n001, fade_xyz.z), mix(n100, n101, fade_xyz.z),
			mix(n010, n011, fade_xyz.z), mix(n110, n111, fade_xyz.z));
		Vector2F n_yz(mix(n_z.x, n_z.y, fade_xyz.y), mix(n_z.z, n_z.w, fade_xyz.y));
		float n_xyz = mix(n_yz.x, n_yz.y, fade_xyz.x);

		return static_cast<float>(2.2) * n_xyz;
	}

	float Perlin(const Vector4F& p, const Vector4F& rep)
	{
		using namespace NoiseDetail;

		Vector4F Pi0 = Mod(Floor(p), rep);
		Vector4F Pi1 = Mod(Pi0 + Vector4F(1.f, 1.f, 1.f, 1.f), rep);

		Pi0.x = mod289(Pi0.x); Pi0.y = mod289(Pi0.y);
		Pi0.z = mod289(Pi0.z); Pi0.w = mod289(Pi0.w);
		Pi1.x = mod289(Pi1.x); Pi1.y = mod289(Pi1.y);
		Pi1.z = mod289(Pi1.z); Pi1.w = mod289(Pi1.w);

		Vector4F Pf0 = Fract(p);
		Vector4F Pf1 = Pf0 - Vector4F(1.f, 1.f, 1.f, 1.f);

		Vector4F ix(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
		Vector4F iy(Pi0.y, Pi0.y, Pi1.y, Pi1.y);

		float iz0 = Pi0.z; float iz1 = Pi1.z;
		float iw0 = Pi0.w; float iw1 = Pi1.w;

		Vector4F ixy;
		ixy.x = permute(permute(ix.x) + iy.x);
		ixy.y = permute(permute(ix.y) + iy.y);
		ixy.z = permute(permute(ix.z) + iy.z);
		ixy.w = permute(permute(ix.w) + iy.w);

		Vector4F ixy0, ixy1;
		ixy0.x = permute(ixy.x + iz0); ixy0.y = permute(ixy.y + iz0);
		ixy0.z = permute(ixy.z + iz0); ixy0.w = permute(ixy.w + iz0);
		ixy1.x = permute(ixy.x + iz1); ixy1.y = permute(ixy.y + iz1);
		ixy1.z = permute(ixy.z + iz1); ixy1.w = permute(ixy.w + iz1);

		Vector4F ixy00, ixy01, ixy10, ixy11;
		ixy00.x = permute(ixy0.x + iw0); ixy00.y = permute(ixy0.y + iw0);
		ixy00.z = permute(ixy0.z + iw0); ixy00.w = permute(ixy0.w + iw0);
		ixy01.x = permute(ixy0.x + iw1); ixy01.y = permute(ixy0.y + iw1);
		ixy01.z = permute(ixy0.z + iw1); ixy01.w = permute(ixy0.w + iw1);
		ixy10.x = permute(ixy1.x + iw0); ixy10.y = permute(ixy1.y + iw0);
		ixy10.z = permute(ixy1.z + iw0); ixy10.w = permute(ixy1.w + iw0);
		ixy11.x = permute(ixy1.x + iw1); ixy11.y = permute(ixy1.y + iw1);
		ixy11.z = permute(ixy1.z + iw1); ixy11.w = permute(ixy1.w + iw1);

		Vector4F ip(1.f / 294.f, 1.f / 49.f, 1.f / 7.f, 0.f);

		auto grad4 = [](float j, const Vector4F& ip) -> Vector4F {
			Vector3F pXYZ = Floor(Fract(Vector3F(j, j, j)) * Vector3F(ip.x, ip.y, ip.z)) * ip.w - Vector3F(1.f, 1.f, 1.f);
			float pW = 1.5f - (Abs(pXYZ.x) + Abs(pXYZ.y) + Abs(pXYZ.z));
			Vector4F s;
			s.x = Step(0.f, pXYZ.x); s.y = Step(0.f, pXYZ.y);
			s.z = Step(0.f, pXYZ.z); s.w = Step(0.f, pW);
			pXYZ.x += (2.f * s.x - 1.f) * s.w;
			pXYZ.y += (2.f * s.y - 1.f) * s.w;
			pXYZ.z += (2.f * s.z - 1.f) * s.w;
			return Vector4F(pXYZ.x, pXYZ.y, pXYZ.z, pW);
		};

		auto dotVec = [](const Vector4F& a, const Vector4F& b) -> float {
			return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		};

		Vector4F g0000 = grad4(ixy00.x, ip);
		Vector4F g1000 = grad4(ixy00.y, ip);
		Vector4F g0100 = grad4(ixy00.z, ip);
		Vector4F g1100 = grad4(ixy00.w, ip);
		Vector4F g0010 = grad4(ixy10.x, ip);
		Vector4F g1010 = grad4(ixy10.y, ip);
		Vector4F g0110 = grad4(ixy10.z, ip);
		Vector4F g1110 = grad4(ixy10.w, ip);
		Vector4F g0001 = grad4(ixy01.x, ip);
		Vector4F g1001 = grad4(ixy01.y, ip);
		Vector4F g0101 = grad4(ixy01.z, ip);
		Vector4F g1101 = grad4(ixy01.w, ip);
		Vector4F g0011 = grad4(ixy11.x, ip);
		Vector4F g1011 = grad4(ixy11.y, ip);
		Vector4F g0111 = grad4(ixy11.z, ip);
		Vector4F g1111 = grad4(ixy11.w, ip);

		float n0 = taylorInvSqrt(dotVec(g0000, g0000));
		float n1 = taylorInvSqrt(dotVec(g0100, g0100));
		float n2 = taylorInvSqrt(dotVec(g1000, g1000));
		float n3 = taylorInvSqrt(dotVec(g1100, g1100));
		g0000 *= n0; g0100 *= n1; g1000 *= n2; g1100 *= n3;

		n0 = taylorInvSqrt(dotVec(g0001, g0001));
		n1 = taylorInvSqrt(dotVec(g0101, g0101));
		n2 = taylorInvSqrt(dotVec(g1001, g1001));
		n3 = taylorInvSqrt(dotVec(g1101, g1101));
		g0001 *= n0; g0101 *= n1; g1001 *= n2; g1101 *= n3;

		n0 = taylorInvSqrt(dotVec(g0010, g0010));
		n1 = taylorInvSqrt(dotVec(g0110, g0110));
		n2 = taylorInvSqrt(dotVec(g1010, g1010));
		n3 = taylorInvSqrt(dotVec(g1110, g1110));
		g0010 *= n0; g0110 *= n1; g1010 *= n2; g1110 *= n3;

		n0 = taylorInvSqrt(dotVec(g0011, g0011));
		n1 = taylorInvSqrt(dotVec(g0111, g0111));
		n2 = taylorInvSqrt(dotVec(g1011, g1011));
		n3 = taylorInvSqrt(dotVec(g1111, g1111));
		g0011 *= n0; g0111 *= n1; g1011 *= n2; g1111 *= n3;

		float n0000 = dotVec(g0000, Pf0);
		float n1000 = g1000.x * Pf1.x + g1000.y * Pf0.y + g1000.z * Pf0.z + g1000.w * Pf0.w;
		float n0100 = g0100.x * Pf0.x + g0100.y * Pf1.y + g0100.z * Pf0.z + g0100.w * Pf0.w;
		float n1100 = g1100.x * Pf1.x + g1100.y * Pf1.y + g1100.z * Pf0.z + g1100.w * Pf0.w;
		float n0010 = g0010.x * Pf0.x + g0010.y * Pf0.y + g0010.z * Pf1.z + g0010.w * Pf0.w;
		float n1010 = g1010.x * Pf1.x + g1010.y * Pf0.y + g1010.z * Pf1.z + g1010.w * Pf0.w;
		float n0110 = g0110.x * Pf0.x + g0110.y * Pf1.y + g0110.z * Pf1.z + g0110.w * Pf0.w;
		float n1110 = g1110.x * Pf1.x + g1110.y * Pf1.y + g1110.z * Pf1.z + g1110.w * Pf0.w;
		float n0001 = g0001.x * Pf0.x + g0001.y * Pf0.y + g0001.z * Pf0.z + g0001.w * Pf1.w;
		float n1001 = g1001.x * Pf1.x + g1001.y * Pf0.y + g1001.z * Pf0.z + g1001.w * Pf1.w;
		float n0101 = g0101.x * Pf0.x + g0101.y * Pf1.y + g0101.z * Pf0.z + g0101.w * Pf1.w;
		float n1101 = g1101.x * Pf1.x + g1101.y * Pf1.y + g1101.z * Pf0.z + g1101.w * Pf1.w;
		float n0011 = g0011.x * Pf0.x + g0011.y * Pf0.y + g0011.z * Pf1.z + g0011.w * Pf1.w;
		float n1011 = g1011.x * Pf1.x + g1011.y * Pf0.y + g1011.z * Pf1.z + g1011.w * Pf1.w;
		float n0111 = g0111.x * Pf0.x + g0111.y * Pf1.y + g0111.z * Pf1.z + g0111.w * Pf1.w;
		float n1111 = dotVec(g1111, Pf1);

		Vector4F fade_xyzw(fade(Pf0.x), fade(Pf0.y), fade(Pf0.z), fade(Pf0.w));

		Vector4F n_0w(mix(n0000, n0001, fade_xyzw.w), mix(n1000, n1001, fade_xyzw.w),
			mix(n0100, n0101, fade_xyzw.w), mix(n1100, n1101, fade_xyzw.w));
		Vector4F n_1w(mix(n0010, n0011, fade_xyzw.w), mix(n1010, n1011, fade_xyzw.w),
			mix(n0110, n0111, fade_xyzw.w), mix(n1110, n1111, fade_xyzw.w));
		Vector4F n_zw(mix(n_0w.x, n_1w.x, fade_xyzw.z), mix(n_0w.y, n_1w.y, fade_xyzw.z),
			mix(n_0w.z, n_1w.z, fade_xyzw.z), mix(n_0w.w, n_1w.w, fade_xyzw.z));
		Vector2F n_yzw(mix(n_zw.x, n_zw.y, fade_xyzw.y), mix(n_zw.z, n_zw.w, fade_xyzw.y));
		float n_xyzw = mix(n_yzw.x, n_yzw.y, fade_xyzw.x);

		return static_cast<float>(2.2) * n_xyzw;
	}

	// =====================================================================
	// Simplex noise (ported from GLM gtc_noise, no GLM dependency)
	// =====================================================================

	float Simplex(float p)
	{
		Vector2F pos(static_cast<float>(p), 0.f);
		return Simplex(pos);
	}

	float Simplex(const Vector2F& v)
	{
		using namespace NoiseDetail;

		const float C_0 = 0.211324865405187f;  // (3.0 - sqrt(3.0)) / 6.0
		const float C_1 = 0.366025403784439f;   // 0.5 * (sqrt(3.0) - 1.0)
		const float C_2 = -0.577350269189626f;  // -1.0 + 2.0 * C_0
		const float C_3 = 0.024390243902439f;   // 1.0 / 41.0

		float dotVV = v.x * C_1 + v.y * C_1;
		Vector2F i(Floor(v.x + dotVV), Floor(v.y + dotVV));
		Vector2F x0(v.x - (i.x - i.x * C_0), v.y - (i.y - i.y * C_0));

		Vector2F i1 = (x0.x > x0.y) ? Vector2F(1.f, 0.f) : Vector2F(0.f, 1.f);

		float x1_x = x0.x - i1.x + C_0;
		float x1_y = x0.y - i1.y + C_0;
		float x2_x = x0.x + C_2;
		float x2_y = x0.y + C_2;

		i.x = mod289(i.x);
		i.y = mod289(i.y);

		float p0 = permute(permute(i.y) + i.x);
		float p1 = permute(permute(i.y + i1.y) + (i.x + i1.x));
		float p2 = permute(permute(i.y + 1.f) + (i.x + 1.f));

		Vector3F pVec(p0, p1, p2);

		float m0 = Max(0.f, 0.5f - (x0.x * x0.x + x0.y * x0.y));
		float m1 = Max(0.f, 0.5f - (x1_x * x1_x + x1_y * x1_y));
		float m2 = Max(0.f, 0.5f - (x2_x * x2_x + x2_y * x2_y));
		m0 *= m0; m1 *= m1; m2 *= m2;
		m0 *= m0; m1 *= m1; m2 *= m2;

		Vector3F xGrad(2.f * Fract(pVec.x * C_3) - 0.5f,
			2.f * Fract(pVec.y * C_3) - 0.5f,
			2.f * Fract(pVec.z * C_3) - 0.5f);
		Vector3F hGrad(Abs(xGrad) - 0.5f);
		Vector3F ox(Floor(xGrad + 0.5f));
		Vector3F a0(xGrad - ox);

		float taylor0 = 1.79284291400159f - 0.85373472095314f * (a0.x * a0.x + hGrad.x * hGrad.x);
		float taylor1 = 1.79284291400159f - 0.85373472095314f * (a0.y * a0.y + hGrad.y * hGrad.y);
		float taylor2 = 1.79284291400159f - 0.85373472095314f * (a0.z * a0.z + hGrad.z * hGrad.z);

		m0 *= taylor0; m1 *= taylor1; m2 *= taylor2;

		float g0 = a0.x * x0.x + hGrad.x * x0.y;
		float g1 = a0.y * x1_x + hGrad.y * x1_y;
		float g2 = a0.z * x2_x + hGrad.z * x2_y;

		return 130.f * (m0 * g0 + m1 * g1 + m2 * g2);
	}

	float Simplex(const Vector3F& v)
	{
		using namespace NoiseDetail;

		const float C_x = 1.0f / 6.0f;
		const float C_y = 1.0f / 3.0f;
		const float D_0 = 0.f;
		const float D_1 = 0.5f;
		const float D_2 = 1.f;
		const float D_3 = 2.f;

		float dotV = v.x * C_y + v.y * C_y + v.z * C_y;
		Vector3F i(Floor(v.x + dotV), Floor(v.y + dotV), Floor(v.z + dotV));
		Vector3F x0(v.x - (i.x - i.x * C_x), v.y - (i.y - i.y * C_x), v.z - (i.z - i.z * C_x));

		Vector3F g(Step(x0.y, x0.x), Step(x0.z, x0.y), Step(x0.x, x0.z));
		Vector3F l(1.f - g.x, 1.f - g.y, 1.f - g.z);

		Vector3F i1(Min(g.x, l.z), Min(g.y, l.x), Min(g.z, l.y));
		Vector3F i2(Max(g.x, l.z), Max(g.y, l.x), Max(g.z, l.y));

		Vector3F x1(x0.x - i1.x + C_x, x0.y - i1.y + C_x, x0.z - i1.z + C_x);
		Vector3F x2(x0.x - i2.x + C_y, x0.y - i2.y + C_y, x0.z - i2.z + C_y);
		Vector3F x3(x0.x - D_1, x0.y - D_1, x0.z - D_1);

		i.x = mod289(i.x);
		i.y = mod289(i.y);
		i.z = mod289(i.z);

		float pj0 = permute(permute(permute(i.z) + i.y) + i.x);
		float pj1 = permute(permute(permute(i.z + i1.z) + (i.y + i1.y)) + (i.x + i1.x));
		float pj2 = permute(permute(permute(i.z + i2.z) + (i.y + i2.y)) + (i.x + i2.x));
		float pj3 = permute(permute(permute(i.z + 1.f) + (i.y + 1.f)) + (i.x + 1.f));

		const float n_ = 0.142857142857f; // 1.0/7.0
		Vector3F ns(n_ * D_3 - D_0, n_ * D_1 - D_2, n_ * D_2 - D_0);

		auto mod49 = [](float p) -> float {
			return p - Floor(p / 49.f) * 49.f;
		};

		float j0 = mod49(pj0); float j1 = mod49(pj1);
		float j2 = mod49(pj2); float j3 = mod49(pj3);

		Vector4F x_(Floor(j0 * ns.z), Floor(j1 * ns.z), Floor(j2 * ns.z), Floor(j3 * ns.z));
		Vector4F y_(Floor(j0 - 7.f * x_.x), Floor(j1 - 7.f * x_.y),
			Floor(j2 - 7.f * x_.z), Floor(j3 - 7.f * x_.w));

		Vector4F x(x_.x * ns.x + ns.y, x_.y * ns.x + ns.y,
			x_.z * ns.x + ns.y, x_.w * ns.x + ns.y);
		Vector4F y(y_.x * ns.x + ns.y, y_.y * ns.x + ns.y,
			y_.z * ns.x + ns.y, y_.w * ns.x + ns.y);
		Vector4F h(1.f - Abs(x.x) - Abs(y.x), 1.f - Abs(x.y) - Abs(y.y),
			1.f - Abs(x.z) - Abs(y.z), 1.f - Abs(x.w) - Abs(y.w));

		Vector4F b0(x.x, x.z, y.x, y.z);
		Vector4F b1(x.y, x.w, y.y, y.w);

		Vector4F s0(Floor(b0.x) * 2.f + 1.f, Floor(b0.y) * 2.f + 1.f,
			Floor(b0.z) * 2.f + 1.f, Floor(b0.w) * 2.f + 1.f);
		Vector4F s1(Floor(b1.x) * 2.f + 1.f, Floor(b1.y) * 2.f + 1.f,
			Floor(b1.z) * 2.f + 1.f, Floor(b1.w) * 2.f + 1.f);
		Vector4F sh(-Step(0.f, h.x), -Step(0.f, h.y), -Step(0.f, h.z), -Step(0.f, h.w));

		Vector4F a0(b0.x + s0.x * sh.x, b0.z + s0.z * sh.z,
			b0.y + s0.y * sh.y, b0.w + s0.w * sh.w);
		Vector4F a1(b1.x + s1.x * sh.z, b1.z + s1.z * sh.w,
			b1.y + s1.y * sh.y, b1.w + s1.w * sh.w);

		Vector3F p0(a0.x, a0.y, h.x);
		Vector3F p1(a0.z, a0.w, h.y);
		Vector3F p2(a1.x, a1.y, h.z);
		Vector3F p3(a1.z, a1.w, h.w);

		auto dot3 = [](const Vector3F& a, const Vector3F& b) -> float {
			return a.x * b.x + a.y * b.y + a.z * b.z;
		};

		float n0 = taylorInvSqrt(dot3(p0, p0));
		float n1 = taylorInvSqrt(dot3(p1, p1));
		float n2 = taylorInvSqrt(dot3(p2, p2));
		float n3 = taylorInvSqrt(dot3(p3, p3));
		p0 *= n0; p1 *= n1; p2 *= n2; p3 *= n3;

		Vector4F m(Max(Vector4F(0.6f, 0.6f, 0.6f, 0.6f) - Vector4F(dot3(x0, x0), dot3(x1, x1), dot3(x2, x2), dot3(x3, x3)),
			Vector4F(0.f, 0.f, 0.f, 0.f)));
		m = m * m;

		return 42.f * (m.x * m.x * dot3(p0, x0) + m.y * m.y * dot3(p1, x1) +
			m.z * m.z * dot3(p2, x2) + m.w * m.w * dot3(p3, x3));
	}

	float Simplex(const Vector4F& v)
	{
		using namespace NoiseDetail;

		const float C_x = 0.138196601125011f;   // (5 - sqrt(5))/20  G4
		const float C_y = 0.276393202250021f;    // 2 * G4
		const float C_z = 0.414589803375032f;    // 3 * G4
		const float C_w = -0.447213595499958f;   // -1 + 4 * G4
		const float F4 = 0.309016994374947451f;  // (sqrt(5) - 1)/4

		float dotV = v.x * F4 + v.y * F4 + v.z * F4 + v.w * F4;
		Vector4F i(Floor(v.x + dotV), Floor(v.y + dotV), Floor(v.z + dotV), Floor(v.w + dotV));
		Vector4F x0(v.x - (i.x - i.x * C_x), v.y - (i.y - i.y * C_x),
			v.z - (i.z - i.z * C_x), v.w - (i.w - i.w * C_x));

		// Rank sorting
		Vector3F isX(Step(x0.y, x0.x), Step(x0.z, x0.x), Step(x0.w, x0.x));
		Vector3F isYZ(Step(x0.z, x0.y), Step(x0.w, x0.y), Step(x0.w, x0.z));

		Vector4F i0(isX.x + isX.y + isX.z, 1.f - isX.x, 1.f - isX.y, 1.f - isX.z);
		i0.y += isYZ.x + isYZ.y;
		i0.z += (1.f - isYZ.x) + isYZ.z;
		i0.w += (1.f - isYZ.y) + (1.f - isYZ.z);

		Vector4F i3(Clamp(i0.x, 0.f, 1.f), Clamp(i0.y, 0.f, 1.f),
			Clamp(i0.z, 0.f, 1.f), Clamp(i0.w, 0.f, 1.f));
		Vector4F i2(Clamp(i0.x - 1.f, 0.f, 1.f), Clamp(i0.y - 1.f, 0.f, 1.f),
			Clamp(i0.z - 1.f, 0.f, 1.f), Clamp(i0.w - 1.f, 0.f, 1.f));
		Vector4F i1(Clamp(i0.x - 2.f, 0.f, 1.f), Clamp(i0.y - 2.f, 0.f, 1.f),
			Clamp(i0.z - 2.f, 0.f, 1.f), Clamp(i0.w - 2.f, 0.f, 1.f));

		Vector4F x1(x0.x - i1.x + C_x, x0.y - i1.y + C_x,
			x0.z - i1.z + C_x, x0.w - i1.w + C_x);
		Vector4F x2(x0.x - i2.x + C_y, x0.y - i2.y + C_y,
			x0.z - i2.z + C_y, x0.w - i2.w + C_y);
		Vector4F x3(x0.x - i3.x + C_z, x0.y - i3.y + C_z,
			x0.z - i3.z + C_z, x0.w - i3.w + C_z);
		Vector4F x4(x0.x + C_w, x0.y + C_w, x0.z + C_w, x0.w + C_w);

		i.x = mod289(i.x); i.y = mod289(i.y);
		i.z = mod289(i.z); i.w = mod289(i.w);

		float j0 = permute(permute(permute(permute(i.w) + i.z) + i.y) + i.x);

		float j1_x = permute(permute(permute(permute(i.w + i1.w) + (i.z + i1.z)) + (i.y + i1.y)) + (i.x + i1.x));
		float j1_y = permute(permute(permute(permute(i.w + i2.w) + (i.z + i2.z)) + (i.y + i2.y)) + (i.x + i2.x));
		float j1_z = permute(permute(permute(permute(i.w + i3.w) + (i.z + i3.z)) + (i.y + i3.y)) + (i.x + i3.x));
		float j1_w = permute(permute(permute(permute(i.w + 1.f) + (i.z + 1.f)) + (i.y + 1.f)) + (i.x + 1.f));

		Vector4F ip(1.f / 294.f, 1.f / 49.f, 1.f / 7.f, 0.f);

		auto grad4 = [](float j, const Vector4F& ip) -> Vector4F {
			Vector3F pXYZ = Floor(Fract(Vector3F(j, j, j)) * Vector3F(ip.x, ip.y, ip.z)) * ip.w - Vector3F(1.f, 1.f, 1.f);
			float pW = 1.5f - (Abs(pXYZ.x) + Abs(pXYZ.y) + Abs(pXYZ.z));
			Vector4F s;
			s.x = Step(0.f, pXYZ.x); s.y = Step(0.f, pXYZ.y);
			s.z = Step(0.f, pXYZ.z); s.w = Step(0.f, pW);
			pXYZ.x += (2.f * s.x - 1.f) * s.w;
			pXYZ.y += (2.f * s.y - 1.f) * s.w;
			pXYZ.z += (2.f * s.z - 1.f) * s.w;
			return Vector4F(pXYZ.x, pXYZ.y, pXYZ.z, pW);
		};

		auto dotVec = [](const Vector4F& a, const Vector4F& b) -> float {
			return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		};

		Vector4F p0 = grad4(j0, ip);
		Vector4F p1 = grad4(j1_x, ip);
		Vector4F p2 = grad4(j1_y, ip);
		Vector4F p3 = grad4(j1_z, ip);
		Vector4F p4 = grad4(j1_w, ip);

		float n0 = taylorInvSqrt(dotVec(p0, p0));
		float n1 = taylorInvSqrt(dotVec(p1, p1));
		float n2 = taylorInvSqrt(dotVec(p2, p2));
		float n3 = taylorInvSqrt(dotVec(p3, p3));
		p0 *= n0; p1 *= n1; p2 *= n2; p3 *= n3;
		p4 *= taylorInvSqrt(dotVec(p4, p4));

		auto dot3f = [](float ax, float ay, float az, float bx, float by, float bz) -> float {
			return ax * bx + ay * by + az * bz;
		};

		float m0_x = Max(0.6f - dotVec(x0, x0), 0.f);
		float m0_y = Max(0.6f - dotVec(x1, x1), 0.f);
		float m0_z = Max(0.6f - dotVec(x2, x2), 0.f);
		float m1_x = Max(0.6f - dotVec(x3, x3), 0.f);
		float m1_y = Max(0.6f - dotVec(x4, x4), 0.f);

		m0_x *= m0_x; m0_y *= m0_y; m0_z *= m0_z;
		m1_x *= m1_x; m1_y *= m1_y;

		return 49.f * (m0_x * m0_x * dotVec(p0, x0) +
			m0_y * m0_y * dotVec(p1, x1) +
			m0_z * m0_z * dotVec(p2, x2) +
			m1_x * m1_x * dotVec(p3, x3) +
			m1_y * m1_y * dotVec(p4, x4));
	}

} // namespace Math
