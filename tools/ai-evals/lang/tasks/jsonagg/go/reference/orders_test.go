package main

import (
	"errors"
	"slices"
	"testing"
)

func TestParseOrdersReadsMembersInAnyOrderAndIgnoresUnknownOnes(t *testing.T) {
	orders, err := ParseOrders([]byte(`{"version": 2, "orders": [
		{"items": [{"price": 0.29, "qty": 3, "sku": "G-7", "name": "x"}], "status": "refunded", "note": {"customer": "z"}, "customer": "ann"},
		{"id": 2, "customer": "bob", "date": "2024-03-02", "status": "paid", "items": [{"sku": "A-1", "qty": 1, "price": 10}]}]}`))
	if err != nil {
		t.Fatal(err)
	}
	if len(orders) != 2 || orders[0].Customer != "ann" || orders[0].Status != Refunded || orders[1].Status != Paid {
		t.Fatalf("orders = %+v", orders)
	}
	if !slices.Equal(orders[0].Items, []Item{{SKU: "G-7", Qty: 3, Cents: 29}}) || orders[1].Items[0].Cents != 1000 {
		t.Errorf("items = %+v %+v", orders[0].Items, orders[1].Items)
	}
}

func TestParseOrdersRoundsPricesToTheNearestCent(t *testing.T) {
	orders, err := ParseOrders([]byte(`{"orders": [{"customer": "a", "status": "paid", "items": [{"sku": "s", "qty": 1, "price": 1.13}, {"sku": "t", "qty": 1, "price": 0.07}]}]}`))
	if err != nil || orders[0].Items[0].Cents != 113 || orders[0].Items[1].Cents != 7 {
		t.Errorf("orders = %+v (%v)", orders, err)
	}
}

func TestParseOrdersOfAnEmptyArray(t *testing.T) {
	orders, err := ParseOrders([]byte(`{"orders": []}`))
	if err != nil || len(orders) != 0 {
		t.Errorf("orders = %+v (%v)", orders, err)
	}
}

func TestParseOrdersRejectsWhatIsNotOrders(t *testing.T) {
	const item = `{"sku": "A", "qty": 1, "price": 2}`
	inputs := map[string]string{
		"not json":          `{"orders": [`,
		"no orders":         `{"order": []}`,
		"orders not array":  `{"orders": {}}`,
		"root not object":   `[]`,
		"order not object":  `{"orders": [3]}`,
		"no customer":       `{"orders": [{"status": "paid", "items": [` + item + `]}]}`,
		"no status":         `{"orders": [{"customer": "a", "items": [` + item + `]}]}`,
		"no items":          `{"orders": [{"customer": "a", "status": "paid"}]}`,
		"bad status":        `{"orders": [{"customer": "a", "status": "shipped", "items": [` + item + `]}]}`,
		"status not string": `{"orders": [{"customer": "a", "status": 1, "items": []}]}`,
		"no sku":            `{"orders": [{"customer": "a", "status": "paid", "items": [{"qty": 1, "price": 2}]}]}`,
		"fractional qty":    `{"orders": [{"customer": "a", "status": "paid", "items": [{"sku": "A", "qty": 1.5, "price": 2}]}]}`,
		"zero qty":          `{"orders": [{"customer": "a", "status": "cancelled", "items": [{"sku": "A", "qty": 0, "price": 2}]}]}`,
		"string price":      `{"orders": [{"customer": "a", "status": "paid", "items": [{"sku": "A", "qty": 1, "price": "2"}]}]}`,
		"no price":          `{"orders": [{"customer": "a", "status": "paid", "items": [{"sku": "A", "qty": 1}]}]}`,
	}
	for name, input := range inputs {
		if _, err := ParseOrders([]byte(input)); !errors.Is(err, errInvalidInput) {
			t.Errorf("%s: err = %v", name, err)
		}
	}
}
