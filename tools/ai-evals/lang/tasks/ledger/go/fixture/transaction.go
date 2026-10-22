package main

import (
	"errors"
	"fmt"
	"strings"
)

const (
	fieldCount = 5
	dateLength = 10
)

// Transaction is one row of the ledger.
type Transaction struct {
	Date     string // YYYY-MM-DD
	Account  string
	Category string
	Cents    int64 // negative for spending
	Memo     string
}

func isDate(text string) bool {
	if len(text) != dateLength || text[4] != '-' || text[7] != '-' {
		return false
	}
	for i := 0; i < dateLength; i++ {
		if i != 4 && i != 7 && (text[i] < '0' || text[i] > '9') {
			return false
		}
	}
	return true
}

// ParseTransaction reads one CSV line "date,account,category,amount,memo"; the error says why it is not one.
func ParseTransaction(line string) (Transaction, error) {
	fields := strings.Split(line, ",")
	if len(fields) != fieldCount {
		return Transaction{}, errors.New("expected 5 fields")
	}
	if !isDate(fields[0]) {
		return Transaction{}, errors.New("bad date")
	}
	if fields[1] == "" || fields[2] == "" {
		return Transaction{}, errors.New("empty account or category")
	}
	cents, ok := ParseCents(fields[3])
	if !ok {
		return Transaction{}, errors.New("bad amount")
	}
	return Transaction{Date: fields[0], Account: fields[1], Category: fields[2], Cents: cents, Memo: fields[4]}, nil
}

// ParseLedger reads every transaction of the CSV text, whose first line is the header; the error names the bad line.
func ParseLedger(text string) ([]Transaction, error) {
	var rows []Transaction
	for i, line := range strings.Split(text, "\n") {
		line = strings.TrimSuffix(line, "\r")
		if i == 0 || line == "" {
			continue
		}
		row, err := ParseTransaction(line)
		if err != nil {
			return nil, fmt.Errorf("line %d: %w", i+1, err)
		}
		rows = append(rows, row)
	}
	return rows, nil
}
