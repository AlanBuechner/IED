include "canvas.ch"

class Square : public Canvas::Shape
{
	public int32 side = 0;
	public override int32 Area() const;
}

Square* MakeSquare(int32 side);
