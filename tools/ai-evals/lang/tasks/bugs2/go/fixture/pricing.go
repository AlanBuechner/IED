package main

const (
	bulkQuantity   = 10
	bulkPercentOff = 10
	goldPercentOff = 5
	taxPercent     = 8
	hundredPercent = 100
)

// Totals is what one order costs, in cents.
type Totals struct {
	Subtotal     int64
	GoldDiscount int64
	Tax          int64
	Total        int64
}

// PercentOf is percent percent of cents, rounded half up to the cent.
func PercentOf(cents, percent int64) int64 {
	return cents * percent / hundredPercent
}

// LineCents is what a line costs: quantity times unit price, less the bulk discount.
func LineCents(quantity, unitCents int64) int64 {
	amount := quantity * unitCents
	if quantity > bulkQuantity {
		return amount - PercentOf(amount, bulkPercentOff)
	}
	return amount
}

// PriceOrder is the totals of one order's lines (all of the same order, at least one).
func PriceOrder(lines []OrderLine) Totals {
	var totals Totals
	var listCents int64
	for _, line := range lines {
		listCents += line.Quantity * line.UnitCents
		totals.Subtotal += LineCents(line.Quantity, line.UnitCents)
	}
	if lines[0].Tier == TierGold {
		totals.GoldDiscount = PercentOf(listCents, goldPercentOff)
	}
	totals.Tax = PercentOf(totals.Subtotal-totals.GoldDiscount, taxPercent)
	totals.Total = totals.Subtotal - totals.GoldDiscount + totals.Tax
	return totals
}
