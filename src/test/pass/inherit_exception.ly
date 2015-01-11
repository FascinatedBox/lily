class MyException(string message) < Exception(message) {
	
}

try: {
	raise MyException::new("Test")
except MyException:
	
}
