package main

import "testing"

func hiddenLine(tier Tier, quantity, unitCents int64) OrderLine {
	return OrderLine{Order: "9", Customer: "h", Tier: tier, Sku: "s", Quantity: quantity, UnitCents: unitCents}
}

func hiddenWant(t *testing.T, what string, got, want int64) {
	t.Helper()
	if got != want {
		t.Errorf("%s: got %d, want %d", what, got, want)
	}
}

func TestHiddenPercentHalfUp(t *testing.T) {
	hiddenWant(t, "80.56", PercentOf(1007, 8), 81)
	hiddenWant(t, "80.48", PercentOf(1006, 8), 80)
	hiddenWant(t, "0.5 rounds up", PercentOf(10, 5), 1)
	hiddenWant(t, "0.45 rounds down", PercentOf(9, 5), 0)
}

func TestHiddenBulkThreshold(t *testing.T) {
	hiddenWant(t, "nine", LineCents(9, 100), 900)
	hiddenWant(t, "ten", LineCents(10, 100), 900)
	hiddenWant(t, "eleven", LineCents(11, 100), 990)
}

func TestHiddenGoldOnSubtotal(t *testing.T) {
	totals := PriceOrder([]OrderLine{hiddenLine(TierGold, 20, 100), hiddenLine(TierGold, 12, 40)})
	hiddenWant(t, "subtotal", totals.Subtotal, 2232)
	hiddenWant(t, "gold", totals.GoldDiscount, 112)
	hiddenWant(t, "tax", totals.Tax, 170)
	hiddenWant(t, "total", totals.Total, 2290)
}

func TestHiddenStandardNoGold(t *testing.T) {
	totals := PriceOrder([]OrderLine{hiddenLine(TierStandard, 1, 1007)})
	hiddenWant(t, "no gold", totals.GoldDiscount, 0)
	hiddenWant(t, "total", totals.Total, 1088)
}

func TestHiddenEmptyReport(t *testing.T) {
	defer func() {
		if r := recover(); r != nil {
			t.Fatalf("no orders: panicked: %v", r)
		}
	}()
	var none []Order
	if got := Report(none); got != "orders 0 total 0.00\naverage 0.00\n" {
		t.Fatalf("no orders: got %q", got)
	}
}
