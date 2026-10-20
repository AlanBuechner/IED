// Core - constants the Forward Const Use tests name before this file declares them (B24).
namespace FcuGeo {
	const int32 kWidth = kBase + Inner::kExtra;
	const int32 kBase = 2;
	namespace Inner { const int32 kExtra = kFcuLanes - 1; }
}
const int32 kFcuLanes = 4;
const int32 kFcuLen = kFcuHalf * 2 - 3;
const int32 kFcuFactor = 5;
const int32 kFcuHalf = 3;
