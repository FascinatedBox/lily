# First, that hashes create values on-demand.
var config_dict: hash[string, string] = []
config_dict["aaa"] = "bbb"
config_dict["bbb"] = "ccc"

# Second, that hashes can use different keys.
var int_str_map: hash[integer, string] = []
int_str_map[10] = "10"
int_str_map[5000] = "11"
int_str_map[0x10] = "12"

# Doubles as keys, with some exponential stuff too.
var num_str_map: hash[double, string] = []
num_str_map[5.5] = "10"
num_str_map[1e1] = "12"

# static hash creation
var str_str_map: hash[string, string] = ["a" => "b", "c" => "d", "e" => "f"]
# Again, but some of the keys repeat. In this case, the right-most key
# gets the value.
var str_str_map_two: hash[string, string] = ["a" => "a", "a" => "b", "a" => "c",
    "d" => "e"]

# Test for any defaulting with duplicate keys.
var str_any_map: hash[string, any] = ["a" => "1", "b" => 2, "c" => 2,
    "a" => 1]
