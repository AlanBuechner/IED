#pragma once

#include <cmath>

namespace Testing
{
	// ─── Epsilon comparison helpers for math tests ──────────────────────────

	/// Compare two column-major matrices element-by-element using epsilon tolerance.
	/// Both our Math library and GLM store matrices in column-major layout, so
	/// raw memory comparison via glm::value_ptr is valid.
	template <int Rows, int Cols, typename T>
	bool MatricesEpsilonEqual(const float* ours, const T* glmMat, float eps = 1e-4f)
	{
		for (int i = 0; i < Rows * Cols; ++i)
		{
			if (std::abs(static_cast<float>(glmMat[i]) - ours[i]) > eps)
				return false;
		}
		return true;
	}

	/// Compare two vectors element-by-element using epsilon tolerance.
	template <int N>
	bool VectorsEpsilonEqual(const float* ours, const float* glmVec, float eps = 1e-4f)
	{
		for (int i = 0; i < N; ++i)
		{
			if (std::abs(glmVec[i] - ours[i]) > eps)
				return false;
		}
		return true;
	}

	/// Compare quaternions with duality handling: q and -q represent the same rotation.
	/// Both our Quaternion and GLM's qua store components as (x, y, z, w) in memory.
	/// The GLM constructor glm::qua(_w, _x, _y, _z) assigns to .w=_w, .x=_x, .y=_y, .z=_z.
	inline bool QuaternionsEpsilonEqual(const Math::Quaternion& ours, glm::quat glmQ, float eps = 1e-4f)
	{
		auto cmp = [&](float ox, float oy, float oz, float ow)
		{
			return std::abs(ox - glmQ.x) <= eps &&
				std::abs(oy - glmQ.y) <= eps &&
				std::abs(oz - glmQ.z) <= eps &&
				std::abs(ow - glmQ.w) <= eps;
		};
		return cmp(ours.x, ours.y, ours.z, ours.w) ||
			cmp(-ours.x, -ours.y, -ours.z, -ours.w);
	}

	/// Compare two Math::Quaternion objects with duality handling.
	inline bool QuaternionsEpsilonEqual(const Math::Quaternion& a, const Math::Quaternion& b, float eps = 1e-4f)
	{
		auto cmp = [&](float ax, float ay, float az, float aw)
		{
			return std::abs(ax - b.x) <= eps &&
				std::abs(ay - b.y) <= eps &&
				std::abs(az - b.z) <= eps &&
				std::abs(aw - b.w) <= eps;
		};
		return cmp(a.x, a.y, a.z, a.w) ||
			cmp(-a.x, -a.y, -a.z, -a.w);
	}
}
