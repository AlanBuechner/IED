package main

import (
	"errors"
	"fmt"
	"strconv"
	"strings"
)

const (
	centsPerUnit  = 100
	fieldCount    = 6
	priceDecimals = 2
)

// Tier is a customer's pricing tier.
type Tier int

const (
	TierStandard Tier = iota
	TierGold
)

// OrderLine is one line of an order.
type OrderLine struct {
	Order     string
	Customer  string
	Tier      Tier
	Sku       string
	Quantity  int64
	UnitCents int64
}

// parseInt reads decimal text: an optional '-' and at least one digit, nothing else.
func parseInt(text string) (int64, bool) {
	if strings.HasPrefix(text, "+") {
		return 0, false
	}
	value, err := strconv.ParseInt(text, 10, 64)
	return value, err == nil
}

func parsePrice(text string) (int64, bool) {
	whole, decimals, hasDot := strings.Cut(text, ".")
	if !hasDot || len(decimals) != priceDecimals {
		return 0, false
	}
	units, ok := parseInt(whole)
	if !ok {
		return 0, false
	}
	fraction, ok := parseInt(decimals)
	if !ok || units < 0 || fraction < 0 {
		return 0, false
	}
	return units*centsPerUnit + fraction, true
}

func parseTier(text string) (Tier, bool) {
	switch text {
	case "standard":
		return TierStandard, true
	case "gold":
		return TierGold, true
	}
	return TierStandard, false
}

// ParseLine reads one CSV line "order,customer,tier,sku,quantity,unit_price"; the error says why it is not one.
func ParseLine(text string) (OrderLine, error) {
	fields := strings.Split(text, ",")
	if len(fields) != fieldCount {
		return OrderLine{}, errors.New("expected 6 fields")
	}
	tier, ok := parseTier(fields[2])
	if !ok {
		return OrderLine{}, errors.New("bad tier")
	}
	quantity, ok := parseInt(fields[4])
	if !ok || quantity <= 0 {
		return OrderLine{}, errors.New("bad quantity")
	}
	unitCents, ok := parsePrice(fields[5])
	if !ok {
		return OrderLine{}, errors.New("bad price")
	}
	return OrderLine{Order: fields[0], Customer: fields[1], Tier: tier, Sku: fields[3], Quantity: quantity, UnitCents: unitCents}, nil
}

// ParseOrders reads every order line of the CSV text, whose first line is the header; the error names the bad line.
func ParseOrders(text string) ([]OrderLine, error) {
	var lines []OrderLine
	for i, row := range strings.Split(text, "\n") {
		if i == 0 || row == "" {
			continue
		}
		line, err := ParseLine(row)
		if err != nil {
			return nil, fmt.Errorf("line %d: %w", i+1, err)
		}
		lines = append(lines, line)
	}
	return lines, nil
}
