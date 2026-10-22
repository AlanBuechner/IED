package main

import "testing"

func TestParsesALine(t *testing.T) {
	row, err := ParseTransaction("2024-03-05,checking,groceries,-45.20,Weekly shop")
	if err != nil {
		t.Fatal(err)
	}
	if row.Account != "checking" || row.Category != "groceries" || row.Cents != -4520 || row.Memo != "Weekly shop" {
		t.Fatalf("got %+v", row)
	}
	if _, err := ParseTransaction("2024-03-05,checking,-45.20"); err == nil {
		t.Fatal("too few fields parsed")
	}
}

func TestReportsTheBadLine(t *testing.T) {
	_, err := ParseLedger("date,account,category,amount,memo\n2024-01-01,a,b,1.00,x\n2024-13,a,b,1.00,x\n")
	if err == nil || err.Error() != "line 3: bad date" {
		t.Fatalf("got %v", err)
	}
}
