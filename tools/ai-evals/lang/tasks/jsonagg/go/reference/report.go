package main

import (
	"cmp"
	"slices"
	"strconv"
	"strings"
)

const topSKUCount = 5

// Cents is money in whole cents, written to JSON as a number of dollars.
type Cents int64

func (c Cents) MarshalJSON() ([]byte, error) {
	return strconv.AppendFloat(nil, float64(c)/100, 'f', -1, 64), nil
}

type CustomerReport struct {
	Orders      int     `json:"orders"`
	PaidOrders  int     `json:"paid_orders"`
	Spent       Cents   `json:"spent"`
	FavoriteSKU *string `json:"favorite_sku"`
}

type Totals struct {
	Orders    int   `json:"orders"`
	Paid      int   `json:"paid"`
	Cancelled int   `json:"cancelled"`
	Refunded  int   `json:"refunded"`
	Revenue   Cents `json:"revenue"`
}

type SKUUnits struct {
	SKU string `json:"sku"`
	Qty int64  `json:"qty"`
}

type Report struct {
	Customers map[string]CustomerReport `json:"customers"`
	Totals    Totals                    `json:"totals"`
	TopSKUs   []SKUUnits                `json:"top_skus"`
}

// customerTally is one customer's orders while they are counted: the units it bought in paid orders by sku.
type customerTally struct {
	orders, paidOrders int
	spent              Cents
	units              map[string]int64
}

// Aggregate counts the orders per customer and in total; money and units come from paid orders only.
func Aggregate(orders []Order) Report {
	tallies := map[string]*customerTally{}
	units := map[string]int64{}
	var totals Totals
	for _, order := range orders {
		tally := tallyOf(tallies, order.Customer)
		tally.orders++
		totals.count(order.Status)
		if order.Status == Paid {
			tally.paidOrders++
			spent := addUnits(order.Items, tally.units, units)
			tally.spent += spent
			totals.Revenue += spent
		}
	}
	return Report{Customers: customerReports(tallies), Totals: totals, TopSKUs: TopSKUs(units, topSKUCount)}
}

func tallyOf(tallies map[string]*customerTally, customer string) *customerTally {
	tally, ok := tallies[customer]
	if !ok {
		tally = &customerTally{units: map[string]int64{}}
		tallies[customer] = tally
	}
	return tally
}

func (t *Totals) count(status Status) {
	t.Orders++
	switch status {
	case Paid:
		t.Paid++
	case Cancelled:
		t.Cancelled++
	case Refunded:
		t.Refunded++
	}
}

// addUnits adds the items' units to both tallies by sku and returns what they cost.
func addUnits(items []Item, customerUnits, allUnits map[string]int64) Cents {
	var spent Cents
	for _, item := range items {
		spent += Cents(item.Cents * item.Qty)
		customerUnits[item.SKU] += item.Qty
		allUnits[item.SKU] += item.Qty
	}
	return spent
}

func customerReports(tallies map[string]*customerTally) map[string]CustomerReport {
	reports := make(map[string]CustomerReport, len(tallies))
	for name, tally := range tallies {
		reports[name] = CustomerReport{Orders: tally.orders, PaidOrders: tally.paidOrders, Spent: tally.spent, FavoriteSKU: FavoriteSKU(tally.units)}
	}
	return reports
}

// FavoriteSKU is the sku with the most units, ties to the smallest; nil when there are none.
func FavoriteSKU(units map[string]int64) *string {
	var favorite string
	var most int64
	for sku, qty := range units {
		if qty > most || (qty == most && sku < favorite) {
			favorite, most = sku, qty
		}
	}
	if most == 0 {
		return nil
	}
	return &favorite
}

// TopSKUs is the n skus with the most units, most first, ties by sku.
func TopSKUs(units map[string]int64, n int) []SKUUnits {
	ranked := make([]SKUUnits, 0, len(units))
	for sku, qty := range units {
		ranked = append(ranked, SKUUnits{SKU: sku, Qty: qty})
	}
	slices.SortFunc(ranked, func(a, b SKUUnits) int {
		if byQty := cmp.Compare(b.Qty, a.Qty); byQty != 0 {
			return byQty
		}
		return strings.Compare(a.SKU, b.SKU)
	})
	return ranked[:min(n, len(ranked))]
}
