package main

import (
	"fmt"
	"strings"
)

// Order is the lines that share one order id.
type Order struct {
	ID       string
	Customer string
	Tier     Tier
	Lines    []OrderLine
}

func formatMoney(cents int64) string {
	return fmt.Sprintf("%d.%02d", cents/centsPerUnit, cents%centsPerUnit)
}

// GroupOrders is the lines grouped by order id, the orders in the order they first appear.
func GroupOrders(lines []OrderLine) []Order {
	var orders []Order
	for _, line := range lines {
		if len(orders) == 0 || orders[len(orders)-1].ID != line.Order {
			orders = append(orders, Order{ID: line.Order, Customer: line.Customer, Tier: line.Tier})
		}
		orders[len(orders)-1].Lines = append(orders[len(orders)-1].Lines, line)
	}
	return orders
}

// Report is one "<order> <customer> <total>" line per order, then "orders <n> total <sum>" and "average <sum / n>".
func Report(orders []Order) string {
	var out strings.Builder
	var sum int64
	for _, order := range orders {
		total := PriceOrder(order.Lines).Total
		sum += total
		fmt.Fprintf(&out, "%s %s %s\n", order.ID, order.Customer, formatMoney(total))
	}
	count := int64(len(orders))
	average := (sum + count/2) / count
	fmt.Fprintf(&out, "orders %d total %s\naverage %s\n", count, formatMoney(sum), formatMoney(average))
	return out.String()
}
