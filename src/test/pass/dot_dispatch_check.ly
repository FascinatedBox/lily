define return_abc( => string) { return "abc" }

list[string] results = []
string abc = "abc"
any abc_any = "abc"

results.append(abc.concat("def"))
results.append((((abc))).concat("def"))
results.append(abc_any.@(string).concat("def"))
results.append(["abc", "def"][0].concat("def"))
results.append(return_abc().concat("def"))

for i in 0...results.size() - 1: {
    if results[i] != "abcdef": {
        print("Failed.\n")
        break
    }
}
