package main

import (
	"encoding/json"
	"errors"
	"math"
)

// Status is what became of an order.
type Status string

const (
	Paid      Status = "paid"
	Cancelled Status = "cancelled"
	Refunded  Status = "refunded"
)

// Item is one line of an order; its price is in whole cents.
type Item struct {
	SKU   string
	Qty   int64
	Cents int64
}

type Order struct {
	Customer string
	Status   Status
	Items    []Item
}

var errInvalidInput = errors.New("invalid input")

// The input as it is written: pointers, so a missing member is told apart from a zero one.
type ordersJSON struct {
	Orders *[]orderJSON `json:"orders"`
}

type orderJSON struct {
	Customer *string     `json:"customer"`
	Status   *string     `json:"status"`
	Items    *[]itemJSON `json:"items"`
}

type itemJSON struct {
	SKU   *string  `json:"sku"`
	Qty   *int64   `json:"qty"`
	Price *float64 `json:"price"`
}

// ParseOrders reads the orders of a JSON document {"orders": [...]}; errInvalidInput when it is not JSON of that shape.
func ParseOrders(data []byte) ([]Order, error) {
	var doc ordersJSON
	if err := json.Unmarshal(data, &doc); err != nil || doc.Orders == nil {
		return nil, errInvalidInput
	}
	orders := make([]Order, 0, len(*doc.Orders))
	for _, raw := range *doc.Orders {
		order, ok := raw.order()
		if !ok {
			return nil, errInvalidInput
		}
		orders = append(orders, order)
	}
	return orders, nil
}

func (raw orderJSON) order() (Order, bool) {
	if raw.Customer == nil || raw.Status == nil || raw.Items == nil || !isStatus(Status(*raw.Status)) {
		return Order{}, false
	}
	order := Order{Customer: *raw.Customer, Status: Status(*raw.Status), Items: make([]Item, 0, len(*raw.Items))}
	for _, rawItem := range *raw.Items {
		item, ok := rawItem.item()
		if !ok {
			return Order{}, false
		}
		order.Items = append(order.Items, item)
	}
	return order, true
}

func (raw itemJSON) item() (Item, bool) {
	if raw.SKU == nil || raw.Qty == nil || *raw.Qty <= 0 || raw.Price == nil {
		return Item{}, false
	}
	return Item{SKU: *raw.SKU, Qty: *raw.Qty, Cents: toCents(*raw.Price)}, true
}

func isStatus(status Status) bool {
	return status == Paid || status == Cancelled || status == Refunded
}

func toCents(price float64) int64 {
	return int64(math.Round(price * 100))
}
