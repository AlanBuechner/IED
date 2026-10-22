package main

import (
	"encoding/json"
	"slices"
	"testing"
)

func paidOrder(customer string, items ...Item) Order {
	return Order{Customer: customer, Status: Paid, Items: items}
}

func TestAggregateCountsMoneyAndUnitsOfPaidOrdersOnly(t *testing.T) {
	report := Aggregate([]Order{
		paidOrder("ann", Item{SKU: "A", Qty: 2, Cents: 350}, Item{SKU: "B", Qty: 1, Cents: 1000}),
		{Customer: "ann", Status: Cancelled, Items: []Item{{SKU: "B", Qty: 9, Cents: 1000}}},
		{Customer: "bob", Status: Refunded, Items: []Item{{SKU: "C", Qty: 1, Cents: 99}}},
		paidOrder("ann", Item{SKU: "B", Qty: 1, Cents: 999}),
	})
	ann := report.Customers["ann"]
	if ann.Orders != 3 || ann.PaidOrders != 2 || ann.Spent != 2699 || ann.FavoriteSKU == nil || *ann.FavoriteSKU != "A" {
		t.Errorf("ann = %+v", ann)
	}
	if bob := report.Customers["bob"]; bob.Orders != 1 || bob.PaidOrders != 0 || bob.Spent != 0 || bob.FavoriteSKU != nil {
		t.Errorf("bob = %+v", bob)
	}
	want := Totals{Orders: 4, Paid: 2, Cancelled: 1, Refunded: 1, Revenue: 2699}
	if report.Totals != want {
		t.Errorf("totals = %+v, want %+v", report.Totals, want)
	}
}

func TestAggregateOfNoOrdersIsEmpty(t *testing.T) {
	report := Aggregate(nil)
	if len(report.Customers) != 0 || report.Totals != (Totals{}) || len(report.TopSKUs) != 0 {
		t.Errorf("report = %+v", report)
	}
	text, err := json.Marshal(report)
	if err != nil || string(text) != `{"customers":{},"totals":{"orders":0,"paid":0,"cancelled":0,"refunded":0,"revenue":0},"top_skus":[]}` {
		t.Errorf("json = %s (%v)", text, err)
	}
}

func TestFavoriteSKUTiesToTheSmallestSKU(t *testing.T) {
	favorite := FavoriteSKU(map[string]int64{"m-2": 2, "x": 2, "m-10": 2, "a": 1})
	if favorite == nil || *favorite != "m-10" {
		t.Errorf("favorite = %v", favorite)
	}
	if FavoriteSKU(map[string]int64{}) != nil {
		t.Error("a favorite without units")
	}
}

func TestTopSKUsRanksByUnitsThenSKUAndKeepsN(t *testing.T) {
	units := map[string]int64{"b": 5, "a": 5, "sku-9": 3, "Z-9": 3, "e": 3, "sku-10": 3, "f": 1}
	want := []SKUUnits{{"a", 5}, {"b", 5}, {"Z-9", 3}, {"e", 3}, {"sku-10", 3}}
	if got := TopSKUs(units, 5); !slices.Equal(got, want) {
		t.Errorf("top = %v, want %v", got, want)
	}
	if got := TopSKUs(map[string]int64{"a": 1}, 5); len(got) != 1 {
		t.Errorf("top of one = %v", got)
	}
}

func TestCentsAreWrittenAsDollars(t *testing.T) {
	for cents, want := range map[Cents]string{0: "0", 5: "0.05", 1250: "12.5", 127893: "1278.93"} {
		if text, _ := json.Marshal(cents); string(text) != want {
			t.Errorf("%d cents = %s, want %s", cents, text, want)
		}
	}
}
