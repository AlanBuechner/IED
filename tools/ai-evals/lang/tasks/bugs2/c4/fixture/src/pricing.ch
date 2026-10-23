include "orderline.ch"

namespace Invoice
{
	const int64 kBulkQuantity = 10;
	const int64 kBulkPercentOff = 10;
	const int64 kGoldPercentOff = 5;
	const int64 kTaxPercent = 8;

	struct Totals
	{
		public int64 subtotal;
		public int64 goldDiscount;
		public int64 tax;
		public int64 total;
	}

	// `percent` percent of `cents`, rounded half up to the cent.
	int64 PercentOf(int64 cents, int64 percent);
	// What a line costs: quantity times unit price, less the bulk discount.
	int64 LineCents(int64 quantity, int64 unitCents);
	// The totals of one order's lines (all of the same order, at least one).
	Totals PriceOrder(const Core::Array<OrderLine>& lines);
}
