# First, that hashes create values on-demand.
hash[string, string] config_dict = []
config_dict["aaa"] = "bbb"
config_dict["bbb"] = "ccc"

# Second, that hashes can use different keys.
hash[integer, string] int_str_map = []
int_str_map[10] = "10"
int_str_map[5000] = "11"
int_str_map[0x10] = "12"

# Doubles as keys, with some exponential stuff too.
hash[double, string] num_str_map = []
num_str_map[5.5] = "10"
num_str_map[1e1] = "12"

# static hash creation
hash[string, string] str_str_map = ["a" => "b", "c" => "d", "e" => "f"]
# Again, but some of the keys repeat. In this case, the right-most key
# gets the value.
hash[string, string] str_str_map_two = ["a" => "a", "a" => "b", "a" => "c",
    "d" => "e"]

# Test for any defaulting with duplicate keys.
hash[string, any] str_any_map = ["a" => "1", "b" => 2, "c" => 2,
    "a" => 1]
