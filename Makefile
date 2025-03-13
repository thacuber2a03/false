DBG := gdb
SRC := false.c
OUT := ./false

$(OUT): $(SRC)
	gcc -g fe/src/fe.c $^ -o $@

run: $(OUT)
	$(OUT)

debug: $(OUT)
	$(DBG) $(OUT)

clean:
	rm $(OUT) -f
