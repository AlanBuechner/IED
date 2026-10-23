package main

import "testing"

func lineOf(tier Tier, quantity, unitCents int64) OrderLine {
	return OrderLine{Order: "1", Customer: "c", Tier: tier, Quantity: quantity, UnitCents: unitCents}
}

func TestBulkStartsAtTen(t *testing.T) {
	if got := LineCents(10, 250); got != 2250 {
		t.Errorf("ten get the discount: got %d", got)
	}
	if got := LineCents(9, 250); got != 2250 {
		t.Errorf("nine do not: got %d", got)
	}
}

func TestGoldDiscountsTheSubtotal(t *testing.T) {
	totals := PriceOrder([]OrderLine{lineOf(TierGold, 20, 100)})
	if totals.Subtotal != 1800 {
		t.Errorf("subtotal: got %d", totals.Subtotal)
	}
	if totals.GoldDiscount != 90 {
		t.Errorf("5%% of the discounted subtotal: got %d", totals.GoldDiscount)
	}
}

func TestRoundsHalfUp(t *testing.T) {
	if got := PercentOf(1007, 8); got != 81 {
		t.Errorf("80.56 cents: got %d", got)
	}
	if got := PercentOf(10, 5); got != 1 {
		t.Errorf("0.5 cent: got %d", got)
	}
}

func TestReportsNoOrders(t *testing.T) {
	if got := Report(nil); got != "orders 0 total 0.00\naverage 0.00\n" {
		t.Fatalf("no orders: got %q", got)
	}
}
