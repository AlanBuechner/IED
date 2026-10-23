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
	indexOf := make(map[string]int)
	for _, line := range lines {
		index, found := indexOf[line.Order]
		if !found {
			index = len(orders)
			indexOf[line.Order] = index
			orders = append(orders, Order{ID: line.Order, Customer: line.Customer, Tier: line.Tier})
		}
		orders[index].Lines = append(orders[index].Lines, line)
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
	var average int64
	if count > 0 {
		average = (sum + count/2) / count
	}
	fmt.Fprintf(&out, "orders %d total %s\naverage %s\n", count, formatMoney(sum), formatMoney(average))
	return out.String()
}
