# TODO list

- [ ] implement input functions
	- [ ] `readbyte` (C)
	- [ ] `readline`
- [ ] implement a JSON/RPC reader/writer
	- (the JSON library will be the hard part, the RPC reader just piggybacks off of it)
- [ ] implement a fe parser
	- [ ] implement a simple LISP reader
	- [ ] extend with parser for common forms like `(= name (fn () ... ))`/`(= name (mac () ... ))`
	- [ ] a macro expander for *everyone's sake*
