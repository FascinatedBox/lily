class First() {
	integer @prop_1 = 10
	double @prop_2 = 5.5
	string @prop_3 = "1"
}

class Second() < First() {
	integer @prop_4 = 20
	double @prop_5 = 55.55
	string @prop_6 = "11"
}

class Third() < Second() {
	integer @prop_7 = 40
	double @prop_8 = 777.777
	string @prop_9 = "333"
}

var v = Third::new()
var v2 = Second::new()
var v3 = First::new()
