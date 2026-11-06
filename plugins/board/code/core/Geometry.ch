// Plane geometry for the board: points, boxes, distances, polylines (a flat array of x, y pairs), their
// simplification and convex hull. World units are CSS pixels at 100% zoom, y down.

include "bedrock.ch"
include "math.ch"

namespace Boards
{
	const double c_Far = 1e300;

	struct Point
	{
		public double m_X;
		public double m_Y;
	}

	// An axis-aligned box; empty while m_X0 > m_X1.
	struct Box
	{
		public double m_X0 = c_Far;
		public double m_Y0 = c_Far;
		public double m_X1 = -c_Far;
		public double m_Y1 = -c_Far;

		public bool IsEmpty() const { return m_X0 > m_X1 || m_Y0 > m_Y1; }
		public double Width() const { return IsEmpty() ? 0.0 : m_X1 - m_X0; }
		public double Height() const { return IsEmpty() ? 0.0 : m_Y1 - m_Y0; }
		public double CenterX() const { return (m_X0 + m_X1) * 0.5; }
		public double CenterY() const { return (m_Y0 + m_Y1) * 0.5; }
		public double Diagonal() const { return Math::Sqrt(Width() * Width() + Height() * Height()); }
		public bool Contains(double x, double y) const { return x >= m_X0 && x <= m_X1 && y >= m_Y0 && y <= m_Y1; }
		public bool ContainsBox(const Box& other) const { return !other.IsEmpty() && other.m_X0 >= m_X0 && other.m_X1 <= m_X1 && other.m_Y0 >= m_Y0 && other.m_Y1 <= m_Y1; }
		public bool Overlaps(const Box& other) const { return !IsEmpty() && !other.IsEmpty() && other.m_X0 <= m_X1 && other.m_X1 >= m_X0 && other.m_Y0 <= m_Y1 && other.m_Y1 >= m_Y0; }
		public void Add(double x, double y) {
			if (x < m_X0) m_X0 = x;
			if (x > m_X1) m_X1 = x;
			if (y < m_Y0) m_Y0 = y;
			if (y > m_Y1) m_Y1 = y;
		}
		public void AddBox(const Box& other) {
			if (other.IsEmpty()) return;
			Add(other.m_X0, other.m_Y0);
			Add(other.m_X1, other.m_Y1);
		}
		public Box Inflated(double by) const {
			Box box = *this;
			if (IsEmpty()) return box;
			box.m_X0 -= by;
			box.m_Y0 -= by;
			box.m_X1 += by;
			box.m_Y1 += by;
			return box;
		}
		// The area both boxes cover.
		public double OverlapArea(const Box& other) const {
			const double w = Math::Min(m_X1, other.m_X1) - Math::Max(m_X0, other.m_X0);
			const double h = Math::Min(m_Y1, other.m_Y1) - Math::Max(m_Y0, other.m_Y0);
			return w > 0.0 && h > 0.0 ? w * h : 0.0;
		}
	}

	Box BoxAt(double x, double y, double w, double h);
	// The box around a polyline's points.
	Box PointsBox(const Core::Array<double>& points);
	// The gap between two boxes (0 when they touch or overlap).
	double BoxGap(const Box& a, const Box& b);

	double Distance(double ax, double ay, double bx, double by);
	// From (px, py) to the segment a-b.
	double SegmentDistance(double px, double py, double ax, double ay, double bx, double by);
	// From (px, py) to the nearest segment of a polyline (a single point counts as one).
	double PolylineDistance(const Core::Array<double>& points, double px, double py);
	double PolylineLength(const Core::Array<double>& points);
	// Ramer-Douglas-Peucker: the polyline with every point closer than `epsilon` to its neighbours' chord left out.
	Core::Array<double> Simplify(const Core::Array<double>& points, double epsilon);
	// The convex hull of the points, counter-clockwise (in a y-up sense), as x, y pairs.
	Core::Array<double> ConvexHull(const Core::Array<double>& points);
	// The area a closed polygon encloses (its points as x, y pairs; the last joins the first).
	double PolygonArea(const Core::Array<double>& polygon);
	// Whether (px, py) is inside a closed polygon (even-odd).
	bool InsidePolygon(const Core::Array<double>& polygon, double px, double py);
	// The point where segment a-b crosses the closed polygon's outline, the crossing nearest to b; false when it does
	// not cross.
	bool ClipToOutline(const Core::Array<double>& polygon, double ax, double ay, double bx, double by, double& x, double& y);
	// The angle (radians, 0 to pi) between the directions a->b and b->c.
	double TurnAngle(double ax, double ay, double bx, double by, double cx, double cy);
}
