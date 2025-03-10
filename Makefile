DBG := gdb
OUT := ./false

$(OUT): main.c
	gcc -g fe/src/fe.c $^ -o $@

run: $(OUT)
	$(OUT)

debug: $(OUT)
	$(DBG) $(OUT)
